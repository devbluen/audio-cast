/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace WebM
{
struct OpusInfo
{
	int channels = 0;
	int preSkip = 0;    // samples at 48 kHz to drop at the start
	int outputGain = 0; // Q7.8 dB, from the OpusHead
};

// Reads the Opus track of a WebM/Matroska file while it is still arriving.
// Handles all lacing modes and unknown-size segments/clusters.
class OpusStream
{
public:
	using PacketSink = std::function<void(const std::uint8_t* data, std::size_t size)>;

private:
	struct Level
	{
		std::uint32_t id = 0;
		std::uint64_t end = 0;
		bool unknownSize = false;
	};

	std::vector<std::uint8_t> buffer_;
	std::uint64_t base_ = 0;     // file offset of buffer_[0]
	std::uint64_t position_ = 0; // file offset of the next element
	std::vector<Level> levels_;
	std::uint64_t trackNumber_ = 0;
	bool haveInfo_ = false;
	OpusInfo info_;
	std::size_t packets_ = 0;
	std::string error_;

	bool process(const PacketSink& sink);
	bool parseTracks(std::size_t start, std::size_t end);
	bool readBlock(std::size_t start, std::size_t end, const PacketSink& sink);

public:
	// Adds the next bytes of the file.  Packets go to sink in order (only
	// after info() is known).  Returns false once the file is unusable.
	bool feed(const std::uint8_t* data, std::size_t size, const PacketSink& sink);
	// Call when the file is complete: false if it had no usable Opus audio.
	bool finish(std::string& error) const;

	bool hasInfo() const { return haveInfo_; }
	const OpusInfo& info() const { return info_; }
	std::size_t packetCount() const { return packets_; }
	const std::string& error() const { return error_; }
};
}
