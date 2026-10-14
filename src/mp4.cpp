/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "mp4.hpp"
#include <algorithm>
#include <cstring>

namespace Mp4
{
namespace
{
constexpr std::uint32_t fourcc(const char (&name)[5])
{
	return (std::uint32_t(std::uint8_t(name[0])) << 24) | (std::uint32_t(std::uint8_t(name[1])) << 16) |
		(std::uint32_t(std::uint8_t(name[2])) << 8) | std::uint32_t(std::uint8_t(name[3]));
}

constexpr std::uint32_t BOX_FTYP = fourcc("ftyp");
constexpr std::uint32_t BOX_MOOV = fourcc("moov");
constexpr std::uint32_t BOX_MOOF = fourcc("moof");
constexpr std::uint32_t BOX_TRAK = fourcc("trak");
constexpr std::uint32_t BOX_TKHD = fourcc("tkhd");
constexpr std::uint32_t BOX_EDTS = fourcc("edts");
constexpr std::uint32_t BOX_ELST = fourcc("elst");
constexpr std::uint32_t BOX_MDIA = fourcc("mdia");
constexpr std::uint32_t BOX_MDHD = fourcc("mdhd");
constexpr std::uint32_t BOX_HDLR = fourcc("hdlr");
constexpr std::uint32_t BOX_MINF = fourcc("minf");
constexpr std::uint32_t BOX_STBL = fourcc("stbl");
constexpr std::uint32_t BOX_STSD = fourcc("stsd");
constexpr std::uint32_t BOX_MP4A = fourcc("mp4a");
constexpr std::uint32_t BOX_ESDS = fourcc("esds");
constexpr std::uint32_t BOX_MVEX = fourcc("mvex");
constexpr std::uint32_t BOX_TREX = fourcc("trex");
constexpr std::uint32_t BOX_TRAF = fourcc("traf");
constexpr std::uint32_t BOX_TFHD = fourcc("tfhd");
constexpr std::uint32_t BOX_TRUN = fourcc("trun");
constexpr std::uint32_t HANDLER_SOUND = fourcc("soun");

// Bytes of already parsed data kept before they are discarded.
constexpr std::size_t COMPACT_THRESHOLD = 1 << 20;
// moov and moof are read whole; anything bigger is not a song's audio.
constexpr std::uint64_t MAX_HEADER_BOX = 16u << 20;
constexpr std::uint32_t MAX_SAMPLE_SIZE = 1u << 20;
constexpr std::size_t MAX_QUEUED_SAMPLES = 100000;

std::uint32_t be16(const std::uint8_t* data) { return (std::uint32_t(data[0]) << 8) | data[1]; }
std::uint32_t be32(const std::uint8_t* data) { return (be16(data) << 16) | be16(data + 2); }
std::uint64_t be64(const std::uint8_t* data) { return (std::uint64_t(be32(data)) << 32) | be32(data + 4); }

enum class Read { Ok, NeedMore, Invalid };

struct Box
{
	std::uint32_t type = 0;
	std::uint64_t size = 0; // whole box; 0 = runs to the end of the file
	std::size_t headerLength = 0;
};

Read box(const std::uint8_t* data, std::size_t available, Box& result)
{
	if (available < 8) return Read::NeedMore;
	result.size = be32(data);
	result.type = be32(data + 4);
	result.headerLength = 8;
	if (result.size == 1)
	{
		if (available < 16) return Read::NeedMore;
		result.size = be64(data + 8);
		result.headerLength = 16;
	}
	if (result.size != 0 && result.size < result.headerLength) return Read::Invalid;
	return Read::Ok;
}

// Calls visit(type, payloadStart, payloadEnd) for each box in [start, end).
template <typename Visit>
bool children(const std::uint8_t* data, std::size_t start, std::size_t end, Visit&& visit)
{
	std::size_t position = start;
	while (end - position >= 8)
	{
		Box child;
		if (box(data + position, end - position, child) != Read::Ok) return false;
		const std::uint64_t size = child.size == 0 ? end - position : child.size;
		if (size > end - position) return false;
		const std::size_t childEnd = position + static_cast<std::size_t>(size);
		if (!visit(child.type, position + child.headerLength, childEnd)) return false;
		position = childEnd;
	}
	return true;
}

// MPEG-4 descriptor header (tag + 1..4 byte length).
bool descriptor(const std::uint8_t* data, std::size_t& position, std::size_t end, std::uint8_t& tag, std::size_t& length)
{
	if (position >= end) return false;
	tag = data[position++];
	length = 0;
	for (int index = 0; index < 4; ++index)
	{
		if (position >= end) return false;
		const std::uint8_t byte = data[position++];
		length = (length << 7) | (byte & 0x7F);
		if (!(byte & 0x80)) break;
	}
	return length <= end - position;
}

// The AudioSpecificConfig inside an esds box.
bool parseEsds(const std::uint8_t* data, std::size_t start, std::size_t end, std::vector<std::uint8_t>& config)
{
	std::size_t position = start + 4; // version, flags
	std::uint8_t tag = 0;
	std::size_t length = 0;
	if (!descriptor(data, position, end, tag, length) || tag != 0x03) return false; // ES_Descriptor
	end = position + length;
	if (end - position < 3) return false;
	position += 2; // ES_ID
	const std::uint8_t flags = data[position++];
	if (flags & 0x80) position += 2;                                  // dependsOn_ES_ID
	if ((flags & 0x40) && position < end) position += 1 + data[position]; // URL
	if (flags & 0x20) position += 2;                                  // OCR_ES_Id
	if (position >= end) return false;

	if (!descriptor(data, position, end, tag, length) || tag != 0x04) return false; // DecoderConfigDescriptor
	end = position + length;
	if (length < 13) return false;
	const std::uint8_t objectType = data[position];
	// MPEG-4 audio; MPEG-2 AAC Main, LC, SSR.
	if (objectType != 0x40 && (objectType < 0x66 || objectType > 0x68)) return false;
	position += 13;

	if (!descriptor(data, position, end, tag, length) || tag != 0x05 || length == 0) return false; // DecoderSpecificInfo
	config.assign(data + position, data + position + length);
	return true;
}

struct TrackInfo
{
	std::uint32_t id = 0;
	std::uint32_t timescale = 0;
	bool sound = false;
	std::int64_t mediaTime = -1;
	std::vector<std::uint8_t> config;
};

bool parseStsd(const std::uint8_t* data, std::size_t start, std::size_t end, TrackInfo& track)
{
	if (end - start < 8) return false;
	return children(data, start + 8, end, [&](std::uint32_t type, std::size_t entryStart, std::size_t entryEnd)
	{
		if (type != BOX_MP4A || !track.config.empty()) return true;
		// SampleEntry (8 bytes), then the AudioSampleEntry fields; QuickTime
		// sound descriptions version 1 and 2 add more.
		if (entryEnd - entryStart < 28) return false;
		const std::uint32_t version = be16(data + entryStart + 8);
		const std::size_t fields = 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);
		if (entryEnd - entryStart < fields) return false;
		return children(data, entryStart + fields, entryEnd, [&](std::uint32_t childType, std::size_t childStart, std::size_t childEnd)
		{
			if (childType == BOX_ESDS && track.config.empty()) parseEsds(data, childStart, childEnd, track.config);
			return true;
		});
	});
}

bool parseTrak(const std::uint8_t* data, std::size_t start, std::size_t end, TrackInfo& track)
{
	std::function<bool(std::uint32_t, std::size_t, std::size_t)> visit = [&](std::uint32_t type, std::size_t boxStart, std::size_t boxEnd)
	{
		const std::size_t size = boxEnd - boxStart;
		const bool version1 = size > 0 && data[boxStart] == 1;
		switch (type)
		{
		case BOX_MDIA:
		case BOX_MINF:
		case BOX_STBL:
		case BOX_EDTS:
			return children(data, boxStart, boxEnd, visit);
		case BOX_TKHD:
			if (size < (version1 ? 24u : 16u)) return false;
			track.id = be32(data + boxStart + (version1 ? 20 : 12));
			return true;
		case BOX_MDHD:
			if (size < (version1 ? 24u : 16u)) return false;
			track.timescale = be32(data + boxStart + (version1 ? 20 : 12));
			return true;
		case BOX_HDLR:
			if (size < 12) return false;
			track.sound = be32(data + boxStart + 8) == HANDLER_SOUND;
			return true;
		case BOX_ELST:
		{
			if (size < 8) return false;
			const std::uint32_t count = be32(data + boxStart + 4);
			const std::size_t entrySize = version1 ? 20 : 12;
			for (std::uint32_t index = 0; index < count && track.mediaTime < 0; ++index)
			{
				const std::size_t entry = boxStart + 8 + index * entrySize;
				if (entry + entrySize > boxEnd) return false;
				// An empty edit (media time -1) only delays the track.
				track.mediaTime = version1 ? static_cast<std::int64_t>(be64(data + entry + 8))
					: static_cast<std::int32_t>(be32(data + entry + 4));
			}
			return true;
		}
		case BOX_STSD:
			return parseStsd(data, boxStart, boxEnd, track);
		default:
			return true;
		}
	};
	return children(data, start, end, visit);
}
}

