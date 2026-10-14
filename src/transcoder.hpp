/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct lame_global_struct;

// Turns decoded PCM into the stream's MP3 format (48 kHz, stereo, constant
// bitrate, no tags) with LAME linked into the plugin.  PCM at another rate
// (AAC is usually 44.1 kHz) is resampled by LAME.  Works chunk by chunk, so a
// song can play while the rest is still converted.
//
// LAME's bit reservoir is disabled: every frame decodes on its own, so the
// stream can be cut anywhere (pause, seek, skip, a listener joining) without
// clicks.
class Mp3Encoder
{
private:
	struct EncoderDeleter { void operator()(lame_global_struct* encoder) const; };

	std::unique_ptr<lame_global_struct, EncoderDeleter> encoder_;
	float gain_ = 1.0f;
	std::vector<short> pcm_;
	std::vector<unsigned char> output_;

public:
	Mp3Encoder();
	~Mp3Encoder();

	// sampleRate: of the PCM passed to encode().  gain: linear factor
	// applied to every sample (1 = unchanged).
	bool begin(int sampleRate, int bitrateKbps, float gain, std::string& error);
	bool started() const { return encoder_ != nullptr; }
	// Interleaved stereo float.  Appends the MP3 bytes produced so far.
	bool encode(const float* stereo, std::size_t frames, std::vector<std::uint8_t>& mp3);
	bool finish(std::vector<std::uint8_t>& mp3);
};

// Gain that brings a song to about -16 dBFS RMS without clipping (two-pass
// loudness normalisation; needs the whole file).
bool MeasureNormalizeGain(const std::vector<std::uint8_t>& audio, const std::atomic<bool>& cancel, float& gain, std::string& error);

// Whole-file conversion of a downloaded song (Opus in WebM or AAC in MP4) to
// MP3.
bool TranscodeToMp3(const std::vector<std::uint8_t>& audio, int bitrateKbps, bool normalize, const std::atomic<bool>& cancel,
	std::vector<std::uint8_t>& mp3, std::string& error);
