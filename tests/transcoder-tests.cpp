/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 *
 *  Builds WebM files with real Opus audio (no lacing, Xiph lacing, EBML
 *  lacing, unknown-size cluster) and fragmented MP4 files with real AAC
 *  audio (AAC-LC, HE-AAC, mono, several fragments) and runs them through the
 *  built-in transcoder.
 */

#include "decoder.hpp"
#include "mp3.hpp"
#include "mp4.hpp"
#include "transcoder.hpp"
#include "webm.hpp"
#include <aacenc_lib.h>
#include <opus.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
int failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			++failures; \
		} \
	} while (0)

using Bytes = std::vector<std::uint8_t>;

void append(Bytes& target, const Bytes& data)
{
	target.insert(target.end(), data.begin(), data.end());
}

Bytes id(std::uint32_t value)
{
	Bytes bytes;
	for (int shift = 24; shift >= 0; shift -= 8)
	{
		const std::uint8_t byte = static_cast<std::uint8_t>(value >> shift);
		if (!bytes.empty() || byte != 0) bytes.push_back(byte);
	}
	return bytes;
}

// Size as an 8-byte vint (or "unknown").
Bytes size(std::uint64_t value, bool unknown = false)
{
	Bytes bytes { 0x01 };
	for (int shift = 48; shift >= 0; shift -= 8) bytes.push_back(unknown ? 0xFF : static_cast<std::uint8_t>(value >> shift));
	return bytes;
}

Bytes element(std::uint32_t elementId, const Bytes& payload, bool unknownSize = false)
{
	Bytes bytes = id(elementId);
	append(bytes, size(payload.size(), unknownSize));
	append(bytes, payload);
	return bytes;
}

Bytes uint(std::uint32_t elementId, std::uint64_t value)
{
	Bytes payload;
	for (int shift = 56; shift >= 0; shift -= 8) payload.push_back(static_cast<std::uint8_t>(value >> shift));
	return element(elementId, payload);
}

Bytes text(std::uint32_t elementId, const std::string& value)
{
	return element(elementId, Bytes(value.begin(), value.end()));
}

std::vector<Bytes> encodeSine(int channels, double seconds)
{
	int error = 0;
	OpusEncoder* encoder = opus_encoder_create(48000, channels, OPUS_APPLICATION_AUDIO, &error);
	std::vector<Bytes> packets;
	const int frameSize = 960; // 20 ms
	std::vector<float> pcm(static_cast<std::size_t>(frameSize) * channels);
	const int frames = static_cast<int>(seconds * 48000 / frameSize);
	long sample = 0;
	for (int frame = 0; frame < frames; ++frame)
	{
		for (int index = 0; index < frameSize; ++index, ++sample)
		{
			const float value = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * sample / 48000.0));
			for (int channel = 0; channel < channels; ++channel) pcm[index * channels + channel] = value;
		}
		unsigned char buffer[4000];
		const int bytes = opus_encode_float(encoder, pcm.data(), frameSize, buffer, sizeof(buffer));
		packets.emplace_back(buffer, buffer + bytes);
	}
	opus_encoder_destroy(encoder);
	return packets;
}

enum class Lacing { None, Xiph, Ebml };

