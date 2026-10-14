/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "webm.hpp"
#include <cstring>

namespace WebM
{
namespace
{
constexpr std::uint32_t ID_EBML = 0x1A45DFA3;
constexpr std::uint32_t ID_SEGMENT = 0x18538067;
constexpr std::uint32_t ID_TRACKS = 0x1654AE6B;
constexpr std::uint32_t ID_TRACK_ENTRY = 0xAE;
constexpr std::uint32_t ID_TRACK_NUMBER = 0xD7;
constexpr std::uint32_t ID_CODEC_ID = 0x86;
constexpr std::uint32_t ID_CODEC_PRIVATE = 0x63A2;
constexpr std::uint32_t ID_AUDIO = 0xE1;
constexpr std::uint32_t ID_CHANNELS = 0x9F;
constexpr std::uint32_t ID_CLUSTER = 0x1F43B675;
constexpr std::uint32_t ID_SIMPLE_BLOCK = 0xA3;
constexpr std::uint32_t ID_BLOCK_GROUP = 0xA0;
constexpr std::uint32_t ID_BLOCK = 0xA1;
// Bytes of already parsed data kept before they are discarded.
constexpr std::size_t COMPACT_THRESHOLD = 1 << 20;

enum class Read { Ok, NeedMore, Invalid };

// Variable-length integer.  IDs keep their marker bit, sizes drop it.
Read vint(const std::uint8_t* data, std::size_t available, std::size_t& length, std::uint64_t& value, bool keepMarker,
	bool* allOnes = nullptr)
{
	if (available == 0) return Read::NeedMore;
	const std::uint8_t first = data[0];
	length = 1;
	while (length <= 8 && !(first & (0x80 >> (length - 1)))) ++length;
	if (length > 8) return Read::Invalid;
	if (length > available) return Read::NeedMore;

	value = keepMarker ? first : (first & (0xFFu >> length));
	bool ones = !keepMarker && value == (0xFFu >> length);
	for (std::size_t index = 1; index < length; ++index)
	{
		value = (value << 8) | data[index];
		if (data[index] != 0xFF) ones = false;
	}
	if (allOnes) *allOnes = ones;
	return Read::Ok;
}

struct Header
{
	std::uint32_t id = 0;
	std::uint64_t size = 0;
	bool unknownSize = false;
	std::size_t headerLength = 0;
};

Read header(const std::uint8_t* data, std::size_t available, Header& result)
{
	std::size_t idLength = 0;
	std::size_t sizeLength = 0;
	std::uint64_t id = 0;
	Read status = vint(data, available, idLength, id, true);
	if (status != Read::Ok) return status;
	if (idLength > 4) return Read::Invalid;
	status = vint(data + idLength, available - idLength, sizeLength, result.size, false, &result.unknownSize);
	if (status != Read::Ok) return status;
	result.id = static_cast<std::uint32_t>(id);
	result.headerLength = idLength + sizeLength;
	return Read::Ok;
}

std::uint64_t unsignedValue(const std::uint8_t* data, std::size_t size)
{
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < size && index < 8; ++index) value = (value << 8) | data[index];
	return value;
}

bool isClusterBoundary(std::uint32_t id)
{
	// Segment children that may follow an unknown-size cluster.
	return id == ID_CLUSTER || id == 0x1C53BB6B /* Cues */ || id == 0x1254C367 /* Tags */ || id == 0x1941A469 /* Attachments */ ||
		id == 0x1043A770 /* Chapters */ || id == 0x114D9B74 /* SeekHead */ || id == 0x1549A966 /* Info */ || id == ID_TRACKS;
}
}

