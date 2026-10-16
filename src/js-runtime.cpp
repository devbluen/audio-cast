/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "js-runtime.hpp"
#include "log.hpp"
#include "process.hpp"
#include "text.hpp"
#include "ytdlp.hpp"
#include <fstream>
#include <iterator>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/utsname.h>
#endif

namespace fs = std::filesystem;

namespace
{
constexpr const char* RELEASE_URL = "https://github.com/denoland/deno/releases/latest/download/";
constexpr auto UPDATE_INTERVAL = std::chrono::hours(24);
constexpr auto RETRY_INTERVAL = std::chrono::minutes(5);
// Directory layout, like yt-dlp's: current/ (the installed build),
// installed.txt (its release checksum), checked.txt (last update check).
constexpr const char* CURRENT_DIR = "current";
constexpr const char* INSTALLED_FILE = "installed.txt";
constexpr const char* CHECKED_FILE = "checked.txt";

#if defined(_WIN32)
constexpr const char* PROGRAM = "deno.exe";
#else
constexpr const char* PROGRAM = "deno";
#endif

// Official build for the machine (the server process itself is 32-bit even
// on 64-bit systems; Deno only exists for 64-bit ones).  Empty when there is
// none.
std::string assetName()
{
#if defined(_WIN32)
	SYSTEM_INFO info {};
	GetNativeSystemInfo(&info);
	switch (info.wProcessorArchitecture)
	{
	case PROCESSOR_ARCHITECTURE_AMD64: return "deno-x86_64-pc-windows-msvc.zip";
	case 12 /* PROCESSOR_ARCHITECTURE_ARM64 */: return "deno-aarch64-pc-windows-msvc.zip";
	default: return {};
	}
#else
	std::error_code ignored;
	for (const auto& entry : fs::directory_iterator("/lib", ignored))
	{
		// Deno's Linux builds need glibc.
		if (entry.path().filename().string().rfind("ld-musl-", 0) == 0) return {};
	}
	utsname name {};
	const std::string machine = uname(&name) == 0 ? name.machine : "";
	if (machine == "x86_64") return "deno-x86_64-unknown-linux-gnu.zip";
	if (machine == "aarch64" || machine == "arm64") return "deno-aarch64-unknown-linux-gnu.zip";
	return {};
#endif
}

std::string readText(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file << text;
}

std::string firstLine(const std::string& text)
{
	std::istringstream stream(text);
	std::string line;
	std::getline(stream, line);
	return Text::trim(line);
}
}

void JsRuntime::configure(const std::string& setting, const fs::path& directory)
{
	std::unique_lock<std::shared_mutex> lock(mutex_);
	setting_ = setting.empty() ? "auto" : setting;
	directory_ = directory;
	managed_ = setting_ == "auto";
	executable_.clear();
	ready_ = !managed_; // "off" and the user's own runtime need nothing
	unavailable_ = false;
	lastAttempt_ = {};
	lastUpdateCheck_ = {};
}

std::string JsRuntime::argument() const
{
	if (setting_ == "off") return {};
	if (!managed_) return setting_;
	return ready_ && !executable_.empty() ? "deno:" + executable_.u8string() : std::string();
}

bool JsRuntime::works(const std::atomic<bool>& cancel, std::string& version) const
{
	if (executable_.empty()) return false;
	const ProcessResult result = RunProcess({ executable_.u8string(), "--version" }, 60, cancel);
	version = firstLine(result.output);
	return result.started && result.exitCode == 0 && version.rfind("deno ", 0) == 0;
}

bool JsRuntime::latestChecksum(const std::atomic<bool>& cancel, std::string& checksum, std::string& error)
{
	const fs::path file = directory_ / "checksum.part";
	if (!DownloadFile(std::string(RELEASE_URL) + assetName() + ".sha256sum", file, cancel, error)) return false;
	// The format differs between platforms; only changes matter.
	checksum = Text::trim(readText(file));
	std::error_code ignored;
	fs::remove(file, ignored);
	if (checksum.empty())
	{
		error = "empty checksum file";
		return false;
	}
	return true;
}