bool LooksLikeMp4(const std::uint8_t* data, std::size_t size)
{
	return size >= 8 && be32(data + 4) == BOX_FTYP;
}

bool AacStream::parseMoov(std::size_t start, std::size_t end)
{
	const std::uint8_t* data = buffer_.data();
	std::vector<TrackInfo> tracks;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> defaultSizes; // track ID, trex default sample size

	const bool parsed = children(data, start, end, [&](std::uint32_t type, std::size_t boxStart, std::size_t boxEnd)
	{
		if (type == BOX_TRAK)
		{
			tracks.emplace_back();
			return parseTrak(data, boxStart, boxEnd, tracks.back());
		}
		if (type == BOX_MVEX)
		{
			return children(data, boxStart, boxEnd, [&](std::uint32_t childType, std::size_t childStart, std::size_t childEnd)
			{
				if (childType != BOX_TREX) return true;
				if (childEnd - childStart < 24) return false;
				defaultSizes.emplace_back(be32(data + childStart + 4), be32(data + childStart + 16));
				return true;
			});
		}
		return true;
	});
	if (!parsed) return false;

	for (TrackInfo& track : tracks)
	{
		if (!track.sound || track.config.empty() || track.id == 0 || track.timescale == 0) continue;
		trackId_ = track.id;
		info_.config = std::move(track.config);
		info_.timescale = track.timescale;
		info_.priming = track.mediaTime > 0 ? static_cast<std::uint64_t>(track.mediaTime) : 0;
		for (const auto& entry : defaultSizes)
		{
			if (entry.first == trackId_) defaultSampleSize_ = entry.second;
		}
		haveInfo_ = true;
		return true;
	}
	error_ = "the MP4 file has no AAC audio track";
	return false;
}

