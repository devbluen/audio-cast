/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "samp-amx.hpp"
#include <plugincommon.h>

namespace
{
void* functionTable = nullptr;

template <typename Return, typename... Args>
Return invoke(std::size_t index, Args... args) noexcept
{
	using Function = Return (AMXAPI*)(Args...);
	if (!functionTable)
	{
		return Return {};
	}

	const auto function = reinterpret_cast<Function*>(functionTable)[index];
	return function ? function(args...) : Return {};
}
}

namespace SampAmx
{
void setFunctionTable(void* table) noexcept
{
	functionTable = table;
}

bool available() noexcept
{
	return functionTable != nullptr;
}

int getAddr(AMX* amx, cell address, cell** physicalAddress) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_GetAddr, amx, address, physicalAddress);
}

int getString(char* destination, const cell* source, int useWchar, std::size_t size) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_GetString, destination, source, useWchar, size);
}

int setString(cell* destination, const char* source, int pack, int useWchar, std::size_t size) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_SetString, destination, source, pack, useWchar, size);
}

int strLen(const cell* source, int* length) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_StrLen, source, length);
}

int findPublic(AMX* amx, const char* name, int* index) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_FindPublic, amx, name, index);
}

int exec(AMX* amx, cell* result, int index) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_Exec, amx, result, index);
}

int push(AMX* amx, cell value) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_Push, amx, value);
}

int pushArray(AMX* amx, cell* amxAddress, cell** physicalAddress, const cell* array, int cells) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_PushArray, amx, amxAddress, physicalAddress, array, cells);
}

int pushString(AMX* amx, cell* amxAddress, cell** physicalAddress, const char* string, int pack, int useWchar) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_PushString, amx, amxAddress, physicalAddress, string, pack, useWchar);
}

int release(AMX* amx, cell address) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_Release, amx, address);
}

int registerNatives(AMX* amx, const AMX_NATIVE_INFO* natives, int count) noexcept
{
	return invoke<int>(PLUGIN_AMX_EXPORT_Register, amx, natives, count);
}
}