Bytes makeWebm(const std::vector<Bytes>& packets, int channels, Lacing lacing, bool unknownCluster)
{
	Bytes file = element(0x1A45DFA3, text(0x4282, "webm"));

	Bytes head = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, static_cast<std::uint8_t>(channels), 0x38, 0x01, 0x80, 0xBB, 0, 0, 0, 0, 0 };
	Bytes entry = uint(0xD7, 1);
	append(entry, uint(0x83, 2));
	append(entry, text(0x86, "A_OPUS"));
	append(entry, element(0x63A2, head));
	append(entry, element(0xE1, uint(0x9F, static_cast<std::uint64_t>(channels))));
	Bytes segment = element(0x1654AE6B, element(0xAE, entry));

	Bytes cluster = uint(0xE7, 0);
	for (std::size_t index = 0; index < packets.size();)
	{
		Bytes block = { 0x81, 0, 0 }; // track 1, timecode 0
		if (lacing == Lacing::None || index + 1 >= packets.size())
		{
			block.push_back(0x80);
			append(block, packets[index]);
			++index;
		}
		else
		{
			const Bytes& first = packets[index];
			const Bytes& second = packets[index + 1];
			if (lacing == Lacing::Xiph)
			{
				block.push_back(0x80 | (1 << 1));
				block.push_back(1); // two frames
				std::size_t value = first.size();
				while (value >= 255)
				{
					block.push_back(255);
					value -= 255;
				}
				block.push_back(static_cast<std::uint8_t>(value));
			}
			else
			{
				block.push_back(0x80 | (3 << 1));
				block.push_back(1);
				block.push_back(0x40 | static_cast<std::uint8_t>(first.size() >> 8)); // 2-byte vint
				block.push_back(static_cast<std::uint8_t>(first.size() & 0xFF));
			}
			append(block, first);
			append(block, second);
			index += 2;
		}
		append(cluster, element(0xA3, block));
	}
	append(segment, element(0x1F43B675, cluster, unknownCluster));
	append(file, element(0x18538067, segment, unknownCluster));
	return file;
}

std::size_t countPackets(const Bytes& webm, std::size_t chunk, WebM::OpusStream& stream)
{
	std::size_t packets = 0;
	for (std::size_t offset = 0; offset < webm.size(); offset += chunk)
	{
		const std::size_t size = std::min(chunk, webm.size() - offset);
		stream.feed(webm.data() + offset, size, [&](const std::uint8_t*, std::size_t) { ++packets; });
	}
	return packets;
}

// Converts a whole file and checks the MP3 that comes out.
void checkMp3(const char* name, const Bytes& audio, std::size_t packets)
{
	std::atomic<bool> cancel { false };
	Bytes mp3;
	std::string error;
	const bool ok = TranscodeToMp3(audio, 128, true, cancel, mp3, error);
	if (!ok) std::fprintf(stderr, "%s: %s\n", name, error.c_str());
	CHECK(ok);

	std::vector<Mp3::Frame> frames;
	CHECK(Mp3::indexFrames(mp3, frames, error));
	const auto duration = Mp3::framesToMs(frames.size());
	std::printf("%s: %zu packets -> %zu MP3 frames, %llu ms\n", name, packets, frames.size(),
		static_cast<unsigned long long>(duration));
	CHECK(duration >= 1950 && duration <= 2100);
	for (const Mp3::Frame& frame : frames)
	{
		CHECK(frame.size == 384);
		// main_data_begin (first 9 bits of the side information) is 0: no
		// frame borrows bits from the previous one, so cuts are clean.
		const std::uint8_t* side = mp3.data() + frame.offset + 4;
		CHECK(((side[0] << 1) | (side[1] >> 7)) == 0);
	}

	// FrameSplitter cuts the same frames from a stream that arrives in pieces.
	Mp3::FrameSplitter splitter;
	std::size_t split = 0;
	for (std::size_t offset = 0; offset < mp3.size(); offset += 100)
	{
		splitter.push(mp3.data() + offset, std::min<std::size_t>(100, mp3.size() - offset), [&](const std::uint8_t*, std::size_t size)
		{
			CHECK(size == 384);
			++split;
		});
	}
	CHECK(split == frames.size());
}

void checkTranscode(const char* name, int channels, Lacing lacing, bool unknownCluster)
{
	const auto packets = encodeSine(channels, 2.0);
	const Bytes webm = makeWebm(packets, channels, lacing, unknownCluster);

	// The same packets come out whether the file arrives at once or in pieces.
	for (std::size_t chunk : { webm.size(), std::size_t(1), std::size_t(7), std::size_t(4096) })
	{
		WebM::OpusStream stream;
		std::string error;
		CHECK(countPackets(webm, chunk, stream) == packets.size());
		CHECK(stream.finish(error));
		CHECK(stream.info().channels == channels);
		CHECK(stream.info().preSkip == 312);
	}
	checkMp3(name, webm, packets.size());
}

