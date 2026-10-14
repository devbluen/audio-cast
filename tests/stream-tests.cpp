/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 *
 *  Loopback test of the HTTP stream: two listeners join at different times
 *  and must end up playing the same frame at the same moment.
 */

#include "station.hpp"
#include "stream-server.hpp"
#include <asio.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

using tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;

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

constexpr std::uint8_t MARK = 0xAB;
constexpr std::size_t MARK_OFFSET = 200;

// A track whose frames carry their own index, so a client can tell which
// moment of the song it received.
std::shared_ptr<const Track> numberedTrack(std::size_t frames)
{
	auto track = std::make_shared<Track>();
	track->videoId = "dQw4w9WgXcQ";
	track->title = "Numbered";
	const std::vector<std::uint8_t> silence = Mp3::makeSilentFrame(128);
	for (std::size_t index = 0; index < frames; ++index)
	{
		std::vector<std::uint8_t> frame = silence;
		frame[MARK_OFFSET] = MARK;
		frame[MARK_OFFSET + 1] = static_cast<std::uint8_t>(index & 0xFF);
		frame[MARK_OFFSET + 2] = static_cast<std::uint8_t>(index >> 8);
		track->append(frame.data(), frame.size());
	}
	track->finish();
	return track;
}

struct Capture
{
	std::string status;
	std::vector<int> frames; // track frame index, -1 for silence
	Clock::time_point firstAudio;
};

std::string request(int port, const std::string& path)
{
	asio::io_context io;
	tcp::socket socket(io);
	socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)));
	const std::string text = "GET " + path + " HTTP/1.0\r\n\r\n";
	asio::write(socket, asio::buffer(text));
	std::string response;
	std::error_code error;
	char buffer[1024];
	while (response.size() < 4096)
	{
		const std::size_t count = socket.read_some(asio::buffer(buffer), error);
		if (error) break;
		response.append(buffer, count);
	}
	return response.substr(0, response.find("\r\n"));
}

Capture listen(int port, const std::string& path, std::chrono::milliseconds duration)
{
	Capture capture;
	asio::io_context io;
	tcp::socket socket(io);
	socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)));
	const std::string text = "GET " + path + " HTTP/1.1\r\nHost: test\r\n\r\n";
	asio::write(socket, asio::buffer(text));

	std::string data;
	bool headersDone = false;
	const auto end = Clock::now() + duration;
	char buffer[8192];
	while (Clock::now() < end)
	{
		std::error_code error;
		const std::size_t count = socket.read_some(asio::buffer(buffer), error);
		if (error) break;
		data.append(buffer, count);
		if (!headersDone)
		{
			const std::size_t headerEnd = data.find("\r\n\r\n");
			if (headerEnd == std::string::npos) continue;
			capture.status = data.substr(0, data.find("\r\n"));
			data.erase(0, headerEnd + 4);
			headersDone = true;
			capture.firstAudio = Clock::now();
		}
	}

	std::vector<std::uint8_t> bytes(data.begin(), data.end());
	std::vector<Mp3::Frame> frames;
	std::string error;
	Mp3::indexFrames(bytes, frames, error);
	for (const Mp3::Frame& frame : frames)
	{
		const std::uint8_t* p = bytes.data() + frame.offset;
		capture.frames.push_back(frame.size > MARK_OFFSET + 2 && p[MARK_OFFSET] == MARK
			? p[MARK_OFFSET + 1] | (p[MARK_OFFSET + 2] << 8)
			: -1);
	}
	return capture;
}
}

