/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 *
 *  Builds WebM files with real Opus audio (no lacing, Xiph lacing, EBML
 *  lacing, unknown-size cluster) and runs them through the built-in
 *  transcoder.
 */

#include "mp3.hpp"
#include "transcoder.hpp"
#include "webm.hpp"
#include <opus.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

using Bytes = std::vector<std::uint8_t>;

void append(Bytes& target, const Bytes& data)
{
	target.insert(target.end(), data.begin(), data.end());
}

Bytes id(std::uint32_t value)
{
	Bytes bytes;
	for (int shift = 24; shift >= 0; shift -= 8)
	{
		const std::uint8_t byte = static_cast<std::uint8_t>(value >> shift);
		if (!bytes.empty() || byte != 0) bytes.push_back(byte);
	}
	return bytes;
}

// Size as an 8-byte vint (or "unknown").
Bytes size(std::uint64_t value, bool unknown = false)
{
	Bytes bytes { 0x01 };
	for (int shift = 48; shift >= 0; shift -= 8) bytes.push_back(unknown ? 0xFF : static_cast<std::uint8_t>(value >> shift));
	return bytes;
}

Bytes element(std::uint32_t elementId, const Bytes& payload, bool unknownSize = false)
{
	Bytes bytes = id(elementId);
	append(bytes, size(payload.size(), unknownSize));
	append(bytes, payload);
	return bytes;
}

Bytes uint(std::uint32_t elementId, std::uint64_t value)
{
	Bytes payload;
	for (int shift = 56; shift >= 0; shift -= 8) payload.push_back(static_cast<std::uint8_t>(value >> shift));
	return element(elementId, payload);
}

Bytes text(std::uint32_t elementId, const std::string& value)
{
	return element(elementId, Bytes(value.begin(), value.end()));
}

std::vector<Bytes> encodeSine(int channels, double seconds)
{
	int error = 0;
	OpusEncoder* encoder = opus_encoder_create(48000, channels, OPUS_APPLICATION_AUDIO, &error);
	std::vector<Bytes> packets;
	const int frameSize = 960; // 20 ms
	std::vector<float> pcm(static_cast<std::size_t>(frameSize) * channels);
	const int frames = static_cast<int>(seconds * 48000 / frameSize);
	long sample = 0;
	for (int frame = 0; frame < frames; ++frame)
	{
		for (int index = 0; index < frameSize; ++index, ++sample)
		{
			const float value = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * sample / 48000.0));
			for (int channel = 0; channel < channels; ++channel) pcm[index * channels + channel] = value;
		}
		unsigned char buffer[4000];
		const int bytes = opus_encode_float(encoder, pcm.data(), frameSize, buffer, sizeof(buffer));
		packets.emplace_back(buffer, buffer + bytes);
	}
	opus_encoder_destroy(encoder);
	return packets;
}

enum class Lacing { None, Xiph, Ebml };

Bytes makeWebm(const std::vector<Bytes>& packets, int channels, Lacing lacing, bool unknownCluster)
{
	Bytes file = element(0x1A45DFA3, text(0x4282, "webm"));

	Bytes head = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, static_cast<std::uint8_t>(channels), 0x38, 0x01, 0x80, 0xBB, 0, 0, 0, 0, 0 };
	Bytes entry = uint(0xD7, 1);
	append(entry, uint(0x83, 2));
	append(entry, text(0x86, "A_OPUS"));
	append(entry, element(0x63A2, head));
	append(entry, element(0xE1, uint(0x9F, static_cast<std::uint64_t>(channels))));
	Bytes segment = element(0x1654AE6B, element(0xAE, entry));

	Bytes cluster = uint(0xE7, 0);
	for (std::size_t index = 0; index < packets.size();)
	{
		Bytes block = { 0x81, 0, 0 }; // track 1, timecode 0
		if (lacing == Lacing::None || index + 1 >= packets.size())
		{
			block.push_back(0x80);
			append(block, packets[index]);
			++index;
		}
		else
		{
			const Bytes& first = packets[index];
			const Bytes& second = packets[index + 1];
			if (lacing == Lacing::Xiph)
			{
				block.push_back(0x80 | (1 << 1));
				block.push_back(1); // two frames
				std::size_t value = first.size();
				while (value >= 255)
				{
					block.push_back(255);
					value -= 255;
				}
				block.push_back(static_cast<std::uint8_t>(value));
			}
			else
			{
				block.push_back(0x80 | (3 << 1));
				block.push_back(1);
				block.push_back(0x40 | static_cast<std::uint8_t>(first.size() >> 8)); // 2-byte vint
				block.push_back(static_cast<std::uint8_t>(first.size() & 0xFF));
			}
			append(block, first);
			append(block, second);
			index += 2;
		}
		append(cluster, element(0xA3, block));
	}
	append(segment, element(0x1F43B675, cluster, unknownCluster));
	append(file, element(0x18538067, segment, unknownCluster));
	return file;
}