// --- AAC in fragmented MP4 ---------------------------------------------------

struct AacEncoded
{
	std::vector<Bytes> packets;
	Bytes config; // AudioSpecificConfig
	std::uint32_t delay = 0;
};

AacEncoded encodeAacSine(int aot, int channels, int sampleRate, int bitrate, double seconds)
{
	AacEncoded result;
	HANDLE_AACENCODER encoder = nullptr;
	if (aacEncOpen(&encoder, 0, static_cast<UINT>(channels)) != AACENC_OK) return result;
	aacEncoder_SetParam(encoder, AACENC_AOT, static_cast<UINT>(aot));
	aacEncoder_SetParam(encoder, AACENC_SAMPLERATE, static_cast<UINT>(sampleRate));
	aacEncoder_SetParam(encoder, AACENC_CHANNELMODE, channels == 2 ? MODE_2 : MODE_1);
	aacEncoder_SetParam(encoder, AACENC_BITRATE, static_cast<UINT>(bitrate));
	aacEncoder_SetParam(encoder, AACENC_TRANSMUX, TT_MP4_RAW);
	if (aacEncEncode(encoder, nullptr, nullptr, nullptr, nullptr) != AACENC_OK)
	{
		aacEncClose(&encoder);
		return result;
	}
	AACENC_InfoStruct info {};
	aacEncInfo(encoder, &info);
	result.config.assign(info.confBuf, info.confBuf + info.confSize);
	result.delay = info.nDelay;

	const long total = static_cast<long>(seconds * sampleRate);
	std::vector<INT_PCM> pcm(static_cast<std::size_t>(total) * channels);
	for (long sample = 0; sample < total; ++sample)
	{
		const double value = 0.3 * std::sin(2.0 * 3.14159265358979 * 440.0 * sample / sampleRate);
		for (int channel = 0; channel < channels; ++channel) pcm[sample * channels + channel] = static_cast<INT_PCM>(value * 32767.0);
	}

	std::size_t consumed = 0;
	std::vector<UCHAR> output(8192);
	for (;;)
	{
		const bool flush = consumed >= pcm.size();
		void* inPointer = pcm.data() + consumed;
		INT inId = IN_AUDIO_DATA;
		INT inSize = static_cast<INT>((pcm.size() - consumed) * sizeof(INT_PCM));
		INT inElementSize = sizeof(INT_PCM);
		AACENC_BufDesc in { 1, &inPointer, &inId, &inSize, &inElementSize };
		void* outPointer = output.data();
		INT outId = OUT_BITSTREAM_DATA;
		INT outSize = static_cast<INT>(output.size());
		INT outElementSize = 1;
		AACENC_BufDesc out { 1, &outPointer, &outId, &outSize, &outElementSize };
		AACENC_InArgs inArgs {};
		inArgs.numInSamples = flush ? -1 : static_cast<INT>(std::min<std::size_t>(pcm.size() - consumed, info.frameLength * channels));
		AACENC_OutArgs outArgs {};
		const AACENC_ERROR status = aacEncEncode(encoder, flush ? nullptr : &in, &out, &inArgs, &outArgs);
		if (status != AACENC_OK) break; // AACENC_ENCODE_EOF once flushed
		consumed += static_cast<std::size_t>(outArgs.numInSamples);
		if (outArgs.numOutBytes > 0) result.packets.emplace_back(output.begin(), output.begin() + outArgs.numOutBytes);
	}
	aacEncClose(&encoder);
	return result;
}

Bytes be(std::uint64_t value, int bytes)
{
	Bytes result;
	for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) result.push_back(static_cast<std::uint8_t>(value >> shift));
	return result;
}