bool AacStream::parseMoof(std::uint64_t moofOffset, std::uint64_t moofEnd, std::size_t start, std::size_t end)
{
	const std::uint8_t* data = buffer_.data();
	return children(data, start, end, [&](std::uint32_t type, std::size_t trafStart, std::size_t trafEnd)
	{
		if (type != BOX_TRAF) return true;

		bool ours = false;
		std::uint64_t base = moofOffset;
		std::uint32_t defaultSize = defaultSampleSize_;
		std::uint64_t next = 0; // where a trun without a data offset starts
		bool haveNext = false;

		return children(data, trafStart, trafEnd, [&](std::uint32_t childType, std::size_t boxStart, std::size_t boxEnd)
		{
			const std::size_t size = boxEnd - boxStart;
			if (childType == BOX_TFHD)
			{
				if (size < 8) return false;
				const std::uint32_t flags = be32(data + boxStart) & 0xFFFFFF;
				ours = be32(data + boxStart + 4) == trackId_;
				std::size_t position = boxStart + 8;
				auto field = [&](std::size_t bytes) -> bool
				{
					if (boxEnd - position < bytes) return false;
					position += bytes;
					return true;
				};
				if (flags & 0x000001)
				{
					if (!field(8)) return false;
					base = be64(data + position - 8);
				}
				if ((flags & 0x000002) && !field(4)) return false; // sample description index
				if ((flags & 0x000008) && !field(4)) return false; // default duration
				if (flags & 0x000010)
				{
					if (!field(4)) return false;
					defaultSize = be32(data + position - 4);
				}
				return true;
			}
			if (childType != BOX_TRUN || !ours) return true;

			if (size < 8) return false;
			const std::uint32_t flags = be32(data + boxStart) & 0xFFFFFF;
			const std::uint32_t count = be32(data + boxStart + 4);
			std::size_t position = boxStart + 8;
			std::uint64_t offset = haveNext ? next : base;
			if (flags & 0x000001)
			{
				if (boxEnd - position < 4) return false;
				offset = base + static_cast<std::int64_t>(static_cast<std::int32_t>(be32(data + position)));
				position += 4;
			}
			if (flags & 0x000004) position += 4; // first sample flags

			const std::size_t perSample = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0) + ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
			if (position > boxEnd || (perSample > 0 && count > (boxEnd - position) / perSample)) return false;
			if (samples_.size() + count > MAX_QUEUED_SAMPLES) return false;

			for (std::uint32_t index = 0; index < count; ++index)
			{
				if (flags & 0x100) position += 4; // duration
				std::uint32_t sampleSize = defaultSize;
				if (flags & 0x200)
				{
					sampleSize = be32(data + position);
					position += 4;
				}
				if (flags & 0x400) position += 4; // flags
				if (flags & 0x800) position += 4; // composition time offset

				if (sampleSize == 0 || sampleSize > MAX_SAMPLE_SIZE) return false;
				// Samples must come after the moof and in file order: the
				// bytes before them are discarded as the download goes on.
				const std::uint64_t floor = samples_.empty() ? moofEnd : std::max(moofEnd, samples_.back().offset + samples_.back().size);
				if (offset < floor)
				{
					error_ = "unsupported MP4 sample layout";
					return false;
				}
				samples_.push_back(Sample { offset, sampleSize });
				offset += sampleSize;
			}
			next = offset;
			haveNext = true;
			return true;
		});
	});
}

