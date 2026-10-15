/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Mp3
{
// Every track is transcoded to this format, so frames of any track (and the
// generated silence) can follow each other in one stream.  48 kHz is Opus'
// native rate; AAC audio (usually 44.1 kHz) is resampled by the encoder.
constexpr int SAMPLE_RATE = 48000;
constexpr int SAMPLES_PER_FRAME = 1152;

struct FrameHeader
{
	int version = 0; // 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5
	int bitrateKbps = 0;
	int sampleRate = 0;
	int samples = 0;
	bool padding = false;
	bool crc = false;
	bool mono = false;
	std::size_t size = 0;
};

struct Frame
{
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
};

// Parses a Layer III frame header.  Returns false for anything else.
bool parseHeader(const std::uint8_t* data, std::size_t available, FrameHeader& header);

// Lists the audio frames of an MP3 file, skipping ID3 tags, junk and the
// Xing/Info/VBRI header frame.  All frames must be MPEG-1 at SAMPLE_RATE.
bool indexFrames(const std::vector<std::uint8_t>& data, std::vector<Frame>& frames, std::string& error);

// Cuts an MP3 byte stream that arrives in pieces into whole frames.
class FrameSplitter
{
private:
	std::vector<std::uint8_t> pending_;

public:
	// Calls onFrame(data, size) for every complete frame.
	template <typename OnFrame>
	void push(const std::uint8_t* data, std::size_t size, OnFrame&& onFrame)
	{
		pending_.insert(pending_.end(), data, data + size);
		std::size_t position = 0;
		while (pending_.size() - position >= 4)
		{
			FrameHeader header;
			if (!parseHeader(pending_.data() + position, pending_.size() - position, header))
			{
				++position; // not a frame start: resynchronise
				continue;
			}
			if (header.size > pending_.size() - position) break;
			onFrame(pending_.data() + position, header.size);
			position += header.size;
		}
		pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(position));
	}
};

// One MPEG-1 Layer III frame at SAMPLE_RATE that decodes to silence.
std::vector<std::uint8_t> makeSilentFrame(int bitrateKbps);

// Volume, as a number of 1.5 dB steps to lower the audio by.  MP3 frames
// store a global gain per granule and channel; lowering it by one step
// scales the decoded audio by 2^(-1/4).  That changes the volume of
// already encoded frames without decoding them, so every listener can have
// their own.
constexpr int MUTE_STEPS = 255;

// percent: 0 (mute) to 100 (unchanged).  50% is half the amplitude.
int volumeToSteps(double percent);

// Lowers the volume of a buffer of whole frames in place.  Frames other
// than MPEG-1 Layer III without CRC are left as they are.
void attenuate(std::uint8_t* frames, std::size_t size, int steps);

// Duration of a number of frames, in milliseconds.
inline std::uint64_t framesToMs(std::uint64_t frames)
{
	return frames * SAMPLES_PER_FRAME * 1000 / SAMPLE_RATE;
}

inline std::uint64_t msToFrames(std::uint64_t ms)
{
	return ms * SAMPLE_RATE / (1000ull * SAMPLES_PER_FRAME);
}
}
