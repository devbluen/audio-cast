/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "ytdlp.hpp"
#include "log.hpp"
#include "process.hpp"
#include "text.hpp"
#include <miniz.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/stat.h>
#include <sys/utsname.h>
#endif

namespace fs = std::filesystem;

namespace
{
constexpr const char* RELEASE_URL = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/";
constexpr auto UPDATE_INTERVAL = std::chrono::hours(24);
constexpr auto RETRY_INTERVAL = std::chrono::minutes(5);
// binDir layout: current/ (the installed build), installed.txt (its release
// checksum) and checked.txt (time of the last update check).
constexpr const char* CURRENT_DIR = "current";
constexpr const char* INSTALLED_FILE = "installed.txt";
constexpr const char* CHECKED_FILE = "checked.txt";

// Official build for the machine (not the server process, which is 32-bit
// even on 64-bit systems).  Zips are the unpacked builds, which start fast.
std::string assetName()
{
#if defined(_WIN32)
	SYSTEM_INFO info {};
	GetNativeSystemInfo(&info);
	switch (info.wProcessorArchitecture)
	{
	case PROCESSOR_ARCHITECTURE_AMD64: return "yt-dlp_win.zip";
	case 12 /* PROCESSOR_ARCHITECTURE_ARM64 */: return "yt-dlp_win_arm64.zip";
	default: return "yt-dlp_win_x86.zip";
	}
#else
	utsname name {};
	const std::string machine = uname(&name) == 0 ? name.machine : "";
	bool musl = false;
	std::error_code ignored;
	for (const auto& entry : fs::directory_iterator("/lib", ignored))
	{
		if (entry.path().filename().string().rfind("ld-musl-", 0) == 0) musl = true;
	}
	if (machine == "x86_64") return musl ? "yt-dlp_musllinux.zip" : "yt-dlp_linux.zip";
	if (machine == "aarch64" || machine == "arm64") return musl ? "yt-dlp_musllinux_aarch64.zip" : "yt-dlp_linux_aarch64.zip";
	if (machine.rfind("armv7", 0) == 0) return "yt-dlp_linux_armv7l.zip";
	// Other CPUs: the Python zipapp, which needs python3 on the machine.
	return "yt-dlp";
#endif
}

std::string firstLine(const std::string& text)
{
	std::istringstream stream(text);
	std::string line;
	std::getline(stream, line);
	return Text::trim(line);
}

std::string lastLine(const std::string& text)
{
	std::istringstream stream(text);
	std::string line;
	std::string last;
	while (std::getline(stream, line))
	{
		line = Text::trim(line);
		if (!line.empty()) last = line;
	}
	return last;
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

// The yt-dlp program inside an unpacked build ("yt-dlp.exe", "yt-dlp_linux").
fs::path findExecutable(const fs::path& directory)
{
	std::error_code ignored;
	for (const auto& entry : fs::directory_iterator(directory, ignored))
	{
		const std::string name = entry.path().filename().u8string();
		if (entry.is_regular_file(ignored) && name.rfind("yt-dlp", 0) == 0) return entry.path();
	}
	return {};
}

void makeExecutable(const fs::path& path)
{
#if defined(_WIN32)
	(void)path;
#else
	chmod(path.c_str(), 0755);
#endif
}

#if defined(_WIN32)
std::wstring widen(const std::string& text)
{
	if (text.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
	return wide;
}

struct InternetHandle
{
	HINTERNET handle = nullptr;
	explicit InternetHandle(HINTERNET value) : handle(value) { }
	~InternetHandle() { if (handle) WinHttpCloseHandle(handle); }
	InternetHandle(const InternetHandle&) = delete;
	InternetHandle& operator=(const InternetHandle&) = delete;
};
#endif
}

bool DownloadFile(const std::string& url, const fs::path& target, const std::atomic<bool>& cancel, std::string& error)
{
#if defined(_WIN32)
	const std::wstring wideUrl = widen(url);
	URL_COMPONENTS parts {};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256] = {};
	wchar_t path[2048] = {};
	parts.lpszHostName = host;
	parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
	if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
	{
		error = "invalid URL " + url;
		return false;
	}

	InternetHandle session(WinHttpOpen(L"AudioCast", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session.handle)
	{
		error = "WinHttpOpen failed (" + std::to_string(GetLastError()) + ")";
		return false;
	}
	WinHttpSetTimeouts(session.handle, 15000, 15000, 30000, 60000);
	InternetHandle connection(WinHttpConnect(session.handle, host, parts.nPort, 0));
	InternetHandle request(connection.handle
		? WinHttpOpenRequest(connection.handle, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
		: nullptr);
	if (!request.handle || !WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(request.handle, nullptr))
	{
		error = "could not download " + url + " (WinHTTP error " + std::to_string(GetLastError()) + ")";
		return false;
	}

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
		&statusSize, WINHTTP_NO_HEADER_INDEX);
	if (status != 200)
	{
		error = "could not download " + url + " (HTTP " + std::to_string(status) + ")";
		return false;
	}

	std::ofstream file(target, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		error = "could not write " + target.u8string();
		return false;
	}
	std::vector<char> buffer(1 << 16);
	for (;;)
	{
		if (cancel.load())
		{
			error = "cancelled";
			return false;
		}
		DWORD read = 0;
		if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
		{
			error = "download of " + url + " was interrupted";
			return false;
		}
		if (read == 0) break;
		file.write(buffer.data(), read);
	}
	file.close();
	return static_cast<bool>(file);
#else
	const std::string output = target.u8string();
	ProcessResult result = RunProcess({ "curl", "-fsSL", "--retry", "2", "--connect-timeout", "15", "-o", output, url }, 300, cancel);
	if (!result.started)
	{
		result = RunProcess({ "wget", "-q", "-T", "30", "-O", output, url }, 300, cancel);
	}
	if (!result.started)
	{
		error = "neither curl nor wget is installed, so " + url + " cannot be downloaded";
		return false;
	}
	if (result.exitCode != 0 || result.timedOut || result.cancelled)
	{
		error = "could not download " + url + ": " + (result.output.empty() ? "exit code " + std::to_string(result.exitCode) : lastLine(result.output));
		return false;
	}
	return true;
#endif
}

bool ExtractZip(const fs::path& zipPath, const fs::path& target, std::string& error)
{
	const std::string bytes = readText(zipPath);
	mz_zip_archive zip {};
	if (bytes.empty() || !mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
	{
		error = "could not open " + zipPath.u8string();
		return false;
	}

	bool ok = true;
	const mz_uint count = mz_zip_reader_get_num_files(&zip);
	for (mz_uint index = 0; index < count && ok; ++index)
	{
		mz_zip_archive_file_stat stat {};
		if (!mz_zip_reader_file_stat(&zip, index, &stat))
		{
			ok = false;
			break;
		}
		const std::string name = stat.m_filename;
		// Never write outside the target directory.
		const fs::path relative = fs::u8path(name).lexically_normal();
		if (name.empty() || relative.is_absolute() || relative.has_root_name() || *relative.begin() == "..")
		{
			error = "unsafe path in zip: " + name;
			ok = false;
			break;
		}

		const fs::path destination = target / relative;
		std::error_code created;
		if (mz_zip_reader_is_file_a_directory(&zip, index))
		{
			fs::create_directories(destination, created);
			continue;
		}
		fs::create_directories(destination.parent_path(), created);

		std::size_t size = 0;
		void* data = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);
		if (!data)
		{
			error = "could not unpack " + name;
			ok = false;
			break;
		}
		{
			std::ofstream file(destination, std::ios::binary | std::ios::trunc);
			file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			ok = static_cast<bool>(file);
		}
		mz_free(data);
		if (!ok)
		{
			error = "could not write " + destination.u8string();
			break;
		}
#if !defined(_WIN32)
		// Unix permissions live in the upper half of the external attributes.
		const mz_uint32 mode = (stat.m_external_attr >> 16) & 0777;
		if (mode != 0) chmod(destination.c_str(), mode);
#endif
	}
	mz_zip_reader_end(&zip);
	return ok;
}

void YtDlp::configure(const std::string& path, const fs::path& binDir)
{
	std::unique_lock<std::shared_mutex> lock(mutex_);
	configuredPath_ = path;
	binDir_ = binDir;
	managed_ = path.empty() || path == "auto";
	executable_.clear();
	ready_ = false;
	lastAttempt_ = {};
	lastUpdateCheck_ = {};
}

std::string YtDlp::path() const
{
	return managed_ ? executable_.u8string() : configuredPath_;
}

bool YtDlp::works(const std::atomic<bool>& cancel, std::string& version) const
{
	if (managed_ && executable_.empty()) return false;
	const ProcessResult result = RunProcess({ path(), "--version" }, 120, cancel);
	version = firstLine(result.output);
	return result.started && result.exitCode == 0 && !version.empty();
}

bool YtDlp::latestChecksum(const std::atomic<bool>& cancel, std::string& checksum, std::string& error)
{
	const fs::path sums = binDir_ / "SHA2-256SUMS.part";
	if (!DownloadFile(std::string(RELEASE_URL) + "SHA2-256SUMS", sums, cancel, error)) return false;
	const std::string text = readText(sums);
	std::error_code ignored;
	fs::remove(sums, ignored);

	const std::string asset = assetName();
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line))
	{
		line = Text::trim(line);
		const std::size_t space = line.find(' ');
		if (space != std::string::npos && Text::trim(line.substr(space)) == asset)
		{
			checksum = line.substr(0, space);
			return true;
		}
	}
	error = "the yt-dlp release has no " + asset;
	return false;
}