bool AacStream::emitReady(const PacketSink& sink)
{
	const std::uint64_t available = base_ + buffer_.size();
	while (!samples_.empty())
	{
		const Sample sample = samples_.front();
		if (sample.offset < base_)
		{
			error_ = "damaged MP4 audio";
			return false;
		}
		if (sample.offset + sample.size > available) break;
		samples_.pop_front();
		++packets_;
		sink(buffer_.data() + static_cast<std::size_t>(sample.offset - base_), sample.size);
	}
	return true;
}

bool AacStream::process(const PacketSink& sink)
{
	for (;;)
	{
		if (!emitReady(sink)) return false;
		if (restIsData_) return true;

		const std::uint64_t available = base_ + buffer_.size();
		if (position_ >= available) return true;

		const std::size_t offset = static_cast<std::size_t>(position_ - base_);
		Box current;
		const Read status = box(buffer_.data() + offset, buffer_.size() - offset, current);
		if (status == Read::NeedMore) return true;
		if (status == Read::Invalid)
		{
			error_ = "the audio is not a valid MP4 file";
			return false;
		}
		if (position_ == 0 && current.type != BOX_FTYP)
		{
			error_ = "the audio is not an MP4 file";
			return false;
		}

		const bool needsData = current.type == BOX_MOOV || current.type == BOX_MOOF;
		if (current.size == 0)
		{
			if (needsData)
			{
				error_ = "unsupported MP4 layout";
				return false;
			}
			restIsData_ = true; // an mdat that runs to the end
			continue;
		}

		const std::uint64_t end = position_ + current.size;
		if (needsData)
		{
			if (current.size > MAX_HEADER_BOX)
			{
				error_ = "damaged MP4 audio";
				return false;
			}
			if (end > available) return true; // wait for the whole box
			const std::size_t start = offset + current.headerLength;
			const std::size_t stop = static_cast<std::size_t>(end - base_);
			bool ok = false;
			if (current.type == BOX_MOOV)
			{
				ok = haveInfo_ || parseMoov(start, stop);
			}
			else if (!haveInfo_)
			{
				error_ = "no AAC audio track before the first fragment";
			}
			else
			{
				ok = parseMoof(position_, end, start, stop);
			}
			if (!ok)
			{
				if (error_.empty()) error_ = "damaged MP4 audio";
				return false;
			}
		}
		// Anything else (mdat, sidx, free...) is skipped, even before its
		// bytes arrive; the samples queued from the moof point into it.
		position_ = end;
	}
}

bool AacStream::feed(const std::uint8_t* data, std::size_t size, const PacketSink& sink)
{
	if (!error_.empty()) return false;
	buffer_.insert(buffer_.end(), data, data + size);
	if (!process(sink)) return false;

	// Drop what has been parsed (and skipped), but not samples still to come.
	const std::uint64_t available = base_ + buffer_.size();
	std::uint64_t keep = restIsData_ ? available : std::min(position_, available);
	if (!samples_.empty()) keep = std::min(keep, samples_.front().offset);
	const std::uint64_t consumed = keep - base_;
	if (consumed >= COMPACT_THRESHOLD)
	{
		buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
		base_ += consumed;
	}
	return true;
}

bool AacStream::finish(std::string& error) const
{
	if (!error_.empty())
	{
		error = error_;
		return false;
	}
	if (!haveInfo_)
	{
		error = base_ + buffer_.size() == 0 ? "no audio was received" : "the MP4 file has no AAC audio track";
		return false;
	}
	if (packets_ == 0)
	{
		error = "the MP4 file has no audio";
		return false;
	}
	return true;
}
}
