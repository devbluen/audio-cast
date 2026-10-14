/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 *
 *  Loads one song exactly like the plugin does, to check a server's setup:
 *
 *    audio-cast-check "<link or search>" [yt-dlp path or auto] [data dir] [port seconds]
 *    audio-cast-check --search "<terms>" [yt-dlp path or auto] [data dir]
 *
 *  With a port it then streams the song like an in-game radio for the given
 *  number of seconds, at http://127.0.0.1:<port>/radio/1.mp3 (VLC, browser).
 */

#include "loader.hpp"
#include "log.hpp"
#include "station.hpp"
#include "stream-server.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
	return std::chrono::duration<double>(Clock::now() - start).count();
}

int serve(AudioCastConfig config, std::shared_ptr<const Track> track, int port, int seconds)
{
	EventQueue events;
	StreamServer server(events);
	config.bindAddress = "127.0.0.1";
	config.port = port;
	config.allowPublic = true;
	std::string error;
	if (!server.start(config, error))
	{
		std::printf("FAILED: %s\n", error.c_str());
		return EXIT_FAILURE;
	}
	auto station = std::make_shared<Station>(1, "AudioCast check", events, config.bitrateKbps, config.burstMs);
	server.addStation(station);
	station->enqueue(1, track->videoId, false);
	station->trackLoaded(1, track);
	std::printf("streaming at http://127.0.0.1:%d/radio/1.mp3 for %d s\n", server.boundPort(), seconds);
	std::fflush(stdout);
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
	server.stop();
	return EXIT_SUCCESS;
}
}

int main(int argc, char** argv)
{
	const bool search = argc > 1 && std::strcmp(argv[1], "--search") == 0;
	const int first = search ? 2 : 1;
	if (argc <= first)
	{
		std::fprintf(stderr,
			"usage: %s \"<link or search>\" [yt-dlp path or auto] [data dir] [port seconds]\n"
			"       %s --search \"<terms>\" [yt-dlp path or auto] [data dir]\n",
			argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	AudioCastLogInit(nullptr);
	AudioCastConfig config;
	if (argc > first + 1) config.ytdlpPath = argv[first + 1];
	if (argc > first + 2) config.dataDir = argv[first + 2];
	config.workers = 1;

	TrackLoader loader;
	loader.start(config);
	LoadRequest request;
	request.kind = search ? LoadRequest::Kind::Search : LoadRequest::Kind::Track;
	request.radioId = 1;
	request.requestId = 1;
	request.query = argv[first];
	request.maxResults = 8;
	loader.submit(request);

	const auto started = Clock::now();
	std::shared_ptr<const Track> track;
	for (;;)
	{
		AudioCastLogFlush();
		for (LoadResult& result : loader.drain())
		{
			AudioCastLogFlush();
			if (!result.error.empty())
			{
				std::printf("FAILED after %.1f s: %s\n", secondsSince(started), result.error.c_str());
				return EXIT_FAILURE;
			}
			if (search)
			{
				std::printf("%zu results after %.1f s:\n", result.entries.size(), secondsSince(started));
				for (const SearchEntry& entry : result.entries)
				{
					std::printf("  %s  %2u:%02u  %s  [%s]\n", entry.videoId.c_str(), entry.durationMs / 60000, entry.durationMs / 1000 % 60,
						entry.title.c_str(), entry.channel.c_str());
				}
				return EXIT_SUCCESS;
			}
			track = result.track;
			std::printf("ready to play after %.1f s: %s (%s)\n", secondsSince(started), track->title.c_str(), track->videoId.c_str());
			std::fflush(stdout);
		}
		if (track && track->complete()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	AudioCastLogFlush();
	std::printf("%s after %.1f s\n  duration: %u ms\n  frames:   %zu\n  bytes:    %zu\n", track->failed() ? "BROKE OFF" : "fully loaded",
		secondsSince(started), track->durationMs(), track->frameCount(), track->byteCount());
	std::fflush(stdout);
	loader.stop();
	if (argc > first + 4) return serve(config, track, std::atoi(argv[first + 3]), std::atoi(argv[first + 4]));
	return track->failed() ? EXIT_FAILURE : EXIT_SUCCESS;
}
