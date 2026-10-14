/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "mp4.hpp"
#include "webm.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct OpusDecoder;
struct AAC_DECODER_INSTANCE;

// Turns a downloaded song into PCM while it is still arriving.  The file is
// Opus in WebM (libopus) or AAC in MP4 (fdk-aac); its first bytes tell which.
// PCM comes out as interleaved stereo float at the song's own sample rate
// (Opus: 48 kHz, AAC: usually 44.1 kHz), with the encoder delay removed.
class AudioDecoder
{
public:
	using PcmSink = std::function<void(const float* stereo, std::size_t frames)>;

private:
	enum class Format { Unknown, WebM, Mp4 };
	struct OpusDeleter { void operator()(OpusDecoder* decoder) const; };
	struct AacDeleter { void operator()(AAC_DECODER_INSTANCE* decoder) const; };

	Format format_ = Format::Unknown;
	std::vector<std::uint8_t> head_; // first bytes, until the format is known
	WebM::OpusStream webm_;
	Mp4::AacStream mp4_;
	std::unique_ptr<OpusDecoder, OpusDeleter> opus_;
	std::unique_ptr<AAC_DECODER_INSTANCE, AacDeleter> aac_;
	bool decoderFailed_ = false;
	int sampleRate_ = 0;
	std::uint64_t skip_ = 0; // frames still to drop (encoder delay)
	std::vector<float> decoded_;
	std::vector<std::int16_t> decoded16_;
	std::vector<float> stereo_;
	std::string error_;

	bool route(const std::uint8_t* data, std::size_t size, const PcmSink& sink);
	void opusPacket(const std::uint8_t* data, std::size_t size, const PcmSink& sink);
	void aacPacket(const std::uint8_t* data, std::size_t size, const PcmSink& sink);
	void deliver(const float* samples, std::size_t frames, int channels, const PcmSink& sink);

public:
	AudioDecoder();
	~AudioDecoder();
	AudioDecoder(const AudioDecoder&) = delete;
	AudioDecoder& operator=(const AudioDecoder&) = delete;

	// Adds the next bytes of the file.  Returns false once it is unusable.
	bool feed(const std::uint8_t* data, std::size_t size, const PcmSink& sink);
	// Call when the file is complete: false if it had no usable audio.
	bool finish(std::string& error) const;

	// Known before the first PCM reaches the sink.
	int sampleRate() const { return sampleRate_; }
	// "Opus", "AAC", or empty while the format is not known.
	const char* codec() const;
	const std::string& error() const { return error_; }
};