bool YtDlp::install(const std::string& checksum, const std::atomic<bool>& cancel, std::string& error)
{
	std::error_code ignored;
	fs::create_directories(binDir_, ignored);
	const std::string asset = assetName();
	const fs::path download = binDir_ / (asset + ".part");
	const fs::path fresh = binDir_ / "current.new";
	const fs::path current = binDir_ / CURRENT_DIR;
	const fs::path old = binDir_ / "current.old";

	AudioCastLogMessage("downloading yt-dlp (" + asset + ")...");
	if (!DownloadFile(std::string(RELEASE_URL) + asset, download, cancel, error))
	{
		fs::remove(download, ignored);
		return false;
	}

	fs::remove_all(fresh, ignored);
	fs::create_directories(fresh, ignored);
	const bool zipped = asset.size() > 4 && asset.compare(asset.size() - 4, 4, ".zip") == 0;
	if (zipped)
	{
		const bool unpacked = ExtractZip(download, fresh, error);
		fs::remove(download, ignored);
		if (!unpacked)
		{
			fs::remove_all(fresh, ignored);
			return false;
		}
	}
	else
	{
		fs::rename(download, fresh / asset, ignored);
	}
	const fs::path program = findExecutable(fresh);
	if (program.empty())
	{
		fs::remove_all(fresh, ignored);
		error = "the yt-dlp download has no program in it";
		return false;
	}
	makeExecutable(program);

	// Swap the new build in.
	fs::remove_all(old, ignored);
	if (fs::exists(current, ignored)) fs::rename(current, old, ignored);
	std::error_code renamed;
	fs::rename(fresh, current, renamed);
	if (renamed)
	{
		if (fs::exists(old, ignored)) fs::rename(old, current, ignored);
		fs::remove_all(fresh, ignored);
		error = "could not install yt-dlp: " + renamed.message();
		return false;
	}
	fs::remove_all(old, ignored);

	// Files of the single-file builds used by older versions of AudioCast.
	for (const char* legacy : { "yt-dlp.exe", "yt-dlp", ".updated" })
	{
		if (fs::is_regular_file(binDir_ / legacy, ignored)) fs::remove(binDir_ / legacy, ignored);
	}

	executable_ = findExecutable(current);
	writeText(binDir_ / INSTALLED_FILE, checksum.empty() ? asset : checksum);
	writeText(binDir_ / CHECKED_FILE, "\n");
	return true;
}

