/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "mp3.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Mp3
{
namespace
{
const int kBitratesV1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1 };
const int kBitratesV2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1 };
const int kSampleRatesV1[4] = { 44100, 48000, 32000, -1 };

std::size_t sideInfoSize(const FrameHeader& header)
{
	if (header.version == 1) return header.mono ? 17 : 32;
	return header.mono ? 9 : 17;
}

bool isInfoFrame(const std::uint8_t* frame, const FrameHeader& header)
{
	const std::size_t offset = 4 + (header.crc ? 2 : 0) + sideInfoSize(header);
	if (offset + 4 <= header.size)
	{
		if (std::memcmp(frame + offset, "Xing", 4) == 0 || std::memcmp(frame + offset, "Info", 4) == 0) return true;
	}
	return header.size >= 40 && std::memcmp(frame + 36, "VBRI", 4) == 0;
}

unsigned readBits(const std::uint8_t* data, std::size_t bit, int count)
{
	unsigned value = 0;
	for (int index = 0; index < count; ++index, ++bit) value = (value << 1) | ((data[bit / 8] >> (7 - bit % 8)) & 1);
	return value;
}

void writeBits(std::uint8_t* data, std::size_t bit, int count, unsigned value)
{
	for (int index = count - 1; index >= 0; --index, ++bit)
	{
		const std::uint8_t mask = static_cast<std::uint8_t>(0x80 >> (bit % 8));
		if ((value >> index) & 1) data[bit / 8] |= mask;
		else data[bit / 8] &= static_cast<std::uint8_t>(~mask);
	}
}

std::size_t id3v2Size(const std::vector<std::uint8_t>& data, std::size_t offset)
{
	if (data.size() < offset + 10 || std::memcmp(data.data() + offset, "ID3", 3) != 0) return 0;
	const std::uint8_t* p = data.data() + offset;
	if ((p[6] | p[7] | p[8] | p[9]) & 0x80) return 0;
	std::size_t size = (std::size_t(p[6]) << 21) | (std::size_t(p[7]) << 14) | (std::size_t(p[8]) << 7) | p[9];
	size += 10;
	if (p[5] & 0x10) size += 10;
	return size;
}
}

bool parseHeader(const std::uint8_t* data, std::size_t available, FrameHeader& header)
{
	if (available < 4 || data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) return false;

	const int versionBits = (data[1] >> 3) & 3;
	const int layerBits = (data[1] >> 1) & 3;
	if (versionBits == 1 || layerBits != 1) return false;

	const int bitrateIndex = data[2] >> 4;
	const int sampleRateIndex = (data[2] >> 2) & 3;
	if (bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3) return false;

	header.version = versionBits == 3 ? 1 : (versionBits == 2 ? 2 : 25);
	header.bitrateKbps = header.version == 1 ? kBitratesV1[bitrateIndex] : kBitratesV2[bitrateIndex];
	header.sampleRate = kSampleRatesV1[sampleRateIndex];
	if (header.version == 2) header.sampleRate /= 2;
	if (header.version == 25) header.sampleRate /= 4;
	header.samples = header.version == 1 ? 1152 : 576;
	header.padding = ((data[2] >> 1) & 1) != 0;
	header.crc = (data[1] & 1) == 0;
	header.mono = (data[3] >> 6) == 3;
	const std::size_t coefficient = header.version == 1 ? 144 : 72;
	header.size = coefficient * std::size_t(header.bitrateKbps) * 1000 / std::size_t(header.sampleRate) + (header.padding ? 1 : 0);
	return header.size > 4;
}

bool indexFrames(const std::vector<std::uint8_t>& data, std::vector<Frame>& frames, std::string& error)
{
	frames.clear();
	std::size_t position = id3v2Size(data, 0);
	const std::size_t total = data.size();
	bool first = true;

	while (position + 4 <= total)
	{
		FrameHeader header;
		if (!parseHeader(data.data() + position, total - position, header) || position + header.size > total)
		{
			++position;
			continue;
		}

		// A sync word inside garbage is accepted only when another frame (or
		// the end of the file) follows it.
		const std::size_t next = position + header.size;
		FrameHeader following;
		if (next + 4 <= total && !parseHeader(data.data() + next, total - next, following) && id3v2Size(data, next) == 0 &&
			std::memcmp(data.data() + next, "TAG", 3) != 0)
		{
			++position;
			continue;
		}

		if (header.version != 1 || header.sampleRate != SAMPLE_RATE || header.samples != SAMPLES_PER_FRAME)
		{
			error = "unsupported MP3 format (expected MPEG-1 Layer III at 48000 Hz)";
			frames.clear();
			return false;
		}

		if (!(first && isInfoFrame(data.data() + position, header)))
		{
			frames.push_back(Frame { static_cast<std::uint32_t>(position), static_cast<std::uint32_t>(header.size) });
		}
		first = false;
		position = next;
	}

	if (frames.empty())
	{
		error = "no MP3 audio frames found";
		return false;
	}
	return true;
}

int volumeToSteps(double percent)
{
	if (percent <= 0.0) return MUTE_STEPS;
	if (percent >= 100.0) return 0;
	return std::min(MUTE_STEPS, static_cast<int>(std::lround(4.0 * std::log2(100.0 / percent))));
}

void attenuate(std::uint8_t* frames, std::size_t size, int steps)
{
	if (steps <= 0) return;
	std::size_t position = 0;
	while (size - position >= 4)
	{
		FrameHeader header;
		if (!parseHeader(frames + position, size - position, header) || header.size > size - position) return;
		// The side information follows the header: main_data_begin (9 bits),
		// private bits (5 mono / 3 stereo), scfsi (4 per channel), then a
		// 59-bit block per granule and channel whose global_gain starts at
		// bit 21.  A CRC would cover these bits, so frames with one stay as
		// they are.
		if (header.version == 1 && !header.crc && header.size >= 4 + sideInfoSize(header))
		{
			std::uint8_t* side = frames + position + 4;
			const int channels = header.mono ? 1 : 2;
			const std::size_t first = header.mono ? 18 : 20;
			for (int block = 0; block < 2 * channels; ++block)
			{
				const std::size_t bit = first + static_cast<std::size_t>(block) * 59 + 21;
				const int gain = static_cast<int>(readBits(side, bit, 8));
				writeBits(side, bit, 8, static_cast<unsigned>(steps >= MUTE_STEPS ? 0 : std::max(0, gain - steps)));
			}
		}
		position += header.size;
	}
}

std::vector<std::uint8_t> makeSilentFrame(int bitrateKbps)
{
	int bitrateIndex = 9; // 128 kbps
	for (int index = 1; index < 15; ++index)
	{
		if (kBitratesV1[index] == bitrateKbps) bitrateIndex = index;
	}

	const std::size_t size = 144 * std::size_t(kBitratesV1[bitrateIndex]) * 1000 / SAMPLE_RATE;
	// MPEG-1, Layer III, no CRC, 48000 Hz, no padding, joint stereo.  The
	// side information is all zero, so no main data is read and every
	// granule decodes to silence.
	std::vector<std::uint8_t> frame(size, 0);
	frame[0] = 0xFF;
	frame[1] = 0xFB;
	frame[2] = static_cast<std::uint8_t>((bitrateIndex << 4) | (1 << 2));
	frame[3] = 0x44;
	return frame;
}
}