bool OpusStream::parseTracks(std::size_t start, std::size_t end)
{
	const std::uint8_t* data = buffer_.data();
	std::size_t position = start;
	while (position < end && trackNumber_ == 0)
	{
		Header entry;
		if (header(data + position, end - position, entry) != Read::Ok || entry.unknownSize) return false;
		const std::size_t entryStart = position + entry.headerLength;
		const std::size_t entryEnd = static_cast<std::size_t>(std::min<std::uint64_t>(entryStart + entry.size, end));

		if (entry.id == ID_TRACK_ENTRY)
		{
			std::uint64_t number = 0;
			std::string codec;
			const std::uint8_t* codecPrivate = nullptr;
			std::size_t codecPrivateSize = 0;
			int channels = 0;

			std::size_t child = entryStart;
			while (child < entryEnd)
			{
				Header field;
				if (header(data + child, entryEnd - child, field) != Read::Ok || field.unknownSize) return false;
				const std::size_t fieldStart = child + field.headerLength;
				const std::size_t fieldSize = static_cast<std::size_t>(std::min<std::uint64_t>(field.size, entryEnd - fieldStart));
				switch (field.id)
				{
				case ID_TRACK_NUMBER: number = unsignedValue(data + fieldStart, fieldSize); break;
				case ID_CODEC_ID:
					codec.assign(reinterpret_cast<const char*>(data + fieldStart), fieldSize);
					codec = codec.c_str(); // drop padding zeros
					break;
				case ID_CODEC_PRIVATE:
					codecPrivate = data + fieldStart;
					codecPrivateSize = fieldSize;
					break;
				case ID_AUDIO:
				{
					std::size_t audio = fieldStart;
					while (audio < fieldStart + fieldSize)
					{
						Header setting;
						if (header(data + audio, fieldStart + fieldSize - audio, setting) != Read::Ok) break;
						const std::size_t settingStart = audio + setting.headerLength;
						if (setting.id == ID_CHANNELS) channels = static_cast<int>(unsignedValue(data + settingStart, static_cast<std::size_t>(setting.size)));
						audio = settingStart + static_cast<std::size_t>(setting.size);
					}
					break;
				}
				default: break;
				}
				child = fieldStart + fieldSize;
			}

			if (codec == "A_OPUS" && number != 0)
			{
				trackNumber_ = number;
				// OpusHead: magic, version, channels, pre-skip, input rate, gain, mapping.
				if (codecPrivate && codecPrivateSize >= 19 && std::memcmp(codecPrivate, "OpusHead", 8) == 0)
				{
					info_.channels = codecPrivate[9];
					info_.preSkip = codecPrivate[10] | (codecPrivate[11] << 8);
					info_.outputGain = static_cast<std::int16_t>(codecPrivate[16] | (codecPrivate[17] << 8));
				}
				else
				{
					info_.channels = channels;
				}
			}
		}
		position = entryEnd;
	}

	if (trackNumber_ == 0)
	{
		error_ = "the WebM file has no Opus audio track";
		return false;
	}
	if (info_.channels < 1 || info_.channels > 2)
	{
		error_ = "unsupported Opus channel count " + std::to_string(info_.channels);
		return false;
	}
	haveInfo_ = true;
	return true;
}

bool OpusStream::readBlock(std::size_t start, std::size_t end, const PacketSink& sink)
{
	const std::uint8_t* data = buffer_.data();
	std::size_t position = start;
	std::size_t length = 0;
	std::uint64_t number = 0;
	if (vint(data + position, end - position, length, number, false) != Read::Ok) return false;
	position += length;
	if (number != trackNumber_) return true;
	if (position + 3 > end) return false;
	const std::uint8_t flags = data[position + 2];
	position += 3;
	const int lacing = (flags >> 1) & 3;

	auto emit = [&](std::size_t offset, std::size_t size)
	{
		++packets_;
		sink(data + offset, size);
	};

	if (lacing == 0)
	{
		emit(position, end - position);
		return true;
	}

	if (position >= end) return false;
	const std::size_t count = static_cast<std::size_t>(data[position++]) + 1;
	std::vector<std::uint64_t> sizes(count, 0);

	if (lacing == 1) // Xiph
	{
		for (std::size_t index = 0; index + 1 < count; ++index)
		{
			std::uint64_t size = 0;
			std::uint8_t byte = 0;
			do
			{
				if (position >= end) return false;
				byte = data[position++];
				size += byte;
			} while (byte == 0xFF);
			sizes[index] = size;
		}
	}
	else if (lacing == 3) // EBML: first size, then signed differences
	{
		std::uint64_t first = 0;
		if (vint(data + position, end - position, length, first, false) != Read::Ok) return false;
		position += length;
		sizes[0] = first;
		for (std::size_t index = 1; index + 1 < count; ++index)
		{
			std::uint64_t raw = 0;
			if (vint(data + position, end - position, length, raw, false) != Read::Ok) return false;
			position += length;
			const std::int64_t bias = (std::int64_t(1) << (7 * length - 1)) - 1;
			const std::int64_t size = static_cast<std::int64_t>(sizes[index - 1]) + static_cast<std::int64_t>(raw) - bias;
			if (size < 0) return false;
			sizes[index] = static_cast<std::uint64_t>(size);
		}
	}

	const std::uint64_t remaining = end - position;
	if (lacing == 2) // fixed
	{
		if (remaining % count != 0) return false;
		for (auto& size : sizes) size = remaining / count;
	}
	else
	{
		std::uint64_t used = 0;
		for (std::size_t index = 0; index + 1 < count; ++index) used += sizes[index];
		if (used > remaining) return false;
		sizes[count - 1] = remaining - used;
	}

	for (std::uint64_t size : sizes)
	{
		emit(position, static_cast<std::size_t>(size));
		position += static_cast<std::size_t>(size);
	}
	return true;
}

