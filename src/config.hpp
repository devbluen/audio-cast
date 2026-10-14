/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct AudioCastConfig
{
	// HTTP stream server.  Players' game clients connect here, so the port
	// must be reachable over TCP from the internet.
	std::string bindAddress = "0.0.0.0";
	int port = 8000;
	// Host (and port) written into the URLs sent to the clients.  Defaults to
	// the server's bind/public address and the stream port.
	std::string publicHost;
	int publicPort = 0;
	// Every listener first receives this much recent audio.  All clients get
	// the same amount, so they play the same moment of the song.
	int burstMs = 4000;
	// Encoding of every track.  All tracks share one format so they can be
	// concatenated into a single endless stream.
	int bitrateKbps = 128;
	bool normalize = false;
	// When enabled the stream can also be opened without a player token
	// (/radio/<id>), for example in VLC or a browser.
	bool allowPublic = false;
	// Strings exchanged with Pawn are Windows-1252 unless this is enabled.
	bool utf8 = false;

	// "auto": AudioCast downloads and updates yt-dlp by itself.
	std::string ytdlpPath = "auto";
	std::string cookiesFile;
	// Holds the managed yt-dlp and short-lived temporary files.  Songs are
	// never written to disk.
	std::string dataDir = "scriptfiles/audiocast";
	// Minutes a song stays in memory after no radio uses it, so replaying it
	// is instant.  0 frees it immediately.
	int cacheMinutes = 0;
	int maxDurationSec = 1800;
	int downloadTimeoutSec = 300;
	int workers = 2;
	int maxListeners = 500;
};

// Reads every setting through lookup(key), which returns an empty string for
// missing keys.  Keys are "audiocast_<name>".
AudioCastConfig LoadAudioCastConfig(const std::function<std::string(const std::string&)>& lookup);

// Names (without the audiocast_ prefix) of every setting, for config files.
const char* const* AudioCastConfigKeys();
