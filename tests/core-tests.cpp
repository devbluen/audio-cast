/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "mp3.hpp"
#include "station.hpp"
#include "text.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			++failures; \
		} \
	} while (0)

std::vector<std::uint8_t> repeatFrames(const std::vector<std::uint8_t>& frame, std::size_t count)
{
	std::vector<std::uint8_t> data;
	for (std::size_t index = 0; index < count; ++index) data.insert(data.end(), frame.begin(), frame.end());
	return data;
}

std::shared_ptr<Track> makeTrack(const std::string& title, std::size_t frames, bool complete = true)
{
	auto track = std::make_shared<Track>();
	track->videoId = "dQw4w9WgXcQ";
	track->title = title;
	const std::vector<std::uint8_t> silence = Mp3::makeSilentFrame(128);
	for (std::size_t index = 0; index < frames; ++index) track->append(silence.data(), silence.size());
	if (complete) track->finish();
	return track;
}

void testMp3()
{
	const std::vector<std::uint8_t> silence = Mp3::makeSilentFrame(128);
	Mp3::FrameHeader header;
	CHECK(Mp3::parseHeader(silence.data(), silence.size(), header));
	CHECK(header.version == 1);
	CHECK(header.sampleRate == 48000);
	CHECK(header.bitrateKbps == 128);
	CHECK(header.size == 384);
	CHECK(silence.size() == 384);
	CHECK(Mp3::makeSilentFrame(192).size() == 576);

	// ID3 tag, junk, an Info header frame and 10 audio frames.
	std::vector<std::uint8_t> data = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 5, 1, 2, 3, 4, 5 };
	data.insert(data.end(), { 0xFF, 0x00, 0x12 });
	std::vector<std::uint8_t> info = silence;
	std::memcpy(info.data() + 36, "Info", 4);
	data.insert(data.end(), info.begin(), info.end());
	const auto audio = repeatFrames(silence, 10);
	const std::size_t audioStart = data.size();
	data.insert(data.end(), audio.begin(), audio.end());

	std::vector<Mp3::Frame> frames;
	std::string error;
	CHECK(Mp3::indexFrames(data, frames, error));
	CHECK(frames.size() == 10);
	CHECK(!frames.empty() && frames.front().offset == audioStart);

	std::vector<std::uint8_t> garbage(1000, 0x55);
	CHECK(!Mp3::indexFrames(garbage, frames, error));

	CHECK(Mp3::framesToMs(Mp3::msToFrames(60000)) <= 60000);
	CHECK(Mp3::msToFrames(1000) == 41);
}

