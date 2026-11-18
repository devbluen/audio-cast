/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "loader.hpp"
#include "decoder.hpp"
#include "log.hpp"
#include "process.hpp"
#include "text.hpp"
#include "transcoder.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
// YouTube's best audio stream is about 1 MB per minute; this allows several
// hours while still bounding memory.
constexpr std::size_t MAX_DOWNLOAD_BYTES = 200u * 1024u * 1024u;
constexpr auto MAINTENANCE_INTERVAL = std::chrono::seconds(30);
// A song is handed to the radio once this much of it is converted; the rest
// is converted while it plays (many times faster than real time).
const std::size_t READY_FRAMES = static_cast<std::size_t>(Mp3::msToFrames(3000));
constexpr int MAX_SEARCH_RESULTS = 20;

std::vector<std::string> splitLines(const std::string& text)
{
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
	}
	return lines;
}

std::vector<std::string> splitTabs(const std::string& line)
{
	std::vector<std::string> fields;
	std::size_t start = 0;
	for (;;)
	{
		const std::size_t tab = line.find('\t', start);
		fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
		if (tab == std::string::npos) return fields;
		start = tab + 1;
	}
}

// Text shown in Pawn dialogs and chat: one line, no tabs.
std::string singleLine(std::string text)
{
	std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\r' || c == '\n'; }, ' ');
	return Text::trim(text);
}

std::uint32_t parseDurationMs(const std::string& text)
{
	try
	{
		const double seconds = std::stod(text);
		return seconds > 0 ? static_cast<std::uint32_t>(seconds * 1000.0) : 0;
	}
	catch (...)
	{
		return 0;
	}
}

// The most useful line of a failed run: its last error, or its last line.
std::string describeFailure(const ProcessResult& result, const char* tool)
{
	if (!result.started) return result.error;
	if (result.timedOut) return std::string(tool) + " timed out";
	if (result.outputTooLarge) return "the song is too large";

	std::string lastError;
	std::string lastLine;
	for (const std::string& line : splitLines(result.output))
	{
		const std::string trimmed = Text::trim(line);
		if (trimmed.empty()) continue;
		lastLine = trimmed;
		if (trimmed.find("ERROR:") != std::string::npos) lastError = trimmed;
	}
	std::string message = !lastError.empty() ? lastError : lastLine;
	if (message.empty()) message = std::string(tool) + " exited with code " + std::to_string(result.exitCode);
	if (message.size() > 240) message.resize(240);
	return message;
}
}

TrackLoader::~TrackLoader()
{
	stop();
}

void TrackLoader::start(const AudioCastConfig& config)
{
	if (running()) return;
	config_ = config;
	stopping_ = false;
	dataDir_ = fs::u8path(config_.dataDir);
	tempDir_ = dataDir_ / "tmp";
	ytdlp_.configure(config_.ytdlpPath, dataDir_ / "bin");
	jsRuntime_.configure(config_.jsRuntime, dataDir_ / "deno");

	if (!config_.cookiesFile.empty())
	{
		std::string message;
		const CookiesStatus cookies = CheckCookiesFile(config_.cookiesFile, message);
		if (cookies == CookiesStatus::Ok) AudioCastLogMessage(message);
		else AudioCastLogWarning(message);
		// yt-dlp would create a missing file, and fail every download on an
		// invalid one.
		if (cookies == CookiesStatus::Missing || cookies == CookiesStatus::Invalid) config_.cookiesFile.clear();
	}

	std::error_code error;
	fs::create_directories(tempDir_, error);
	if (error)
	{
		AudioCastLogWarning("could not create '" + config_.dataDir + "': " + error.message());
	}
	cleanDataDirectory();

	for (int index = 0; index < config_.workers; ++index)
	{
		workers_.emplace_back([this, index] { workerLoop(index); });
	}
}

