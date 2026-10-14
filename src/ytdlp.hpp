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

// Provides the yt-dlp executable.  In "auto" mode the official build for this
// OS/CPU is downloaded into the data directory on first use and replaced when
// a new release comes out (checked once a day), so the server owner installs
// nothing.  The unpacked ("onedir") build is used because it starts about a
// second faster than the single-file one on every song.  YouTube changes
// often; keeping yt-dlp separate means it can be updated without a new
// plugin release.
class YtDlp
{
private:
	std::string configuredPath_;
	std::filesystem::path binDir_;
	std::filesystem::path executable_;
	bool managed_ = false;
	bool ready_ = false;
	std::chrono::steady_clock::time_point lastAttempt_ {};
	std::chrono::steady_clock::time_point lastUpdateCheck_ {};
	// Shared while yt-dlp runs, exclusive while it is installed or replaced.
	mutable std::shared_mutex mutex_;

	bool install(const std::string& checksum, const std::atomic<bool>& cancel, std::string& error);
	bool latestChecksum(const std::atomic<bool>& cancel, std::string& checksum, std::string& error);
	bool works(const std::atomic<bool>& cancel, std::string& version) const;

public:
	// path: "auto" (managed) or the path/name of an installed yt-dlp.
	void configure(const std::string& path, const std::filesystem::path& binDir);

	// Makes sure yt-dlp can run, installing it in auto mode.  Thread-safe.
	bool ensure(const std::atomic<bool>& cancel, std::string& error);
	// Installs a newer release when there is one (auto mode, once a day).
	void updateIfDue(const std::atomic<bool>& cancel);

	std::string path() const;
	bool managed() const { return managed_; }
	std::shared_mutex& mutex() const { return mutex_; }
};

// Downloads an HTTPS URL to a file (WinHTTP on Windows, curl/wget on Linux).
bool DownloadFile(const std::string& url, const std::filesystem::path& target, const std::atomic<bool>& cancel, std::string& error);

// Unpacks a zip file into a directory, keeping Unix permissions.
bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& target, std::string& error);
