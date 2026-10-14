/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "webm.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct OpusDecoder;
struct lame_global_struct;

// Turns Opus packets into the stream's MP3 format (48 kHz, stereo, constant
// bitrate, no tags) with libopus and LAME linked into the plugin.  Works
// packet by packet, so a song can play while the rest is still converted.
//
// LAME's bit reservoir is disabled: every frame decodes on its own, so the
// stream can be cut anywhere (pause, seek, skip, a listener joining) without
// clicks.
class OpusToMp3
{
private:
	struct DecoderDeleter { void operator()(OpusDecoder* decoder) const; };
	struct EncoderDeleter { void operator()(lame_global_struct* encoder) const; };

	std::unique_ptr<OpusDecoder, DecoderDeleter> decoder_;
	std::unique_ptr<lame_global_struct, EncoderDeleter> encoder_;
	int channels_ = 0;
	int skip_ = 0;
	float gain_ = 1.0f;
	std::vector<float> decoded_;
	std::vector<short> pcm_;
	std::vector<unsigned char> output_;

public:
	OpusToMp3();
	~OpusToMp3();

	// gain: linear factor applied to every sample (1 = unchanged).
	bool begin(const WebM::OpusInfo& info, int bitrateKbps, float gain, std::string& error);
	// Appends the MP3 bytes produced so far to mp3.
	bool packet(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& mp3);
	bool finish(std::vector<std::uint8_t>& mp3);
};

// Gain that brings a song to about -16 dBFS RMS without clipping (two-pass
// loudness normalisation; needs the whole file).
bool MeasureNormalizeGain(const std::vector<std::uint8_t>& webm, const std::atomic<bool>& cancel, float& gain, std::string& error);

// Whole-file conversion of Opus-in-WebM to MP3.
bool TranscodeWebmToMp3(const std::vector<std::uint8_t>& webm, int bitrateKbps, bool normalize, const std::atomic<bool>& cancel,
	std::vector<std::uint8_t>& mp3, std::string& error);