Bytes box(const char* type, const Bytes& payload)
{
	Bytes result = be(8 + payload.size(), 4);
	result.insert(result.end(), type, type + 4);
	append(result, payload);
	return result;
}

Bytes fullBox(const char* type, std::uint32_t versionAndFlags, const Bytes& payload)
{
	Bytes body = be(versionAndFlags, 4);
	append(body, payload);
	return box(type, body);
}

Bytes concat(std::initializer_list<Bytes> parts)
{
	Bytes result;
	for (const Bytes& part : parts) append(result, part);
	return result;
}

// MPEG-4 descriptor with a one-byte length.
Bytes descriptor(std::uint8_t tag, const Bytes& payload)
{
	Bytes result { tag, static_cast<std::uint8_t>(payload.size()) };
	append(result, payload);
	return result;
}

Bytes trak(std::uint32_t id, const char* handler, const Bytes& stsd, std::uint32_t timescale, std::uint32_t mediaTime)
{
	const Bytes tkhd = fullBox("tkhd", 7, concat({ be(0, 8), be(id, 4), Bytes(64, 0) }));
	const Bytes elst = fullBox("elst", 0, concat({ be(1, 4), be(0, 4), be(mediaTime, 4), be(0x10000, 4) }));
	const Bytes mdhd = fullBox("mdhd", 0, concat({ be(0, 8), be(timescale, 4), be(0, 4), be(0x55C40000, 4) }));
	const Bytes hdlr = fullBox("hdlr", 0, concat({ be(0, 4), Bytes(handler, handler + 4), Bytes(13, 0) }));
	const Bytes minf = box("minf", box("stbl", fullBox("stsd", 0, stsd)));
	return box("trak", concat({ tkhd, box("edts", elst), box("mdia", concat({ mdhd, hdlr, minf })) }));
}

// Like YouTube's DASH m4a: ftyp, moov (a video track first, then the audio
// track 2), sidx, then moof/mdat pairs of perFragment samples.
Bytes makeMp4(const AacEncoded& aac, int channels, int sampleRate, std::size_t perFragment)
{
	Bytes esds = concat({ be(2, 2), Bytes { 0 } });
	append(esds, descriptor(0x04, concat({ Bytes { 0x40, 0x15 }, Bytes(11, 0), descriptor(0x05, aac.config) })));
	append(esds, descriptor(0x06, Bytes { 0x02 }));
	const Bytes mp4a = box("mp4a", concat({ Bytes(6, 0), be(1, 2), Bytes(8, 0), be(static_cast<std::uint32_t>(channels), 2), be(16, 2),
		be(0, 4), be(static_cast<std::uint32_t>(sampleRate) << 16, 4), fullBox("esds", 0, descriptor(0x03, esds)) }));

	const Bytes video = trak(1, "vide", be(0, 4), 90000, 0);
	const Bytes audio = trak(2, "soun", concat({ be(1, 4), mp4a }), static_cast<std::uint32_t>(sampleRate), aac.delay);
	const Bytes mvex = box("mvex", concat({ fullBox("trex", 0, concat({ be(1, 4), be(1, 4), be(0, 12) })),
		fullBox("trex", 0, concat({ be(2, 4), be(1, 4), be(0, 12) })) }));
	Bytes file = concat({ box("ftyp", Bytes { 'd', 'a', 's', 'h', 0, 0, 0, 0 }),
		box("moov", concat({ fullBox("mvhd", 0, Bytes(96, 0)), video, audio, mvex })), fullBox("sidx", 0, Bytes(24, 0)) });

	std::uint32_t sequence = 1;
	for (std::size_t first = 0; first < aac.packets.size(); first += perFragment)
	{
		const std::size_t count = std::min(perFragment, aac.packets.size() - first);
		Bytes sizes;
		Bytes data;
		for (std::size_t index = first; index < first + count; ++index)
		{
			append(sizes, be(aac.packets[index].size(), 4));
			append(data, aac.packets[index]);
		}
		// The trun's data offset counts from the moof (default-base-is-moof)
		// and points just past the mdat header.
		auto moof = [&](std::uint32_t dataOffset)
		{
			const Bytes videoTraf = box("traf", concat({ fullBox("tfhd", 0x020000, be(1, 4)), fullBox("trun", 0, be(0, 4)) }));
			const Bytes audioTraf = box("traf", concat({ fullBox("tfhd", 0x020000, be(2, 4)),
				fullBox("trun", 0x000201, concat({ be(count, 4), be(dataOffset, 4), sizes })) }));
			return box("moof", concat({ fullBox("mfhd", 0, be(sequence, 4)), videoTraf, audioTraf }));
		};
		append(file, moof(static_cast<std::uint32_t>(moof(0).size() + 8)));
		append(file, box("mdat", data));
		++sequence;
	}
	return file;
}