void TrackLoader::cleanDataDirectory()
{
	std::error_code ignored;
	// Leftovers of a crash or restart.
	for (const auto& entry : fs::directory_iterator(tempDir_, ignored))
	{
		fs::remove_all(entry.path(), ignored);
	}
	// Version 1.0 kept songs on disk as <video id>.mp3/.txt; remove them.
	for (const auto& entry : fs::directory_iterator(dataDir_, ignored))
	{
		const fs::path& path = entry.path();
		const std::string extension = path.extension().u8string();
		if ((extension == ".mp3" || extension == ".txt") && Text::isVideoId(path.stem().u8string()))
		{
			fs::remove(path, ignored);
		}
	}
}

void TrackLoader::stop()
{
	if (workers_.empty()) return;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopping_ = true;
		pending_.clear();
	}
	wake_.notify_all();
	videoDone_.notify_all();
	for (std::thread& worker : workers_)
	{
		if (worker.joinable()) worker.join();
	}
	workers_.clear();
	std::lock_guard<std::mutex> lock(mutex_);
	results_.clear();
	busyVideos_.clear();
	loaded_.clear();
	retained_.clear();
}

void TrackLoader::submit(LoadRequest request)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_.push_back(std::move(request));
	}
	wake_.notify_one();
}

std::vector<LoadResult> TrackLoader::drain()
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<LoadResult> results;
	results.swap(results_);
	return results;
}

void TrackLoader::publish(LoadResult result)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!stopping_) results_.push_back(std::move(result));
}

void TrackLoader::prepare()
{
	std::string error;
	if (!ytdlp_.ensure(stopping_, error))
	{
		if (!stopping_) AudioCastLogWarning(error);
		return;
	}
	jsRuntime_.ensure(stopping_);
	ytdlp_.updateIfDue(stopping_);
	jsRuntime_.updateIfDue(stopping_);
}

void TrackLoader::maintenance()
{
	// Forget songs nobody has used for audiocast_cache_minutes.
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto now = Clock::now();
		for (auto it = retained_.begin(); it != retained_.end();)
		{
			if (it->second.track.use_count() > 1) it->second.lastUsed = now;
			if (now - it->second.lastUsed >= std::chrono::minutes(config_.cacheMinutes)) it = retained_.erase(it);
			else ++it;
		}
		for (auto it = loaded_.begin(); it != loaded_.end();)
		{
			if (it->second.expired()) it = loaded_.erase(it);
			else ++it;
		}
	}
	ytdlp_.updateIfDue(stopping_);
	jsRuntime_.updateIfDue(stopping_);
}

void TrackLoader::workerLoop(int index)
{
	if (index == 0) prepare();
	auto nextMaintenance = Clock::now() + MAINTENANCE_INTERVAL;

	for (;;)
	{
		LoadRequest request;
		bool haveRequest = false;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			const auto ready = [this] { return stopping_ || !pending_.empty(); };
			if (index == 0) wake_.wait_until(lock, nextMaintenance, ready);
			else wake_.wait(lock, ready);
			if (stopping_) return;
			if (!pending_.empty())
			{
				request = std::move(pending_.front());
				pending_.pop_front();
				haveRequest = true;
			}
		}

		if (index == 0 && Clock::now() >= nextMaintenance)
		{
			maintenance();
			nextMaintenance = Clock::now() + MAINTENANCE_INTERVAL;
		}
		if (!haveRequest) continue;

		try
		{
			if (request.kind == LoadRequest::Kind::Search) processSearch(request);
			else processTrack(request);
		}
		catch (const std::exception& exception)
		{
			LoadResult result;
			result.kind = request.kind;
			result.radioId = request.radioId;
			result.requestId = request.requestId;
			result.query = request.query;
			result.error = std::string("internal error: ") + exception.what();
			publish(std::move(result));
		}
	}
}