void testQueries()
{
	using Kind = Text::Query::Kind;
	auto video = [](const std::string& input)
	{
		const Text::Query query = Text::parseQuery(input);
		return query.kind == Kind::Video ? query.value : std::string("<") + std::to_string(static_cast<int>(query.kind)) + ">";
	};

	CHECK(video("https://www.youtube.com/watch?v=dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(video("https://youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=42s") == "dQw4w9WgXcQ");
	CHECK(video("youtube.com/watch?v=dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(video("https://youtu.be/dQw4w9WgXcQ?si=abc") == "dQw4w9WgXcQ");
	CHECK(video("https://m.youtube.com/watch?v=dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(video("https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=RD") == "dQw4w9WgXcQ");
	CHECK(video("https://www.youtube.com/shorts/dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(video("https://www.youtube.com/embed/dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(video("https://www.youtube.com/live/dQw4w9WgXcQ?feature=x") == "dQw4w9WgXcQ");
	CHECK(video("  dQw4w9WgXcQ  ") == "dQw4w9WgXcQ");

	CHECK(Text::parseQuery("https://example.com/watch?v=dQw4w9WgXcQ").kind == Kind::Invalid);
	CHECK(Text::parseQuery("https://www.youtube.com/watch?v=short").kind == Kind::Invalid);
	CHECK(Text::parseQuery("file:///etc/passwd").kind == Kind::Invalid);
	CHECK(Text::parseQuery("   ").kind == Kind::Invalid);

	const Text::Query search = Text::parseQuery("Imagination");
	CHECK(search.kind == Kind::Search && search.value == "Imagination");
	const Text::Query terms = Text::parseQuery("--exec rm -rf / tiago iorc");
	CHECK(terms.kind == Kind::Search);
}

void testText()
{
	CHECK(Text::utf8ToLatin1("Canção \xE2\x80\x93 ação") == "Can\xE7\xE3o - a\xE7\xE3o");
	CHECK(Text::utf8ToLatin1("\xE6\x97\xA5") == "?");
	CHECK(Text::latin1ToUtf8("Can\xE7\xE3o") == "Canção");
	CHECK(Text::utf8ToLatin1(Text::latin1ToUtf8("\xC1\xE9\xED\xF3\xFA")) == "\xC1\xE9\xED\xF3\xFA");
	CHECK(Text::utf8ToCodepage("Spr\xC4\x99\xC5\xBCynka", 1250) == "Spr\xEA\xBF" "ynka");
	CHECK(Text::utf8ToCodepage("Spre\xCC\xA8z\xCC\x87ynka Le\xCC\x81s\xCC\x81ny", 1250) == "Spr\xEA\xBF" "ynka L\xE9\x9C" "ny");
	CHECK(Text::utf8ToCodepage("Spr\xC4\x99\xC5\xBCynka", 1252) == "Sprezynka");
	CHECK(Text::utf8ToCodepage("Can\xC3\xA7\xC3\xA3o \xE2\x80\x93", 1252) == "Can\xE7\xE3o \x96");
	CHECK(Text::utf8ToCodepage("\xE6\x97\xA5 a\xCC\x81", 1250) == "? \xE1");
	CHECK(Text::utf8ToCodepage("\xD0\x9F\xD1\x80\xD0\xB8", 1251) == "\xCF\xF0\xE8");
	CHECK(Text::utf8ToCodepage("Spr\xC4\x99\xC5\xBCynka \xC3\xA9 \xE2\x80\x93", 0) == "Sprezynka e -");
	CHECK(Text::isSupportedCodepage(0) && Text::isSupportedCodepage(1250) && Text::isSupportedCodepage(1258));
	CHECK(!Text::isSupportedCodepage(1249) && !Text::isSupportedCodepage(1259) && !Text::isSupportedCodepage(65001));
	const std::string token = Text::randomToken(24);
	CHECK(token.size() == 24);
	CHECK(token != Text::randomToken(24));
}

std::size_t countFrames(const std::string& audio)
{
	std::vector<std::uint8_t> bytes(audio.begin(), audio.end());
	std::vector<Mp3::Frame> frames;
	std::string error;
	Mp3::indexFrames(bytes, frames, error);
	return frames.size();
}

void testStation()
{
	EventQueue events;
	Station station(1, "Test", events, 128, 1000);
	const auto start = Station::Clock::now();
	std::string audio;
	auto at = [&](int milliseconds)
	{
		audio.clear();
		station.produce(start + std::chrono::milliseconds(milliseconds), audio);
	};
	std::vector<AudioCastEvent> seen;
	auto collect = [&] { for (auto& event : events.drain()) seen.push_back(event); };

	// The burst is full from the start, so every listener gets the same delay.
	CHECK(station.burst().size() == Mp3::msToFrames(1000) * 384);

	// Real-time pacing: about 41.7 frames per second.
	at(1000);
	const std::size_t firstSecond = countFrames(audio);
	CHECK(firstSecond >= 41 && firstSecond <= 44);
	at(1000);
	CHECK(audio.empty());

	station.enqueue(10, "first", false);
	station.enqueue(11, "second", false);
	CHECK(station.waitingFor(10));
	CHECK(station.status().loading);
	CHECK(station.trackLoaded(11, makeTrack("Second", 200)));
	CHECK(station.trackLoaded(10, makeTrack("First", 100)));
	CHECK(station.streamTitle() == "First"); // produced from now on...
	CHECK(!station.status().playing);        // ...but heard one burst later
	collect();
	CHECK(seen.empty());

	at(2000);
	StationStatus status = station.status();
	CHECK(status.playing && status.requestId == 10 && status.title == "First");
	CHECK(status.positionMs < 100);
	CHECK(status.durationMs == Mp3::framesToMs(100));

	// Two seconds after it was loaded the song is heard one second in.
	at(3000);
	status = station.status();
	CHECK(status.positionMs >= 950 && status.positionMs <= 1100);

	// After it ends the queued song follows.
	at(5000);
	CHECK(station.status().requestId == 11);

	// Radio_Play replaces the current song once loaded.
	station.enqueue(12, "now", true);
	CHECK(station.trackLoaded(12, makeTrack("Now", 500)));
	CHECK(station.status().requestId == 11);
	at(6100);
	CHECK(station.status().requestId == 12);

	CHECK(station.seek(5000));
	at(7200);
	status = station.status();
	CHECK(status.positionMs >= 5000 && status.positionMs <= 5300);

	// Pausing freezes what listeners hear (one burst later).
	CHECK(station.setPaused(true));
	CHECK(!station.setPaused(true));
	at(9000);
	status = station.status();
	CHECK(status.paused);
	const std::uint32_t frozen = status.positionMs;
	at(9500);
	CHECK(station.status().positionMs == frozen);
	CHECK(station.setPaused(false));
	at(10600);
	status = station.status();
	CHECK(!status.paused && status.positionMs > frozen && status.positionMs < frozen + 200);

	CHECK(station.skip());
	at(11700);
	CHECK(!station.status().playing);
	station.enqueue(13, "cancelled", false);
	station.stop();
	CHECK(!station.waitingFor(13));
	CHECK(!station.trackLoaded(13, makeTrack("Late", 10)));

	// A long stall does not flood the clients.
	at(70000);
	CHECK(countFrames(audio) <= 85);

	collect();
	std::vector<std::string> log;
	for (const AudioCastEvent& event : seen)
	{
		if (event.type == AudioCastEvent::Type::TrackStart) log.push_back("start " + std::to_string(event.requestId));
		if (event.type == AudioCastEvent::Type::TrackEnd)
		{
			log.push_back("end " + std::to_string(event.requestId) + " " + std::to_string(event.value));
		}
	}
	const std::vector<std::string> expected { "start 10", "end 10 0", "start 11", "end 11 3", "start 12", "end 12 1" };
	CHECK(log == expected);
	if (log != expected)
	{
		for (const auto& line : log) std::fprintf(stderr, "  event: %s\n", line.c_str());
	}
}

// A song still downloading plays what has arrived and waits for the rest.
void testGrowingTrack()
{
	EventQueue events;
	Station station(1, "Test", events, 128, 0);
	const auto start = Station::Clock::now();
	auto track = makeTrack("Growing", 20, false);
	station.enqueue(1, "growing", false);
	CHECK(station.trackLoaded(1, track));

	std::string audio;
	station.produce(start + std::chrono::milliseconds(1000), audio);
	StationStatus status = station.status();
	CHECK(status.playing);
	CHECK(status.positionMs == Mp3::framesToMs(20)); // waits after the last frame that arrived

	const std::vector<std::uint8_t> silence = Mp3::makeSilentFrame(128);
	for (int index = 0; index < 80; ++index) track->append(silence.data(), silence.size());
	station.produce(start + std::chrono::milliseconds(2000), audio);
	CHECK(station.status().positionMs > Mp3::framesToMs(50));

	track->finish();
	station.produce(start + std::chrono::milliseconds(4000), audio);
	CHECK(!station.status().playing);
	int ends = 0;
	for (const AudioCastEvent& event : events.drain())
	{
		if (event.type == AudioCastEvent::Type::TrackEnd) ++ends;
	}
	CHECK(ends == 1);
}
}

int main()
{
	testMp3();
	testQueries();
	testText();
	testStation();
	testGrowingTrack();
	if (failures == 0) std::puts("all tests passed");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
