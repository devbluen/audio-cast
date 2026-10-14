/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "track-events.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct StationStatus
{
	bool playing = false;
	bool paused = false;
	bool loading = false;
	int requestId = 0;
	std::string title;
	std::string videoId;
	std::uint32_t positionMs = 0;
	std::uint32_t durationMs = 0;
	std::size_t queueSize = 0;
};

// One radio.  It owns a single output clock: frames leave at exactly the
// audio's real-time rate whether anyone listens or not, so every listener
// receives the same moment of the song.  Silence is sent between songs to
// keep the HTTP streams open.
//
// Listeners hear each frame one burst later than it is produced (the burst
// is what keeps them in sync).  Every produced frame therefore remembers the
// song state it belongs to, and status() and the track callbacks follow the
// frames as they leave the burst - that is, what players actually hear.
//
// Commands come from the server thread and frames are produced on the
// network thread; both sides take the station mutex.
class Station
{
public:
	using Clock = std::chrono::steady_clock;

private:
	struct QueueEntry
	{
		int requestId = 0;
		std::string query;
		std::shared_ptr<const Track> track; // null while loading
	};

	struct Ending
	{
		int requestId = 0;
		TrackEndReason reason = TrackEndReason::Finished;
	};

	// One produced frame and the song state it carries.
	struct Emitted
	{
		std::string bytes;
		std::shared_ptr<const Track> track;
		int requestId = 0;
		std::size_t frameIndex = 0;
		bool paused = false;
		std::vector<Ending> endings; // songs that ended right before this frame
	};

	const int id_;
	const std::string name_;
	EventQueue& events_;
	const std::string silence_;
	const std::size_t burstFrames_;

	mutable std::mutex mutex_;
	// Live state: what is being produced now.
	std::shared_ptr<const Track> current_;
	int currentRequestId_ = 0;
	std::size_t frameIndex_ = 0;
	bool paused_ = false;
	std::deque<QueueEntry> queue_;
	// Request started with Radio_Play: it replaces the current song as soon
	// as it is loaded.
	int interruptRequestId_ = 0;
	std::vector<Ending> pendingEndings_;

	Clock::time_point clockStart_;
	std::uint64_t emittedFrames_ = 0;
	// The most recent frames, sent to each new listener before live audio.
	std::deque<Emitted> history_;
	// Audible state: the frame listeners hear now.
	Emitted audible_;
	std::set<int> audibleStarted_;

	void startTrack(QueueEntry entry);
	void endCurrent(TrackEndReason reason);
	void advance();
	void nextFrame(Emitted& frame);
	void becomeAudible(Emitted& frame);

public:
	Station(int id, std::string name, EventQueue& events, int bitrateKbps, int burstMs);

	int id() const { return id_; }
	const std::string& name() const { return name_; }

	// Server thread.
	void enqueue(int requestId, const std::string& query, bool interrupt);
	bool waitingFor(int requestId) const;
	// Returns false when the request is no longer wanted (stopped/cleared).
	bool trackLoaded(int requestId, std::shared_ptr<const Track> track);
	bool trackFailed(int requestId);
	bool skip();
	void stop();
	std::size_t clearQueue();
	bool setPaused(bool paused);
	bool seek(std::uint32_t positionMs);
	// What listeners hear now (plus the live queue).
	StationStatus status() const;

	// Network thread.  Appends the frames that are due at `now` to output.
	void produce(Clock::time_point now, std::string& output);
	std::string burst() const;
	std::string streamTitle() const;
};