bool YtDlp::ensure(const std::atomic<bool>& cancel, std::string& error)
{
	{
		std::shared_lock<std::shared_mutex> lock(mutex_);
		if (ready_) return true;
	}

	std::unique_lock<std::shared_mutex> lock(mutex_);
	if (ready_) return true;

	if (managed_) executable_ = findExecutable(binDir_ / CURRENT_DIR);
	std::string version;
	if (works(cancel, version))
	{
		ready_ = true;
		AudioCastLogMessage("using yt-dlp " + version + (managed_ ? " (managed)" : " from " + configuredPath_));
		return true;
	}
	if (!managed_)
	{
		error = "yt-dlp not found at '" + configuredPath_ + "'. Set audiocast_ytdlp to auto to let AudioCast download it.";
		return false;
	}

	// Do not hammer GitHub when it is unreachable: retry every few minutes.
	const auto now = std::chrono::steady_clock::now();
	if (lastAttempt_ != std::chrono::steady_clock::time_point {} && now - lastAttempt_ < RETRY_INTERVAL)
	{
		error = "yt-dlp is not available yet (download failed, retrying soon)";
		return false;
	}
	lastAttempt_ = now;

	std::string checksum;
	std::string ignoredError;
	latestChecksum(cancel, checksum, ignoredError);
	if (!install(checksum, cancel, error)) return false;
	if (!works(cancel, version))
	{
		error = "the downloaded yt-dlp does not run on this machine";
		return false;
	}
	ready_ = true;
	lastUpdateCheck_ = now;
	AudioCastLogMessage("installed yt-dlp " + version);
	return true;
}

void YtDlp::updateIfDue(const std::atomic<bool>& cancel)
{
	if (!managed_) return;
	{
		std::shared_lock<std::shared_mutex> lock(mutex_);
		if (!ready_) return;
	}

	// checked.txt keeps the schedule across server restarts.
	const auto now = std::chrono::steady_clock::now();
	if (lastUpdateCheck_ != std::chrono::steady_clock::time_point {} && now - lastUpdateCheck_ < UPDATE_INTERVAL) return;
	lastUpdateCheck_ = now;
	std::error_code error;
	const auto checked = fs::last_write_time(binDir_ / CHECKED_FILE, error);
	if (!error && fs::file_time_type::clock::now() - checked < UPDATE_INTERVAL)
	{
		lastUpdateCheck_ = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(fs::file_time_type::clock::now() - checked);
		return;
	}

	std::string checksum;
	std::string problem;
	if (!latestChecksum(cancel, checksum, problem))
	{
		if (!cancel.load()) AudioCastLogWarning("yt-dlp update check failed: " + problem);
		return;
	}
	writeText(binDir_ / CHECKED_FILE, "\n");
	if (Text::trim(readText(binDir_ / INSTALLED_FILE)) == checksum) return;

	// Wait for running downloads, then replace the build.
	std::unique_lock<std::shared_mutex> lock(mutex_);
	if (!install(checksum, cancel, problem))
	{
		if (!cancel.load()) AudioCastLogWarning("yt-dlp update failed: " + problem);
		executable_ = findExecutable(binDir_ / CURRENT_DIR);
		return;
	}
	std::string version;
	works(cancel, version);
	AudioCastLogMessage("updated yt-dlp to " + version);
}
