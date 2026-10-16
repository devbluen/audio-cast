/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <string>

// The JavaScript runtime yt-dlp needs for YouTube: YouTube hides its audio
// URLs behind JavaScript challenges, and without a runtime to solve them many
// videos fail (often with "The page needs to be reloaded", especially when
// cookies are used).  The official yt-dlp builds include the solver scripts;
// they only need a runtime, and Deno is the one yt-dlp recommends.
//
// In "auto" mode the official Deno build for this OS/CPU is downloaded into
// the data directory on first use and updated like yt-dlp (checked once a
// day).  The setting can also be "off" or any --js-runtimes value
// ("node", "deno:/opt/deno", "quickjs:/usr/bin/qjs"...), passed as is.
class JsRuntime
{
private:
	std::string setting_;
	std::filesystem::path directory_;
	std::filesystem::path executable_;
	bool managed_ = false;
	bool ready_ = false;
	bool unavailable_ = false; // no Deno build for this machine
	std::chrono::steady_clock::time_point lastAttempt_ {};
	std::chrono::steady_clock::time_point lastUpdateCheck_ {};
	// Shared while yt-dlp may run Deno, exclusive while it is replaced.
	mutable std::shared_mutex mutex_;

	bool install(const std::string& checksum, const std::atomic<bool>& cancel, std::string& error);
	bool latestChecksum(const std::atomic<bool>& cancel, std::string& checksum, std::string& error);
	bool works(const std::atomic<bool>& cancel, std::string& version) const;

public:
	// setting: "auto", "off", or a yt-dlp --js-runtimes value.
	void configure(const std::string& setting, const std::filesystem::path& directory);

	// Installs Deno in auto mode when needed.  Thread-safe.  A failure is
	// logged and songs are fetched without it.
	void ensure(const std::atomic<bool>& cancel);
	// Installs a newer Deno when there is one (auto mode, once a day).
	void updateIfDue(const std::atomic<bool>& cancel);

	// The --js-runtimes value for yt-dlp, or empty to leave yt-dlp's default.
	// Call with mutex() held (shared) for as long as yt-dlp runs.
	std::string argument() const;
	std::shared_mutex& mutex() const { return mutex_; }
};