std::size_t countPackets(const Bytes& webm, std::size_t chunk, WebM::OpusStream& stream)
{
	std::size_t packets = 0;
	for (std::size_t offset = 0; offset < webm.size(); offset += chunk)
	{
		const std::size_t size = std::min(chunk, webm.size() - offset);
		stream.feed(webm.data() + offset, size, [&](const std::uint8_t*, std::size_t) { ++packets; });
	}
	return packets;
}

void checkTranscode(const char* name, int channels, Lacing lacing, bool unknownCluster)
{
	const auto packets = encodeSine(channels, 2.0);
	const Bytes webm = makeWebm(packets, channels, lacing, unknownCluster);

	// The same packets come out whether the file arrives at once or in pieces.
	for (std::size_t chunk : { webm.size(), std::size_t(1), std::size_t(7), std::size_t(4096) })
	{
		WebM::OpusStream stream;
		std::string error;
		CHECK(countPackets(webm, chunk, stream) == packets.size());
		CHECK(stream.finish(error));
		CHECK(stream.info().channels == channels);
		CHECK(stream.info().preSkip == 312);
	}

	std::atomic<bool> cancel { false };
	Bytes mp3;
	std::string error;
	const bool ok = TranscodeWebmToMp3(webm, 128, true, cancel, mp3, error);
	if (!ok) std::fprintf(stderr, "%s: %s\n", name, error.c_str());
	CHECK(ok);

	std::vector<Mp3::Frame> frames;
	CHECK(Mp3::indexFrames(mp3, frames, error));
	const auto duration = Mp3::framesToMs(frames.size());
	std::printf("%s: %zu Opus packets -> %zu MP3 frames, %llu ms\n", name, packets.size(), frames.size(),
		static_cast<unsigned long long>(duration));
	CHECK(duration >= 1950 && duration <= 2100);
	for (const Mp3::Frame& frame : frames)
	{
		CHECK(frame.size == 384);
		// main_data_begin (first 9 bits of the side information) is 0: no
		// frame borrows bits from the previous one, so cuts are clean.
		const std::uint8_t* side = mp3.data() + frame.offset + 4;
		CHECK(((side[0] << 1) | (side[1] >> 7)) == 0);
	}

	// FrameSplitter cuts the same frames from a stream that arrives in pieces.
	Mp3::FrameSplitter splitter;
	std::size_t split = 0;
	for (std::size_t offset = 0; offset < mp3.size(); offset += 100)
	{
		splitter.push(mp3.data() + offset, std::min<std::size_t>(100, mp3.size() - offset), [&](const std::uint8_t*, std::size_t size)
		{
			CHECK(size == 384);
			++split;
		});
	}
	CHECK(split == frames.size());
}
}

int main()
{
	checkTranscode("stereo", 2, Lacing::None, false);
	checkTranscode("mono, Xiph lacing", 1, Lacing::Xiph, false);
	checkTranscode("stereo, EBML lacing, unknown sizes", 2, Lacing::Ebml, true);

	std::string error;
	WebM::OpusStream garbage;
	const Bytes junk(100, 0x42);
	garbage.feed(junk.data(), junk.size(), [](const std::uint8_t*, std::size_t) {});
	CHECK(!garbage.finish(error));

	if (failures == 0) std::puts("transcoder tests passed");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