bool OpusStream::process(const PacketSink& sink)
{
	for (;;)
	{
		// Leave finished containers.
		while (!levels_.empty() && !levels_.back().unknownSize && position_ >= levels_.back().end) levels_.pop_back();

		const std::uint64_t available = base_ + buffer_.size();
		if (position_ >= available) return true;

		const std::size_t offset = static_cast<std::size_t>(position_ - base_);
		Header element;
		const Read status = header(buffer_.data() + offset, buffer_.size() - offset, element);
		if (status == Read::NeedMore) return true;
		if (status == Read::Invalid)
		{
			error_ = "the audio is not a valid WebM file";
			return false;
		}

		const std::uint64_t dataStart = position_ + element.headerLength;
		const std::uint64_t dataEnd = dataStart + element.size;
		const std::uint32_t parent = levels_.empty() ? 0 : levels_.back().id;

		// Top level: the EBML header, then the segment.
		if (parent == 0)
		{
			if (position_ == 0 && element.id != ID_EBML)
			{
				error_ = "the audio is not a WebM file";
				return false;
			}
			if (element.id == ID_SEGMENT)
			{
				levels_.push_back(Level { ID_SEGMENT, dataEnd, element.unknownSize });
				position_ = dataStart;
				continue;
			}
		}

		if (parent == ID_CLUSTER && levels_.back().unknownSize && isClusterBoundary(element.id))
		{
			levels_.pop_back(); // the element belongs to the segment
			continue;
		}

		if (parent == ID_SEGMENT && element.id == ID_CLUSTER)
		{
			if (!haveInfo_)
			{
				error_ = "no Opus audio track before the first cluster";
				return false;
			}
			levels_.push_back(Level { ID_CLUSTER, dataEnd, element.unknownSize });
			position_ = dataStart;
			continue;
		}

		if (parent == ID_CLUSTER && element.id == ID_BLOCK_GROUP)
		{
			levels_.push_back(Level { ID_BLOCK_GROUP, dataEnd, element.unknownSize });
			position_ = dataStart;
			continue;
		}

		const bool needsData = (parent == ID_SEGMENT && element.id == ID_TRACKS) ||
			(parent == ID_CLUSTER && element.id == ID_SIMPLE_BLOCK) || (parent == ID_BLOCK_GROUP && element.id == ID_BLOCK);
		if (element.unknownSize)
		{
			error_ = "unsupported unknown-size WebM element";
			return false;
		}
		if (needsData)
		{
			if (dataEnd > available) return true; // wait for the whole element
			const std::size_t start = static_cast<std::size_t>(dataStart - base_);
			const std::size_t end = static_cast<std::size_t>(dataEnd - base_);
			const bool ok = element.id == ID_TRACKS ? parseTracks(start, end) : readBlock(start, end, sink);
			if (!ok)
			{
				if (error_.empty()) error_ = "damaged WebM audio";
				return false;
			}
		}
		// Anything else is skipped, even before its bytes arrive.
		position_ = dataEnd;
	}
}

bool OpusStream::feed(const std::uint8_t* data, std::size_t size, const PacketSink& sink)
{
	if (!error_.empty()) return false;
	buffer_.insert(buffer_.end(), data, data + size);
	if (!process(sink)) return false;

	// Drop what has been parsed (and skipped) already.
	const std::uint64_t consumed = std::min<std::uint64_t>(position_, base_ + buffer_.size()) - base_;
	if (consumed >= COMPACT_THRESHOLD)
	{
		buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
		base_ += consumed;
	}
	return true;
}

bool OpusStream::finish(std::string& error) const
{
	if (!error_.empty())
	{
		error = error_;
		return false;
	}
	if (!haveInfo_)
	{
		error = base_ + buffer_.size() == 0 ? "no audio was received" : "the WebM file has no Opus audio track";
		return false;
	}
	if (packets_ == 0)
	{
		error = "the WebM file has no audio";
		return false;
	}
	return true;
}
}
