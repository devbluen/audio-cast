/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "decoder.hpp"
#include <aacdecoder_lib.h>
#include <opus.h>
#include <algorithm>
#include <cstring>

namespace
{
constexpr int OPUS_RATE = 48000;
// Longest Opus packet: 120 ms at 48 kHz.
constexpr int MAX_OPUS_SAMPLES = 5760;
// Longest AAC frame (HE-AAC: 2048 samples) for up to 8 channels, before the
// downmix to stereo.
constexpr int MAX_AAC_VALUES = 2048 * 8;
}

void AudioDecoder::OpusDeleter::operator()(OpusDecoder* decoder) const
{
	opus_decoder_destroy(decoder);
}

void AudioDecoder::AacDeleter::operator()(AAC_DECODER_INSTANCE* decoder) const
{
	aacDecoder_Close(decoder);
}

AudioDecoder::AudioDecoder() = default;
AudioDecoder::~AudioDecoder() = default;

const char* AudioDecoder::codec() const
{
	switch (format_)
	{
	case Format::WebM: return "Opus";
	case Format::Mp4: return "AAC";
	default: return "";
	}
}

void AudioDecoder::deliver(const float* samples, std::size_t frames, int channels, const PcmSink& sink)
{
	std::size_t first = 0;
	if (skip_ > 0)
	{
		first = static_cast<std::size_t>(std::min<std::uint64_t>(skip_, frames));
		skip_ -= first;
	}
	if (first >= frames) return;
	const std::size_t count = frames - first;

	stereo_.resize(count * 2);
	for (std::size_t frame = 0; frame < count; ++frame)
	{
		const float* source = samples + (first + frame) * channels;
		stereo_[frame * 2] = source[0];
		stereo_[frame * 2 + 1] = source[channels >= 2 ? 1 : 0];
	}
	sink(stereo_.data(), count);
}

void AudioDecoder::opusPacket(const std::uint8_t* data, std::size_t size, const PcmSink& sink)
{
	if (decoderFailed_) return;
	const WebM::OpusInfo& info = webm_.info();
	if (!opus_)
	{
		int status = 0;
		opus_.reset(opus_decoder_create(OPUS_RATE, info.channels, &status));
		if (status != OPUS_OK || !opus_)
		{
			error_ = std::string("opus decoder: ") + opus_strerror(status);
			decoderFailed_ = true;
			return;
		}
		if (info.outputGain != 0) opus_decoder_ctl(opus_.get(), OPUS_SET_GAIN(info.outputGain));
		sampleRate_ = OPUS_RATE;
		skip_ = static_cast<std::uint64_t>(info.preSkip);
		decoded_.resize(static_cast<std::size_t>(MAX_OPUS_SAMPLES) * info.channels);
	}

	const int frames = opus_decode_float(opus_.get(), data, static_cast<opus_int32>(size), decoded_.data(), MAX_OPUS_SAMPLES, 0);
	if (frames < 0) return; // a damaged packet is skipped, not fatal
	deliver(decoded_.data(), static_cast<std::size_t>(frames), info.channels, sink);
}