int main()
{
	EventQueue events;
	StreamServer server(events);
	AudioCastConfig config;
	config.bindAddress = "127.0.0.1";
	config.port = 0;
	config.burstMs = 1000;
	std::string error;
	if (!server.start(config, error))
	{
		std::fprintf(stderr, "server failed: %s\n", error.c_str());
		return EXIT_FAILURE;
	}
	const int port = server.boundPort();

	auto station = std::make_shared<Station>(1, "Test radio", events, 128, config.burstMs);
	server.addStation(station);
	station->enqueue(1, "numbered", false);
	station->trackLoaded(1, numberedTrack(3000));
	server.addTicket("tokena", StreamServer::Ticket { 5, 1 });
	server.addTicket("tokenb", StreamServer::Ticket { 6, 1 });

	CHECK(request(port, "/radio/1/wrong.mp3") == "HTTP/1.0 403 Forbidden");
	CHECK(request(port, "/radio/1.mp3") == "HTTP/1.0 403 Forbidden");
	CHECK(request(port, "/radio/9/tokena.mp3") == "HTTP/1.0 403 Forbidden");
	CHECK(request(port, "/nothing") == "HTTP/1.0 404 Not Found");

	// Let the burst history fill with song frames.
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));

	Capture first;
	Capture second;
	std::thread listenerA([&] { first = listen(port, "/radio/1/tokena.mp3", std::chrono::milliseconds(4500)); });
	std::this_thread::sleep_for(std::chrono::milliseconds(1700));
	std::thread listenerB([&] { second = listen(port, "/radio/1/tokenb.mp3", std::chrono::milliseconds(2500)); });
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	CHECK(server.listenerCount(1) == 2);
	CHECK(server.ticketConnected("tokena"));
	listenerA.join();
	listenerB.join();

	CHECK(first.status == "HTTP/1.0 200 OK");
	CHECK(second.status == "HTTP/1.0 200 OK");
	CHECK(first.frames.size() > 100);
	CHECK(second.frames.size() > 60);

	if (!first.frames.empty() && !second.frames.empty())
	{
		// The second stream must be the first one, shifted by k frames...
		std::size_t offset = 0;
		while (offset < first.frames.size() && first.frames[offset] != second.frames.front()) ++offset;
		CHECK(offset < first.frames.size());
		std::size_t mismatches = 0;
		std::size_t compared = 0;
		for (std::size_t index = 0; index < second.frames.size() && offset + index < first.frames.size(); ++index)
		{
			++compared;
			if (second.frames[index] != first.frames[offset + index]) ++mismatches;
		}
		CHECK(compared > 50);
		CHECK(mismatches == 0);

		// ...where k is exactly how much later it joined.  A client plays its
		// data from the moment it arrives, so both play the same frame at the
		// same wall-clock time.
		const double joinedLater = std::chrono::duration<double>(second.firstAudio - first.firstAudio).count();
		const double expected = joinedLater * Mp3::SAMPLE_RATE / Mp3::SAMPLES_PER_FRAME;
		std::printf("listener B joined %.3f s later: offset %zu frames, expected %.1f\n", joinedLater, offset, expected);
		CHECK(std::fabs(static_cast<double>(offset) - expected) <= 3.0);

		// Each listener first receives exactly one burst of recent audio.
		CHECK(second.frames.front() >= 0);
	}

	int connects = 0;
	for (const AudioCastEvent& event : events.drain())
	{
		if (event.type == AudioCastEvent::Type::ListenerConnect) ++connects;
	}
	CHECK(connects == 2);

	// Pause and resume: the listener hears silence, then the song continues
	// with exactly the frame after the last one it heard.
	{
		Capture paused;
		std::thread listener([&] { paused = listen(port, "/radio/1/tokenb.mp3", std::chrono::milliseconds(4000)); });
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
		CHECK(station->setPaused(true));
		std::this_thread::sleep_for(std::chrono::milliseconds(1200));
		CHECK(station->setPaused(false));
		listener.join();

		std::size_t silence = 0;
		std::size_t jumps = 0;
		int last = -1;
		for (int frame : paused.frames)
		{
			if (frame < 0)
			{
				++silence;
				continue;
			}
			if (last >= 0 && frame != last + 1) ++jumps;
			last = frame;
		}
		std::printf("pause: %zu silent frames, %zu jumps in the song\n", silence, jumps);
		CHECK(silence >= 40 && silence <= 60); // ~1.2 s
		CHECK(jumps == 0);
	}

	server.removeTicket("tokena");
	server.stop();
	if (failures == 0) std::puts("stream tests passed");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
