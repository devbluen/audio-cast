/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace Mp4
{
struct AacInfo
{
	std::vector<std::uint8_t> config; // AudioSpecificConfig, from the esds
	std::uint32_t timescale = 0;      // of the track's sample times
	std::uint64_t priming = 0;        // encoder delay to drop (edit list), in timescale units
};

// Reads the AAC track of a fragmented MP4 (YouTube's DASH m4a: ftyp, moov,
// then moof/mdat pairs) while it is still arriving.  Packets are handed out
// as soon as their bytes are in, without waiting for the whole mdat.
class AacStream
{
public:
	using PacketSink = std::function<void(const std::uint8_t* data, std::size_t size)>;

private:
	struct Sample
	{
		std::uint64_t offset = 0;
		std::uint32_t size = 0;
	};

	std::vector<std::uint8_t> buffer_;
	std::uint64_t base_ = 0;     // file offset of buffer_[0]
	std::uint64_t position_ = 0; // file offset of the next top-level box
	bool restIsData_ = false;    // a size-0 box runs to the end of the file
	std::uint32_t trackId_ = 0;
	std::uint32_t defaultSampleSize_ = 0; // from the trex
	bool haveInfo_ = false;
	AacInfo info_;
	std::deque<Sample> samples_; // known samples whose bytes have not all arrived
	std::size_t packets_ = 0;
	std::string error_;

	bool process(const PacketSink& sink);
	bool emitReady(const PacketSink& sink);
	bool parseMoov(std::size_t start, std::size_t end);
	bool parseMoof(std::uint64_t moofOffset, std::uint64_t moofEnd, std::size_t start, std::size_t end);

public:
	// Adds the next bytes of the file.  Packets go to sink in order (only
	// after info() is known).  Returns false once the file is unusable.
	bool feed(const std::uint8_t* data, std::size_t size, const PacketSink& sink);
	// Call when the file is complete: false if it had no usable AAC audio.
	bool finish(std::string& error) const;

	bool hasInfo() const { return haveInfo_; }
	const AacInfo& info() const { return info_; }
	std::size_t packetCount() const { return packets_; }
	const std::string& error() const { return error_; }
};

// True if the file starts like an MP4 (an ftyp box).  Needs 8 bytes.
bool LooksLikeMp4(const std::uint8_t* data, std::size_t size);
}
