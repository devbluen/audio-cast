/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include <cstdint>

namespace Text
{
// The character of each byte from 0x80 in a Windows code page from 1250 to
// 1258 (0 where the byte has none), or nullptr for any other code page.
const std::uint16_t* codepageTable(int codepage);
}