std::vector<std::string> TrackLoader::ytdlpBaseArgs() const
{
	std::vector<std::string> args {
		ytdlp_.path(),
		"--ignore-config",
		"--no-playlist",
		"--no-warnings",
		"--no-progress",
		"--encoding", "utf-8",
		"--cache-dir", (dataDir_ / "ytdlp-cache").u8string(),
	};
	const std::string jsRuntime = jsRuntime_.argument();
	if (!jsRuntime.empty())
	{
		args.push_back("--js-runtimes");
		args.push_back(jsRuntime);
	}
	if (!config_.cookiesFile.empty())
	{
		args.push_back("--cookies");
		args.push_back(config_.cookiesFile);
	}
	return args;
}

void TrackLoader::processSearch(const LoadRequest& request)
{
	LoadResult result;
	result.kind = LoadRequest::Kind::Search;
	result.requestId = request.requestId;
	result.query = request.query;

	const Text::Query query = Text::parseQuery(request.query);
	const int wanted = std::clamp(request.maxResults, 1, MAX_SEARCH_RESULTS);
	if (query.kind == Text::Query::Kind::Invalid)
	{
		result.error = "not a YouTube link, video ID or search";
		publish(std::move(result));
		return;
	}
	if (!ytdlp_.ensure(stopping_, result.error))
	{
		publish(std::move(result));
		return;
	}
	jsRuntime_.ensure(stopping_);

	// Live streams cannot be played and are left out, so ask for a few more.
	const std::string target = query.kind == Text::Query::Kind::Video
		? "https://www.youtube.com/watch?v=" + query.value
		: "ytsearch" + std::to_string(std::min(wanted + 3, MAX_SEARCH_RESULTS + 3)) + ":" + query.value;

	ProcessResult run;
	{
		std::shared_lock<std::shared_mutex> lock(ytdlp_.mutex());
		std::shared_lock<std::shared_mutex> jsLock(jsRuntime_.mutex());
		std::vector<std::string> args = ytdlpBaseArgs();
		args.insert(args.end(), {
			"--flat-playlist",
			"--print", "AUDIOCAST\t%(id)s\t%(duration)s\t%(channel,uploader)s\t%(title)s",
			"--",
			target,
		});
		run = RunProcess(args, config_.downloadTimeoutSec, stopping_);
	}

	for (const std::string& line : splitLines(run.output))
	{
		if (line.compare(0, 10, "AUDIOCAST\t") != 0) continue;
		const std::vector<std::string> fields = splitTabs(line.substr(10));
		if (fields.size() < 4 || !Text::isVideoId(fields[0])) continue;
		SearchEntry entry;
		entry.videoId = fields[0];
		entry.durationMs = parseDurationMs(fields[1]);
		if (entry.durationMs == 0 || entry.durationMs > static_cast<std::uint32_t>(config_.maxDurationSec) * 1000u) continue;
		entry.channel = fields[2] == "NA" ? std::string() : singleLine(fields[2]);
		std::string title = fields[3];
		for (std::size_t index = 4; index < fields.size(); ++index) title += " " + fields[index];
		entry.title = singleLine(title);
		result.entries.push_back(std::move(entry));
		if (static_cast<int>(result.entries.size()) >= wanted) break;
	}

	if (result.entries.empty())
	{
		result.error = run.started && !run.timedOut && run.exitCode == 0 ? "no results" : describeFailure(run, "yt-dlp");
	}
	publish(std::move(result));
}

std::shared_ptr<const Track> TrackLoader::reuse(const std::string& videoId)
{
	// mutex_ is held by the caller.
	const auto retained = retained_.find(videoId);
	if (retained != retained_.end())
	{
		retained->second.lastUsed = Clock::now();
		return retained->second.track;
	}
	const auto loaded = loaded_.find(videoId);
	return loaded != loaded_.end() ? loaded->second.lock() : nullptr;
}

