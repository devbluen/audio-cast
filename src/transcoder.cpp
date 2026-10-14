/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "transcoder.hpp"
#include "mp3.hpp"
#include <lame.h>
#include <opus.h>
#include <algorithm>
#include <cmath>

namespace
{
// Longest Opus packet: 120 ms at 48 kHz.
constexpr int MAX_PACKET_SAMPLES = 5760;
}

void OpusToMp3::DecoderDeleter::operator()(OpusDecoder* decoder) const
{
	opus_decoder_destroy(decoder);
}

void OpusToMp3::EncoderDeleter::operator()(lame_global_struct* encoder) const
{
	lame_close(encoder);
}

OpusToMp3::OpusToMp3() = default;
OpusToMp3::~OpusToMp3() = default;

bool OpusToMp3::begin(const WebM::OpusInfo& info, int bitrateKbps, float gain, std::string& error)
{
	int status = 0;
	decoder_.reset(opus_decoder_create(Mp3::SAMPLE_RATE, info.channels, &status));
	if (status != OPUS_OK || !decoder_)
	{
		error = std::string("opus decoder: ") + opus_strerror(status);
		return false;
	}
	if (info.outputGain != 0) opus_decoder_ctl(decoder_.get(), OPUS_SET_GAIN(info.outputGain));
	channels_ = info.channels;
	skip_ = info.preSkip;
	gain_ = gain;

	encoder_.reset(lame_init());
	if (!encoder_)
	{
		error = "could not create the MP3 encoder";
		return false;
	}
	lame_global_flags* flags = encoder_.get();
	lame_set_in_samplerate(flags, Mp3::SAMPLE_RATE);
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
		error = "could not configure the MP3 encoder";
		return false;
	}

	decoded_.resize(static_cast<std::size_t>(MAX_PACKET_SAMPLES) * channels_);
	pcm_.resize(static_cast<std::size_t>(MAX_PACKET_SAMPLES) * 2);
	output_.resize(static_cast<std::size_t>(1.25 * MAX_PACKET_SAMPLES + 7200));
	return true;
}

bool OpusToMp3::packet(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& mp3)
{
	const int frames = opus_decode_float(decoder_.get(), data, static_cast<opus_int32>(size), decoded_.data(), MAX_PACKET_SAMPLES, 0);
	if (frames < 0) return true; // a damaged packet is skipped, not fatal

	int first = 0;
	if (skip_ > 0)
	{
		first = std::min(skip_, frames);
		skip_ -= first;
	}
	const int count = frames - first;
	if (count <= 0) return true;

	for (int frame = 0; frame < count; ++frame)
	{
		const std::size_t source = static_cast<std::size_t>(first + frame) * channels_;
		const float left = decoded_[source] * gain_;
		const float right = decoded_[source + (channels_ == 2 ? 1 : 0)] * gain_;
		pcm_[frame * 2] = static_cast<short>(std::lrint(std::clamp(left, -1.0f, 1.0f) * 32767.0f));
		pcm_[frame * 2 + 1] = static_cast<short>(std::lrint(std::clamp(right, -1.0f, 1.0f) * 32767.0f));
	}
	const int bytes = lame_encode_buffer_interleaved(encoder_.get(), pcm_.data(), count, output_.data(), static_cast<int>(output_.size()));
	if (bytes < 0) return false;
	mp3.insert(mp3.end(), output_.begin(), output_.begin() + bytes);
	return true;
}

bool OpusToMp3::finish(std::vector<std::uint8_t>& mp3)
{
	const int bytes = lame_encode_flush(encoder_.get(), output_.data(), static_cast<int>(output_.size()));
	if (bytes < 0) return false;
	mp3.insert(mp3.end(), output_.begin(), output_.begin() + bytes);
	return true;
}

bool MeasureNormalizeGain(const std::vector<std::uint8_t>& webm, const std::atomic<bool>& cancel, float& gain, std::string& error)
{
	gain = 1.0f;
	WebM::OpusStream stream;
	OpusDecoder* decoder = nullptr;
	std::vector<float> decoded;
	double sumSquares = 0.0;
	std::uint64_t samples = 0;
	float peak = 0.0f;
	int skip = 0;

	const bool parsed = stream.feed(webm.data(), webm.size(), [&](const std::uint8_t* data, std::size_t size)
	{
		if (!decoder)
		{
			int status = 0;
			decoder = opus_decoder_create(Mp3::SAMPLE_RATE, stream.info().channels, &status);
			if (status != OPUS_OK) return;
			if (stream.info().outputGain != 0) opus_decoder_ctl(decoder, OPUS_SET_GAIN(stream.info().outputGain));
			decoded.resize(static_cast<std::size_t>(MAX_PACKET_SAMPLES) * stream.info().channels);
			skip = stream.info().preSkip * stream.info().channels;
		}
		if (cancel.load()) return;
		const int frames = opus_decode_float(decoder, data, static_cast<opus_int32>(size), decoded.data(), MAX_PACKET_SAMPLES, 0);
		const int values = frames * stream.info().channels;
		for (int index = 0; index < values; ++index)
		{
			if (skip > 0)
			{
				--skip;
				continue;
			}
			const float value = decoded[index];
			sumSquares += double(value) * value;
			peak = std::max(peak, std::fabs(value));
			++samples;
		}
	});
	if (decoder) opus_decoder_destroy(decoder);
	if (cancel.load())
	{
		error = "cancelled";
		return false;
	}
	if (!parsed || !stream.finish(error)) return false;

	if (samples > 0 && sumSquares > 0.0)
	{
		const double rms = std::sqrt(sumSquares / double(samples));
		double wanted = std::pow(10.0, -16.0 / 20.0) / rms;
		if (peak > 0.0f) wanted = std::min(wanted, 0.98 / peak);
		gain = static_cast<float>(std::clamp(wanted, 0.1, 8.0));
	}
	return true;
}

bool TranscodeWebmToMp3(const std::vector<std::uint8_t>& webm, int bitrateKbps, bool normalize, const std::atomic<bool>& cancel,
	std::vector<std::uint8_t>& mp3, std::string& error)
{
	mp3.clear();
	float gain = 1.0f;
	if (normalize && !MeasureNormalizeGain(webm, cancel, gain, error)) return false;

	WebM::OpusStream stream;
	OpusToMp3 converter;
	bool started = false;
	bool failed = false;
	const bool parsed = stream.feed(webm.data(), webm.size(), [&](const std::uint8_t* data, std::size_t size)
	{
		if (failed || cancel.load()) return;
		if (!started)
		{
			started = true;
			if (!converter.begin(stream.info(), bitrateKbps, gain, error))
			{
				failed = true;
				return;
			}
		}
		if (!converter.packet(data, size, mp3))
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
	if (!parsed || !stream.finish(error)) return false;
	if (!converter.finish(mp3) || mp3.empty())
	{
		error = "the song has no audio";
		return false;
	}
	return true;
}
