/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "mp3.hpp"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// One song as MP3 frames.  It is published to the radios as soon as its first
// seconds are ready and keeps growing while the rest downloads, so playback
// starts long before the whole song is converted.  videoId, title and
// expectedDurationMs are set before publishing and never change afterwards;
// the frames are guarded by a mutex because the loader appends while the
// network thread reads.
class Track
{
private:
	mutable std::mutex mutex_;
	std::vector<std::uint8_t> data_;
	std::vector<Mp3::Frame> frames_;
	bool complete_ = false;
	bool failed_ = false;

public:
	std::string videoId;
	std::string title; // UTF-8
	// From YouTube's metadata; used until the last frame is known.
	std::uint32_t expectedDurationMs = 0;

	// Loader side.
	void append(const std::uint8_t* frame, std::size_t size)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		frames_.push_back(Mp3::Frame { static_cast<std::uint32_t>(data_.size()), static_cast<std::uint32_t>(size) });
		data_.insert(data_.end(), frame, frame + size);
	}

	// No more frames will come.  failed: the download broke off early.
	void finish(bool failed = false)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		complete_ = true;
		failed_ = failed;
		data_.shrink_to_fit();
		frames_.shrink_to_fit();
	}

	// Reader side.  Copies one frame; false while it has not arrived yet.
	bool copyFrame(std::size_t index, std::string& output) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= frames_.size()) return false;
		const Mp3::Frame& frame = frames_[index];
		output.assign(reinterpret_cast<const char*>(data_.data() + frame.offset), frame.size);
		return true;
	}

	std::size_t frameCount() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return frames_.size();
	}

	std::size_t byteCount() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return data_.size();
	}

	bool complete() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return complete_;
	}

	bool failed() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return failed_;
	}

	std::uint32_t durationMs() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto loaded = static_cast<std::uint32_t>(Mp3::framesToMs(frames_.size()));
		return complete_ ? loaded : std::max(loaded, expectedDurationMs);
	}
};

enum class TrackEndReason
{
	Finished = 0,
	Skipped = 1,
	Stopped = 2,
	Replaced = 3,
};

// Raised on worker and network threads, delivered to Pawn on the server tick.
struct AudioCastEvent
{
	enum class Type
	{
		TrackLoaded,
		TrackStart,
		TrackEnd,
		TrackError,
		ListenerConnect,
		ListenerDisconnect,
		SearchResults,
	};

	Type type = Type::TrackLoaded;
	int radioId = 0;
	int requestId = 0;
	int playerId = -1;
	int value = 0;
	std::string text;  // UTF-8
	std::string text2; // UTF-8
};

class EventQueue
{
private:
	std::mutex mutex_;
	std::vector<AudioCastEvent> events_;

public:
	void push(AudioCastEvent event)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		// Pawn may stop ticking (server shutting down); do not grow forever.
		if (events_.size() < 65536) events_.push_back(std::move(event));
	}

	std::vector<AudioCastEvent> drain()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<AudioCastEvent> events;
		events.swap(events_);
		return events;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		events_.clear();
	}
};
