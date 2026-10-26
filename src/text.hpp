/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <cstddef>
#include <string>

namespace Text
{
struct Query
{
	enum class Kind { Invalid, Video, Search };
	Kind kind = Kind::Invalid;
	// Video ID for Kind::Video, cleaned search terms for Kind::Search.
	std::string value;
};

// Accepts a YouTube link (watch, youtu.be, shorts, embed, live, music), a bare
// video ID or free text, which becomes a YouTube search.  Links to other
// sites are rejected.
Query parseQuery(const std::string& input);
bool isVideoId(const std::string& value);

// Pawn scripts use Windows-1252 text unless audiocast_utf8 is enabled.
std::string utf8ToLatin1(const std::string& text);
std::string latin1ToUtf8(const std::string& text);

// A Windows code page from 1250 to 1258, or 0 for plain ASCII, for text that
// game clients show in their own code page.  Letters missing from it lose
// their accent.
std::string utf8ToCodepage(const std::string& text, int codepage);
bool isSupportedCodepage(int codepage);

std::string trim(const std::string& value);
std::string randomToken(std::size_t length);
}
