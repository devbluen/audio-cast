/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "transcoder.hpp"
#include "decoder.hpp"
#include "mp3.hpp"
#include <lame.h>
#include <algorithm>
#include <cmath>

namespace
{
// PCM frames handed to LAME per call.
constexpr std::size_t CHUNK_FRAMES = 8192;
}

void Mp3Encoder::EncoderDeleter::operator()(lame_global_struct* encoder) const
{
	lame_close(encoder);
}

Mp3Encoder::Mp3Encoder() = default;
Mp3Encoder::~Mp3Encoder() = default;

bool Mp3Encoder::begin(int sampleRate, int bitrateKbps, float gain, std::string& error)
{
	gain_ = gain;
	encoder_.reset(lame_init());
	if (!encoder_)
	{
		error = "could not create the MP3 encoder";
		return false;
	}
	lame_global_flags* flags = encoder_.get();
	lame_set_in_samplerate(flags, sampleRate);
	lame_set_out_samplerate(flags, Mp3::SAMPLE_RATE);
	lame_set_num_channels(flags, 2);
	lame_set_mode(flags, JOINT_STEREO);
	lame_set_VBR(flags, vbr_off);
	lame_set_brate(flags, bitrateKbps);
	lame_set_quality(flags, 3);
	lame_set_disable_reservoir(flags, 1);
	// No Xing/Info frame and no ID3 tags: frames are joined into one stream.
	lame_set_bWriteVbrTag(flags, 0);
	if (lame_init_params(flags) < 0)
	{
		encoder_.reset();
		error = "could not configure the MP3 encoder";
		return false;
	}

	pcm_.resize(CHUNK_FRAMES * 2);
	// LAME's worst case is 1.25 * samples + 7200, for samples at the output
	// rate; upsampling (44.1 -> 48 kHz) needs a little more.
	output_.resize(CHUNK_FRAMES * 2 + 7200);
	return true;
}

bool Mp3Encoder::encode(const float* stereo, std::size_t frames, std::vector<std::uint8_t>& mp3)
{
	while (frames > 0)
	{
		const std::size_t count = std::min(frames, CHUNK_FRAMES);
		for (std::size_t index = 0; index < count * 2; ++index)
		{
			const float value = std::clamp(stereo[index] * gain_, -1.0f, 1.0f);
			pcm_[index] = static_cast<short>(std::lrint(value * 32767.0f));
		}
		const int bytes = lame_encode_buffer_interleaved(encoder_.get(), pcm_.data(), static_cast<int>(count), output_.data(),
			static_cast<int>(output_.size()));
		if (bytes < 0) return false;
		mp3.insert(mp3.end(), output_.begin(), output_.begin() + bytes);
		stereo += count * 2;
		frames -= count;
	}
	return true;
}

bool Mp3Encoder::finish(std::vector<std::uint8_t>& mp3)
{
	const int bytes = lame_encode_flush(encoder_.get(), output_.data(), static_cast<int>(output_.size()));
	if (bytes < 0) return false;
	mp3.insert(mp3.end(), output_.begin(), output_.begin() + bytes);
	return true;
}

bool MeasureNormalizeGain(const std::vector<std::uint8_t>& audio, const std::atomic<bool>& cancel, float& gain, std::string& error)
{
	gain = 1.0f;
	AudioDecoder decoder;
	double sumSquares = 0.0;
	std::uint64_t samples = 0;
	float peak = 0.0f;

	const bool parsed = decoder.feed(audio.data(), audio.size(), [&](const float* stereo, std::size_t frames)
	{
		if (cancel.load()) return;
		for (std::size_t index = 0; index < frames * 2; ++index)
		{
			const float value = stereo[index];
			sumSquares += double(value) * value;
			peak = std::max(peak, std::fabs(value));
		}
		samples += frames * 2;
	});
	if (cancel.load())
	{
		error = "cancelled";
		return false;
	}
	if (!parsed || !decoder.finish(error))
	{
		if (error.empty()) error = decoder.error();
		return false;
	}

	if (samples > 0 && sumSquares > 0.0)
	{
		const double rms = std::sqrt(sumSquares / double(samples));
		double wanted = std::pow(10.0, -16.0 / 20.0) / rms;
		if (peak > 0.0f) wanted = std::min(wanted, 0.98 / peak);
		gain = static_cast<float>(std::clamp(wanted, 0.1, 8.0));
	}
	return true;
}

bool TranscodeToMp3(const std::vector<std::uint8_t>& audio, int bitrateKbps, bool normalize, const std::atomic<bool>& cancel,
	std::vector<std::uint8_t>& mp3, std::string& error)
{
	mp3.clear();
	float gain = 1.0f;
	if (normalize && !MeasureNormalizeGain(audio, cancel, gain, error)) return false;

	AudioDecoder decoder;
	Mp3Encoder encoder;
	bool failed = false;
	const bool parsed = decoder.feed(audio.data(), audio.size(), [&](const float* stereo, std::size_t frames)
	{
		if (failed || cancel.load()) return;
		if (!encoder.started() && !encoder.begin(decoder.sampleRate(), bitrateKbps, gain, error))
		{
			failed = true;
			return;
		}
		if (!encoder.encode(stereo, frames, mp3))
		{
			error = "MP3 encoding failed";
			failed = true;
		}
	});
	if (cancel.load())
	{
		error = "cancelled";
		return false;
	}
	if (failed) return false;
	if (!parsed || !decoder.finish(error))
	{
		if (error.empty()) error = decoder.error();
		return false;
	}
	if (!encoder.started() || !encoder.finish(mp3) || mp3.empty())
	{
		error = "the song has no audio";
		return false;
	}
	return true;
}