void checkAac(const char* name, int aot, int channels, std::size_t perFragment)
{
	const int sampleRate = 44100;
	const AacEncoded aac = encodeAacSine(aot, channels, sampleRate, aot == 2 ? 128000 : 48000, 2.0);
	CHECK(!aac.packets.empty());
	CHECK(!aac.config.empty());
	if (aac.packets.empty()) return;
	const Bytes mp4 = makeMp4(aac, channels, sampleRate, perFragment);

	// The same packets come out whether the file arrives at once or in pieces.
	for (std::size_t chunk : { mp4.size(), std::size_t(1), std::size_t(7), std::size_t(4096) })
	{
		Mp4::AacStream stream;
		std::size_t packets = 0;
		bool same = true;
		for (std::size_t offset = 0; offset < mp4.size(); offset += chunk)
		{
			stream.feed(mp4.data() + offset, std::min(chunk, mp4.size() - offset), [&](const std::uint8_t* data, std::size_t size)
			{
				const Bytes& expected = aac.packets[std::min(packets, aac.packets.size() - 1)];
				same = same && size == expected.size() && std::equal(data, data + size, expected.begin());
				++packets;
			});
		}
		std::string error;
		CHECK(stream.finish(error));
		CHECK(packets == aac.packets.size());
		CHECK(same);
		CHECK(stream.info().config == aac.config);
		CHECK(stream.info().timescale == static_cast<std::uint32_t>(sampleRate));
		CHECK(stream.info().priming == aac.delay);
	}

	// The decoder recognises the MP4 by itself and drops the encoder delay.
	AudioDecoder decoder;
	std::size_t frames = 0;
	CHECK(decoder.feed(mp4.data(), mp4.size(), [&](const float*, std::size_t count) { frames += count; }));
	std::string error;
	CHECK(decoder.finish(error));
	CHECK(std::string(decoder.codec()) == "AAC");
	CHECK(decoder.sampleRate() == sampleRate);
	const double seconds = double(frames) / sampleRate;
	std::printf("%s: %zu samples decoded (%.3f s)\n", name, frames, seconds);
	CHECK(seconds >= 1.95 && seconds <= 2.1);

	checkMp3(name, mp4, aac.packets.size());
}
}

int main()
{
	checkTranscode("stereo", 2, Lacing::None, false);
	checkTranscode("mono, Xiph lacing", 1, Lacing::Xiph, false);
	checkTranscode("stereo, EBML lacing, unknown sizes", 2, Lacing::Ebml, true);
	checkAac("AAC-LC stereo", 2, 2, 40);
	checkAac("AAC-LC mono, one fragment", 2, 1, 1000);
	checkAac("HE-AAC stereo", 5, 2, 25);

	std::string error;
	WebM::OpusStream garbage;
	const Bytes junk(100, 0x42);
	garbage.feed(junk.data(), junk.size(), [](const std::uint8_t*, std::size_t) {});
	CHECK(!garbage.finish(error));

	AudioDecoder unknown;
	CHECK(!unknown.feed(junk.data(), junk.size(), [](const float*, std::size_t) {}));
	CHECK(!unknown.finish(error));

	if (failures == 0) std::puts("transcoder tests passed");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
