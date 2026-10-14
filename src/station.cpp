/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "station.hpp"
#include <algorithm>

namespace
{
// Frames produced ahead of the wall clock, so a late timer never starves
// the clients.
constexpr std::uint64_t LEAD_FRAMES = 2;
// After a stall (debugger, suspended VM...) at most this many frames are
// sent at once; the rest is skipped so the radio stays on the wall clock.
constexpr std::uint64_t MAX_CATCH_UP_FRAMES = 80;

std::string toString(const std::vector<std::uint8_t>& bytes)
{
	return std::string(bytes.begin(), bytes.end());
}
}

Station::Station(int id, std::string name, EventQueue& events, int bitrateKbps, int burstMs)
	: id_(id)
	, name_(std::move(name))
	, events_(events)
	, silence_(toString(Mp3::makeSilentFrame(bitrateKbps)))
	, burstFrames_(static_cast<std::size_t>(Mp3::msToFrames(static_cast<std::uint64_t>(std::max(burstMs, 0)))))
	, clockStart_(Clock::now())
{
	// Start with a full history of silence: the very first listeners then get
	// the same burst as later ones, which keeps everyone at the same delay.
	Emitted silent;
	silent.bytes = silence_;
	history_.assign(burstFrames_, silent);
	audible_ = silent;
}

void Station::startTrack(QueueEntry entry)
{
	current_ = std::move(entry.track);
	currentRequestId_ = entry.requestId;
	frameIndex_ = 0;
	paused_ = false;
}

void Station::endCurrent(TrackEndReason reason)
{
	if (!current_) return;
	pendingEndings_.push_back(Ending { currentRequestId_, reason });
	current_.reset();
	currentRequestId_ = 0;
	frameIndex_ = 0;
}

void Station::advance()
{
	if (current_ || queue_.empty() || !queue_.front().track) return;
	QueueEntry entry = std::move(queue_.front());
	queue_.pop_front();
	if (entry.requestId == interruptRequestId_) interruptRequestId_ = 0;
	startTrack(std::move(entry));
}

void Station::enqueue(int requestId, const std::string& query, bool interrupt)
{
	std::lock_guard<std::mutex> lock(mutex_);
	QueueEntry entry;
	entry.requestId = requestId;
	entry.query = query;

	if (!interrupt)
	{
		queue_.push_back(std::move(entry));
		return;
	}

	// A newer Radio_Play wins over one that is still loading.
	if (interruptRequestId_ != 0)
	{
		const int previous = interruptRequestId_;
		queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [previous](const QueueEntry& item)
		{
			return item.requestId == previous;
		}), queue_.end());
	}
	interruptRequestId_ = requestId;
	queue_.push_front(std::move(entry));
}

bool Station::waitingFor(int requestId) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return std::any_of(queue_.begin(), queue_.end(), [requestId](const QueueEntry& item)
	{
		return item.requestId == requestId && !item.track;
	});
}

bool Station::trackLoaded(int requestId, std::shared_ptr<const Track> track)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto it = std::find_if(queue_.begin(), queue_.end(), [requestId](const QueueEntry& item)
	{
		return item.requestId == requestId;
	});
	if (it == queue_.end()) return false;
	it->track = std::move(track);

	if (requestId == interruptRequestId_)
	{
		interruptRequestId_ = 0;
		QueueEntry entry = std::move(*it);
		queue_.erase(it);
		endCurrent(TrackEndReason::Replaced);
		startTrack(std::move(entry));
		return true;
	}

	advance();
	return true;
}

bool Station::trackFailed(int requestId)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto it = std::find_if(queue_.begin(), queue_.end(), [requestId](const QueueEntry& item)
	{
		return item.requestId == requestId;
	});
	if (it == queue_.end()) return false;
	queue_.erase(it);
	if (requestId == interruptRequestId_) interruptRequestId_ = 0;
	advance();
	return true;
}

bool Station::skip()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!current_) return false;
	endCurrent(TrackEndReason::Skipped);
	advance();
	return true;
}

void Station::stop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	queue_.clear();
	interruptRequestId_ = 0;
	paused_ = false;
	endCurrent(TrackEndReason::Stopped);
}

std::size_t Station::clearQueue()
{
	std::lock_guard<std::mutex> lock(mutex_);
	const std::size_t count = queue_.size();
	queue_.clear();
	interruptRequestId_ = 0;
	return count;
}

