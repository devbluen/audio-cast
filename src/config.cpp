/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "config.hpp"
#include "text.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace
{
const char* const kKeys[] = {
	"bind", "port", "public_host", "public_port", "burst_ms", "bitrate", "normalize", "allow_public", "utf8", "metadata_codepage", "log_songs",
	"ytdlp", "js_runtime", "cookies", "data_dir", "cache_minutes", "max_duration", "download_timeout", "workers", "max_listeners",
	nullptr,
};

bool parseInt(const std::string& text, int& value)
{
	try
	{
		std::size_t used = 0;
		const int parsed = std::stoi(text, &used);
		if (used != text.size()) return false;
		value = parsed;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool parseBool(std::string text, bool fallback)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (text == "1" || text == "true" || text == "yes" || text == "on") return true;
	if (text == "0" || text == "false" || text == "no" || text == "off") return false;
	return fallback;
}
}

AudioCastConfig LoadAudioCastConfig(const std::function<std::string(const std::string&)>& lookup)
{
	AudioCastConfig config;
	auto get = [&lookup](const char* name) { return lookup(std::string("audiocast_") + name); };
	auto readString = [&get](const char* name, std::string& target)
	{
		std::string value = get(name);
		// "C:\servers\my server\cookies.txt" works with or without quotes.
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
		if (!value.empty()) target = value;
	};
	auto readInt = [&get](const char* name, int& target, int minimum, int maximum)
	{
		int value = 0;
		if (parseInt(get(name), value)) target = std::clamp(value, minimum, maximum);
	};
	auto readBool = [&get](const char* name, bool& target)
	{
		const std::string value = get(name);
		if (!value.empty()) target = parseBool(value, target);
	};

	readString("bind", config.bindAddress);
	readInt("port", config.port, 1, 65535);
	readString("public_host", config.publicHost);
	readInt("public_port", config.publicPort, 0, 65535);
	readInt("burst_ms", config.burstMs, 0, 30000);
	readInt("bitrate", config.bitrateKbps, 32, 320);
	readBool("normalize", config.normalize);
	readBool("allow_public", config.allowPublic);
	readBool("utf8", config.utf8);
	int codepage = config.metadataCodepage;
	readInt("metadata_codepage", codepage, 0, 65535);
	if (Text::isSupportedCodepage(codepage)) config.metadataCodepage = codepage;
	readBool("log_songs", config.logSongs);
	readString("ytdlp", config.ytdlpPath);
	readString("js_runtime", config.jsRuntime);
	readString("cookies", config.cookiesFile);
	readString("cache_dir", config.dataDir); // name used by version 1.0
	readString("data_dir", config.dataDir);
	readInt("cache_minutes", config.cacheMinutes, 0, 24 * 60);
	readInt("max_duration", config.maxDurationSec, 10, 6 * 3600);
	readInt("download_timeout", config.downloadTimeoutSec, 10, 3600);
	readInt("workers", config.workers, 1, 8);
	readInt("max_listeners", config.maxListeners, 1, 10000);

	// libmp3lame only produces these MPEG-1 Layer III bitrates.
	static const int kBitrates[] = { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
	int best = 128;
	for (int bitrate : kBitrates)
	{
		if (bitrate <= config.bitrateKbps) best = bitrate;
	}
	config.bitrateKbps = best;
	return config;
}

const char* const* AudioCastConfigKeys()
{
	return kKeys;
}

CookiesStatus CheckCookiesFile(const std::string& path, std::string& message)
{
	namespace fs = std::filesystem;
	std::error_code ignored;
	const fs::path file = fs::absolute(fs::u8path(path), ignored);
	const std::string shown = "audiocast_cookies: " + file.u8string();
	if (!fs::is_regular_file(file, ignored))
	{
		message = shown + " was not found, so songs load without cookies. A relative path starts at the server folder (" +
			fs::current_path(ignored).u8string() + ").";
		return CookiesStatus::Missing;
	}

	std::ifstream input(file, std::ios::binary);
	std::string line;
	bool header = false;
	bool youtube = false;
	bool signedIn = false;
	while (std::getline(input, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.rfind("# Netscape HTTP Cookie File", 0) == 0 || line.rfind("# HTTP Cookie File", 0) == 0) header = true;
		if (line.find("youtube.com\t") == std::string::npos) continue;
		youtube = true;
		// domain, subdomains, path, secure, expiry, name, value
		std::size_t start = 0;
		for (int field = 0; field < 5 && start != std::string::npos; ++field)
		{
			start = line.find('\t', start);
			if (start != std::string::npos) ++start;
		}
		if (start == std::string::npos) continue;
		const std::string name = line.substr(start, line.find('\t', start) - start);
		if (name == "LOGIN_INFO" || name == "SAPISID" || name == "__Secure-1PSID" || name == "__Secure-3PSID") signedIn = true;
	}

	if (!header)
	{
		message = shown + " is not a cookies.txt file (Netscape format), so it is not used. Export it with a \"cookies.txt\" "
			"browser extension; JSON exports and cookies copied by hand do not work.";
		return CookiesStatus::Invalid;
	}
	if (!youtube || !signedIn)
	{
		message = shown + (youtube ? " has YouTube cookies but no sign-in" : " has no youtube.com cookies") +
			". Sign in on youtube.com in the browser and export the cookies again from that tab.";
		return CookiesStatus::NoSignIn;
	}
	message = "using the YouTube sign-in from " + file.u8string();
	return CookiesStatus::Ok;
}
