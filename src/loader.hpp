/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "config.hpp"
#include "track-events.hpp"
#include "ytdlp.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct LoadRequest
{
	enum class Kind { Track, Search };

	Kind kind = Kind::Track;
	int radioId = 0;
	int requestId = 0; // the search ID for Kind::Search
	std::string query; // UTF-8
	int maxResults = 0;
};

struct SearchEntry
{
	std::string videoId;
	std::string title;   // UTF-8
	std::string channel; // UTF-8
	std::uint32_t durationMs = 0;
};

struct LoadResult
{
	LoadRequest::Kind kind = LoadRequest::Kind::Track;
	int radioId = 0;
	int requestId = 0;
	std::string query;
	// Track: published as soon as its first seconds are ready; it keeps
	// growing while the rest downloads.
	std::shared_ptr<const Track> track;
	std::vector<SearchEntry> entries;
	std::string error;
};

// Fetches songs with yt-dlp straight into memory and converts them with the
// built-in Opus/AAC decoders and MP3 encoder while they download.  Nothing is
// kept on disk: a song lives in memory while a radio uses it (plus
// audiocast_cache_minutes).  Runs on its own worker threads.
class TrackLoader
{
private:
	using Clock = std::chrono::steady_clock;

	struct Retained
	{
		std::shared_ptr<const Track> track;
		Clock::time_point lastUsed;
	};

	AudioCastConfig config_;
	std::filesystem::path dataDir_;
	std::filesystem::path tempDir_;
	YtDlp ytdlp_;

	std::vector<std::thread> workers_;
	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<LoadRequest> pending_;
	std::vector<LoadResult> results_;
	std::atomic<bool> stopping_ { false };

	// A video is fetched by one worker at a time; others wait and reuse it.
	std::set<std::string> busyVideos_;
	std::condition_variable videoDone_;
	// Songs any radio still holds, so a second radio does not fetch them again.
	std::map<std::string, std::weak_ptr<const Track>> loaded_;
	// Songs kept for audiocast_cache_minutes after their last use.
	std::map<std::string, Retained> retained_;

	void workerLoop(int index);
	void prepare();
	void maintenance();
	void cleanDataDirectory();
	void publish(LoadResult result);
	void processTrack(const LoadRequest& request);
	void processSearch(const LoadRequest& request);
	std::shared_ptr<const Track> reuse(const std::string& videoId);
	void fetch(const LoadRequest& request, const std::string& target, const std::string& knownVideoId);
	std::vector<std::string> ytdlpBaseArgs() const;

public:
	~TrackLoader();

	void start(const AudioCastConfig& config);
	void stop();
	bool running() const { return !workers_.empty(); }

	void submit(LoadRequest request);
	std::vector<LoadResult> drain();
};