bool JsRuntime::install(const std::string& checksum, const std::atomic<bool>& cancel, std::string& error)
{
	std::error_code ignored;
	fs::create_directories(directory_, ignored);
	const std::string asset = assetName();
	const fs::path download = directory_ / (asset + ".part");
	const fs::path fresh = directory_ / "current.new";
	const fs::path current = directory_ / CURRENT_DIR;
	const fs::path old = directory_ / "current.old";

	AudioCastLogMessage("downloading Deno, the JavaScript runtime yt-dlp needs for YouTube (" + asset + ", about 45 MB)...");
	if (!DownloadFile(std::string(RELEASE_URL) + asset, download, cancel, error))
	{
		fs::remove(download, ignored);
		return false;
	}

	fs::remove_all(fresh, ignored);
	fs::create_directories(fresh, ignored);
	const bool unpacked = ExtractZip(download, fresh, error);
	fs::remove(download, ignored);
	if (!unpacked || !fs::is_regular_file(fresh / PROGRAM, ignored))
	{
		fs::remove_all(fresh, ignored);
		if (error.empty()) error = "the Deno download has no program in it";
		return false;
	}
#if !defined(_WIN32)
	chmod((fresh / PROGRAM).c_str(), 0755);
#endif

	// Swap the new build in.
	fs::remove_all(old, ignored);
	if (fs::exists(current, ignored)) fs::rename(current, old, ignored);
	std::error_code renamed;
	fs::rename(fresh, current, renamed);
	if (renamed)
	{
		if (fs::exists(old, ignored)) fs::rename(old, current, ignored);
		fs::remove_all(fresh, ignored);
		error = "could not install Deno: " + renamed.message();
		return false;
	}
	fs::remove_all(old, ignored);

	executable_ = current / PROGRAM;
	writeText(directory_ / INSTALLED_FILE, checksum.empty() ? asset : checksum);
	writeText(directory_ / CHECKED_FILE, "\n");
	return true;
}

void JsRuntime::ensure(const std::atomic<bool>& cancel)
{
	{
		std::shared_lock<std::shared_mutex> lock(mutex_);
		if (ready_ || unavailable_) return;
	}

	std::unique_lock<std::shared_mutex> lock(mutex_);
	if (ready_ || unavailable_) return;

	if (assetName().empty())
	{
		unavailable_ = true;
		AudioCastLogWarning("Deno has no build for this machine, so yt-dlp runs without a JavaScript runtime and some YouTube "
			"videos may fail. Install Node.js and set audiocast_js_runtime to node.");
		return;
	}

	executable_ = directory_ / CURRENT_DIR / PROGRAM;
	std::string version;
	if (works(cancel, version))
	{
		ready_ = true;
		AudioCastLogMessage("using " + version + " (managed) for YouTube's JavaScript challenges");
		return;
	}
	executable_.clear();

	// Do not hammer GitHub when it is unreachable.
	const auto now = std::chrono::steady_clock::now();
	if (lastAttempt_ != std::chrono::steady_clock::time_point {} && now - lastAttempt_ < RETRY_INTERVAL) return;
	lastAttempt_ = now;

	std::string checksum;
	std::string error;
	latestChecksum(cancel, checksum, error);
	error.clear();
	if (!install(checksum, cancel, error) || !works(cancel, version))
	{
		if (error.empty()) error = "the downloaded Deno does not run on this machine";
		executable_.clear();
		if (!cancel.load())
		{
			AudioCastLogWarning(error + ". Songs load without a JavaScript runtime for now (some YouTube videos may fail); "
				"retrying in a few minutes.");
		}
		return;
	}
	ready_ = true;
	lastUpdateCheck_ = now;
	AudioCastLogMessage("installed " + version);
}

void JsRuntime::updateIfDue(const std::atomic<bool>& cancel)
{
	{
		std::shared_lock<std::shared_mutex> lock(mutex_);
		if (!managed_ || !ready_) return;
	}

	// checked.txt keeps the schedule across server restarts.
	const auto now = std::chrono::steady_clock::now();
	if (lastUpdateCheck_ != std::chrono::steady_clock::time_point {} && now - lastUpdateCheck_ < UPDATE_INTERVAL) return;
	lastUpdateCheck_ = now;
	std::error_code error;
	const auto checked = fs::last_write_time(directory_ / CHECKED_FILE, error);
	if (!error && fs::file_time_type::clock::now() - checked < UPDATE_INTERVAL)
	{
		lastUpdateCheck_ = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(fs::file_time_type::clock::now() - checked);
		return;
	}

	std::string checksum;
	std::string problem;
	if (!latestChecksum(cancel, checksum, problem))
	{
		if (!cancel.load()) AudioCastLogWarning("Deno update check failed: " + problem);
		return;
	}
	writeText(directory_ / CHECKED_FILE, "\n");
	if (Text::trim(readText(directory_ / INSTALLED_FILE)) == checksum) return;

	// Wait for running downloads, then replace the build.
	std::unique_lock<std::shared_mutex> lock(mutex_);
	if (!install(checksum, cancel, problem))
	{
		if (!cancel.load()) AudioCastLogWarning("Deno update failed: " + problem);
		executable_ = directory_ / CURRENT_DIR / PROGRAM;
		return;
	}
	std::string version;
	works(cancel, version);
	AudioCastLogMessage("updated Deno to " + version);
}
