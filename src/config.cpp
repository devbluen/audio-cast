/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "config.hpp"
#include <algorithm>
#include <cctype>

namespace
{
const char* const kKeys[] = {
	"bind", "port", "public_host", "public_port", "burst_ms", "bitrate", "normalize", "allow_public", "utf8",
	"ytdlp", "cookies", "data_dir", "cache_minutes", "max_duration", "download_timeout", "workers", "max_listeners",
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
		const std::string value = get(name);
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
	readString("ytdlp", config.ytdlpPath);
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