void AudioDecoder::aacPacket(const std::uint8_t* data, std::size_t size, const PcmSink& sink)
{
	if (decoderFailed_) return;
	const Mp4::AacInfo& info = mp4_.info();
	if (!aac_)
	{
		aac_.reset(aacDecoder_Open(TT_MP4_RAW, 1));
		if (!aac_)
		{
			error_ = "could not create the AAC decoder";
			decoderFailed_ = true;
			return;
		}
		UCHAR* config = const_cast<UCHAR*>(info.config.data());
		const UINT configSize = static_cast<UINT>(info.config.size());
		if (aacDecoder_ConfigRaw(aac_.get(), &config, &configSize) != AAC_DEC_OK)
		{
			error_ = "unsupported AAC audio";
			decoderFailed_ = true;
			return;
		}
		// Stereo at most, and no limiter: it would only add delay.
		aacDecoder_SetParam(aac_.get(), AAC_PCM_MAX_OUTPUT_CHANNELS, 2);
		aacDecoder_SetParam(aac_.get(), AAC_PCM_LIMITER_ENABLE, 0);
		decoded16_.resize(MAX_AAC_VALUES);
	}

	UCHAR* input = const_cast<UCHAR*>(data);
	const UINT inputSize = static_cast<UINT>(size);
	UINT left = inputSize;
	if (aacDecoder_Fill(aac_.get(), &input, &inputSize, &left) != AAC_DEC_OK) return;
	const AAC_DECODER_ERROR status = aacDecoder_DecodeFrame(aac_.get(), decoded16_.data(), MAX_AAC_VALUES, 0);
	if (!IS_OUTPUT_VALID(status)) return; // a damaged frame is skipped, not fatal

	const CStreamInfo* stream = aacDecoder_GetStreamInfo(aac_.get());
	if (!stream || stream->frameSize <= 0 || stream->numChannels <= 0 || stream->sampleRate <= 0) return;
	if (stream->frameSize * stream->numChannels > MAX_AAC_VALUES) return;
	if (sampleRate_ == 0)
	{
		sampleRate_ = stream->sampleRate;
		// The edit list's delay is in track time; the decoder's rate can
		// differ from it (HE-AAC doubles the core rate).
		skip_ = info.timescale ? info.priming * static_cast<std::uint64_t>(sampleRate_) / info.timescale : 0;
	}
	else if (stream->sampleRate != sampleRate_)
	{
		return; // the stream cannot switch rates midway; drop the odd frame
	}

	const std::size_t values = static_cast<std::size_t>(stream->frameSize) * stream->numChannels;
	decoded_.resize(values);
	for (std::size_t index = 0; index < values; ++index) decoded_[index] = decoded16_[index] / 32768.0f;
	deliver(decoded_.data(), static_cast<std::size_t>(stream->frameSize), stream->numChannels, sink);
}

bool AudioDecoder::route(const std::uint8_t* data, std::size_t size, const PcmSink& sink)
{
	bool ok = false;
	if (format_ == Format::WebM)
	{
		ok = webm_.feed(data, size, [&](const std::uint8_t* packet, std::size_t length) { opusPacket(packet, length, sink); });
		if (!ok && error_.empty()) error_ = webm_.error();
	}
	else
	{
		ok = mp4_.feed(data, size, [&](const std::uint8_t* packet, std::size_t length) { aacPacket(packet, length, sink); });
		if (!ok && error_.empty()) error_ = mp4_.error();
	}
	return ok && !decoderFailed_;
}

bool AudioDecoder::feed(const std::uint8_t* data, std::size_t size, const PcmSink& sink)
{
	if (!error_.empty()) return false;
	if (format_ != Format::Unknown) return route(data, size, sink);

	head_.insert(head_.end(), data, data + size);
	if (head_.size() < 8) return true;
	static const std::uint8_t EBML[] = { 0x1A, 0x45, 0xDF, 0xA3 };
	if (std::memcmp(head_.data(), EBML, sizeof(EBML)) == 0)
	{
		format_ = Format::WebM;
	}
	else if (Mp4::LooksLikeMp4(head_.data(), head_.size()))
	{
		format_ = Format::Mp4;
	}
	else
	{
		error_ = "the audio is neither WebM nor MP4";
		return false;
	}
	std::vector<std::uint8_t> head;
	head.swap(head_);
	return route(head.data(), head.size(), sink);
}

bool AudioDecoder::finish(std::string& error) const
{
	if (!error_.empty())
	{
		error = error_;
		return false;
	}
	switch (format_)
	{
	case Format::WebM: return webm_.finish(error);
	case Format::Mp4: return mp4_.finish(error);
	default:
		error = head_.empty() ? "no audio was received" : "the audio is neither WebM nor MP4";
		return false;
	}
}