bool Station::setPaused(bool paused)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!current_ || paused_ == paused) return false;
	paused_ = paused;
	return true;
}

bool Station::seek(std::uint32_t positionMs)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!current_) return false;
	// Only as far as the song has been loaded.
	frameIndex_ = std::min(static_cast<std::size_t>(Mp3::msToFrames(positionMs)), current_->frameCount());
	return true;
}

StationStatus Station::status() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	StationStatus status;
	status.queueSize = queue_.size();
	status.loading = std::any_of(queue_.begin(), queue_.end(), [](const QueueEntry& item) { return !item.track; });
	if (audible_.track)
	{
		status.playing = true;
		status.paused = audible_.paused;
		status.requestId = audible_.requestId;
		status.title = audible_.track->title;
		status.videoId = audible_.track->videoId;
		status.durationMs = audible_.track->durationMs();
		status.positionMs = static_cast<std::uint32_t>(std::min<std::uint64_t>(Mp3::framesToMs(audible_.frameIndex), status.durationMs));
	}
	return status;
}

void Station::nextFrame(Emitted& frame)
{
	if (!current_) advance();
	while (current_ && !paused_)
	{
		if (current_->copyFrame(frameIndex_, frame.bytes))
		{
			frame.track = current_;
			frame.requestId = currentRequestId_;
			frame.frameIndex = frameIndex_++;
			return;
		}
		if (!current_->complete()) break; // still downloading: hold the position
		endCurrent(TrackEndReason::Finished);
		advance();
	}

	frame.bytes = silence_;
	frame.track = current_;
	frame.requestId = current_ ? currentRequestId_ : 0;
	frame.frameIndex = frameIndex_;
	frame.paused = paused_;
}

void Station::becomeAudible(Emitted& frame)
{
	for (const Ending& ending : frame.endings)
	{
		if (audibleStarted_.erase(ending.requestId) == 0) continue; // never heard
		AudioCastEvent event;
		event.type = AudioCastEvent::Type::TrackEnd;
		event.radioId = id_;
		event.requestId = ending.requestId;
		event.value = static_cast<int>(ending.reason);
		events_.push(std::move(event));
	}

	if (frame.track && frame.requestId != 0 && audibleStarted_.insert(frame.requestId).second)
	{
		AudioCastEvent event;
		event.type = AudioCastEvent::Type::TrackStart;
		event.radioId = id_;
		event.requestId = frame.requestId;
		event.value = static_cast<int>(frame.track->durationMs());
		event.text = frame.track->title;
		events_.push(std::move(event));
	}

	audible_.track = std::move(frame.track);
	audible_.requestId = frame.requestId;
	audible_.frameIndex = frame.frameIndex;
	audible_.paused = frame.paused;
}

void Station::produce(Clock::time_point now, std::string& output)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - clockStart_).count();
	if (elapsed < 0) return;
	const std::uint64_t due = static_cast<std::uint64_t>(elapsed) * Mp3::SAMPLE_RATE /
		(1000000ull * Mp3::SAMPLES_PER_FRAME) + LEAD_FRAMES;

	if (due > emittedFrames_ + MAX_CATCH_UP_FRAMES)
	{
		// The song keeps following the wall clock even though these frames
		// were never sent.
		const std::uint64_t skipped = due - emittedFrames_ - MAX_CATCH_UP_FRAMES;
		if (current_ && !paused_)
		{
			frameIndex_ = static_cast<std::size_t>(std::min<std::uint64_t>(frameIndex_ + skipped, current_->frameCount()));
		}
		emittedFrames_ += skipped;
	}

	while (emittedFrames_ < due)
	{
		Emitted frame;
		nextFrame(frame);
		frame.endings = std::move(pendingEndings_);
		pendingEndings_.clear();
		output += frame.bytes;
		history_.push_back(std::move(frame));
		while (history_.size() > burstFrames_)
		{
			becomeAudible(history_.front());
			history_.pop_front();
		}
		++emittedFrames_;
	}
}

std::string Station::burst() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string data;
	std::size_t total = 0;
	for (const Emitted& frame : history_) total += frame.bytes.size();
	data.reserve(total);
	for (const Emitted& frame : history_) data += frame.bytes;
	return data;
}

std::string Station::streamTitle() const
{
	// In-band metadata travels with the audio, so it uses the live state.
	std::lock_guard<std::mutex> lock(mutex_);
	return current_ && !paused_ ? current_->title : name_;
}