void TrackLoader::processTrack(const LoadRequest& request)
{
	LoadResult result;
	result.radioId = request.radioId;
	result.requestId = request.requestId;
	result.query = request.query;

	const Text::Query query = Text::parseQuery(request.query);
	if (query.kind == Text::Query::Kind::Invalid)
	{
		result.error = "not a YouTube link, video ID or search";
		publish(std::move(result));
		return;
	}

	if (query.kind == Text::Query::Kind::Search)
	{
		// Search and download in one yt-dlp run.
		fetch(request, "ytsearch1:" + query.value, std::string());
		return;
	}

	// Only one worker fetches a given video; the others wait and reuse it.
	const std::string& videoId = query.value;
	{
		std::unique_lock<std::mutex> lock(mutex_);
		std::shared_ptr<const Track> track = reuse(videoId);
		if (!track)
		{
			videoDone_.wait(lock, [&] { return stopping_ || busyVideos_.count(videoId) == 0; });
			if (stopping_) return;
			track = reuse(videoId);
		}
		if (track)
		{
			result.track = std::move(track);
			results_.push_back(std::move(result));
			return;
		}
		busyVideos_.insert(videoId);
	}
	fetch(request, "https://www.youtube.com/watch?v=" + videoId, videoId);
}

void TrackLoader::fetch(const LoadRequest& request, const std::string& target, const std::string& knownVideoId)
{
	LoadResult result;
	result.radioId = request.radioId;
	result.requestId = request.requestId;
	result.query = request.query;

	auto track = std::make_shared<Track>();
	bool published = false;
	const fs::path infoPath = tempDir_ / ("info-" + Text::randomToken(10) + ".txt");

	// Hands the song to the radio; called once enough of it is converted.
	auto publishTrack = [&]
	{
		published = true;
		std::ifstream info(infoPath, std::ios::binary);
		std::string videoId;
		std::string duration;
		std::string title;
		std::getline(info, videoId);
		std::getline(info, duration);
		std::getline(info, title);
		track->videoId = Text::isVideoId(Text::trim(videoId)) ? Text::trim(videoId) : knownVideoId;
		track->expectedDurationMs = parseDurationMs(Text::trim(duration));
		title = singleLine(title);
		track->title = title.empty() || title == "NA" ? (track->videoId.empty() ? request.query : track->videoId) : title;

		std::lock_guard<std::mutex> lock(mutex_);
		if (!knownVideoId.empty()) busyVideos_.erase(knownVideoId);
		if (!track->videoId.empty())
		{
			loaded_[track->videoId] = track;
			if (config_.cacheMinutes > 0) retained_[track->videoId] = Retained { track, Clock::now() };
		}
		result.track = track;
		if (!stopping_) results_.push_back(result);
	};

	auto releaseBusy = [&]
	{
		if (knownVideoId.empty()) return;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			busyVideos_.erase(knownVideoId);
		}
		videoDone_.notify_all();
	};

	if (!ytdlp_.ensure(stopping_, result.error))
	{
		releaseBusy();
		publish(std::move(result));
		return;
	}
	jsRuntime_.ensure(stopping_);

	// The audio (Opus in WebM, or AAC in MP4 for videos without Opus) comes
	// through stdout and is converted as it arrives.  With audiocast_normalize
	// the whole file is needed first (two passes).
	AudioDecoder decoder;
	Mp3Encoder encoder;
	Mp3::FrameSplitter splitter;
	std::vector<std::uint8_t> mp3;
	std::vector<std::uint8_t> whole;
	std::string conversionError;
	std::size_t received = 0;

	auto addMp3 = [&]
	{
		splitter.push(mp3.data(), mp3.size(), [&](const std::uint8_t* frame, std::size_t size) { track->append(frame, size); });
		mp3.clear();
	};
	auto onPcm = [&](const float* stereo, std::size_t frames)
	{
		if (!conversionError.empty()) return;
		if (!encoder.started() && !encoder.begin(decoder.sampleRate(), config_.bitrateKbps, 1.0f, conversionError)) return;
		if (!encoder.encode(stereo, frames, mp3)) conversionError = "MP3 encoding failed";
		addMp3();
	};
	auto onAudio = [&](const std::uint8_t* data, std::size_t size) -> bool
	{
		received += size;
		if (received > MAX_DOWNLOAD_BYTES)
		{
			conversionError = "the song is too large";
			return false;
		}
		if (config_.normalize)
		{
			whole.insert(whole.end(), data, data + size);
			return !stopping_;
		}
		if (!decoder.feed(data, size, onPcm) || !conversionError.empty()) return false;
		if (!published && track->frameCount() >= READY_FRAMES) publishTrack();
		return !stopping_;
	};

	ProcessResult run;
	{
		std::shared_lock<std::shared_mutex> lock(ytdlp_.mutex());
		std::shared_lock<std::shared_mutex> jsLock(jsRuntime_.mutex());
		std::vector<std::string> args = ytdlpBaseArgs();
		args.insert(args.end(), {
			// Decoded by the plugin itself.  Opus first; some videos (often
			// recent uploads) only have AAC.
			"-f", "bestaudio[acodec=opus]/bestaudio[ext=m4a]",
			"--max-filesize", "150M",
			"--match-filter", "!is_live & duration <=? " + std::to_string(config_.maxDurationSec),
			"--print-to-file", "%(id)s\n%(duration)s\n%(title)s", infoPath.u8string(),
			"-o", "-",
			"--",
			target,
		});
		run = RunProcess(args, config_.downloadTimeoutSec, stopping_, 1024 * 1024, 0, onAudio);
	}

	std::string error = conversionError.empty() ? decoder.error() : conversionError;
	if (error.empty() && config_.normalize && !whole.empty() && !stopping_)
	{
		float gain = 1.0f;
		if (MeasureNormalizeGain(whole, stopping_, gain, error))
		{
			AudioDecoder wholeDecoder;
			Mp3Encoder wholeEncoder;
			wholeDecoder.feed(whole.data(), whole.size(), [&](const float* stereo, std::size_t frames)
			{
				if (!error.empty()) return;
				if (!wholeEncoder.started() && !wholeEncoder.begin(wholeDecoder.sampleRate(), config_.bitrateKbps, gain, error)) return;
				if (!wholeEncoder.encode(stereo, frames, mp3)) error = "MP3 encoding failed";
				addMp3();
			});
			if (wholeEncoder.started()) wholeEncoder.finish(mp3);
			addMp3();
			if (error.empty()) wholeDecoder.finish(error);
		}
	}
	else if (error.empty() && encoder.started())
	{
		encoder.finish(mp3);
		addMp3();
	}

	const bool processOk = run.started && !run.timedOut && !run.cancelled && !run.outputTooLarge &&
		(run.exitCode == 0 || run.exitCode == -1);
	if (error.empty() && !processOk)
	{
		error = received == 0 && run.started && run.exitCode == 0
			? "video is live, longer than audiocast_max_duration or has no usable audio"
			: describeFailure(run, "yt-dlp");
	}
	if (error.empty() && !config_.normalize) decoder.finish(error);
	if (error.empty() && received == 0) error = "video is live, longer than audiocast_max_duration or has no usable audio";
	if (error.empty() && track->frameCount() == 0) error = "the song has no audio";

	if (error.empty())
	{
		if (!published) publishTrack();
		track->finish(false);
		if (config_.logSongs) AudioCastLogMessage("loaded " + track->videoId + " (" + track->title + ")");
	}
	else if (published)
	{
		// The radio already plays it: keep what arrived and end there.
		track->finish(true);
		if (!stopping_) AudioCastLogWarning("download of " + track->videoId + " broke off: " + error);
	}
	else
	{
		track->finish(true);
		releaseBusy();
		result.error = stopping_ ? std::string("cancelled") : error;
		publish(std::move(result));
	}

	if (published) videoDone_.notify_all();
	std::error_code ignored;
	fs::remove(infoPath, ignored);
}
