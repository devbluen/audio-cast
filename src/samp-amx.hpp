/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "samp-plugin.hpp"
#include <cstddef>

namespace SampAmx
{
void setFunctionTable(void* table) noexcept;
bool available() noexcept;

int getAddr(AMX* amx, cell address, cell** physicalAddress) noexcept;
int getString(char* destination, const cell* source, int useWchar, std::size_t size) noexcept;
int setString(cell* destination, const char* source, int pack, int useWchar, std::size_t size) noexcept;
int strLen(const cell* source, int* length) noexcept;
int findPublic(AMX* amx, const char* name, int* index) noexcept;
int exec(AMX* amx, cell* result, int index) noexcept;
int push(AMX* amx, cell value) noexcept;
int pushArray(AMX* amx, cell* amxAddress, cell** physicalAddress, const cell* array, int cells) noexcept;
int pushString(AMX* amx, cell* amxAddress, cell** physicalAddress, const char* string, int pack, int useWchar) noexcept;
int release(AMX* amx, cell address) noexcept;
int registerNatives(AMX* amx, const AMX_NATIVE_INFO* natives, int count) noexcept;
}
