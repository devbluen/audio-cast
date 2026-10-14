/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "samp-plugin.hpp"
#include "samp-amx.hpp"
#include "audiocast.hpp"
#include "log.hpp"
#include "version.hpp"

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports()
{
	return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** data)
{
	if (!data) return false;

	SampAmx::setFunctionTable(data[PLUGIN_DATA_AMX_EXPORTS]);
	logprintf = reinterpret_cast<logprintf_t>(data[PLUGIN_DATA_LOGPRINTF]);
	AudioCastComponent::getInstance()->sampLoad();
	AudioCastLogFlush();
	if (logprintf) logprintf("[AudioCast] v%s loaded as SA-MP plugin", AUDIOCAST_VERSION);
	return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload()
{
	AudioCastComponent::getInstance()->sampUnload();
	SampAmx::setFunctionTable(nullptr);
	if (logprintf) logprintf("[AudioCast] SA-MP plugin unloaded");
	logprintf = nullptr;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX* amx)
{
	AudioCastComponent::getInstance()->sampAmxLoad(amx);
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX* amx)
{
	AudioCastComponent::getInstance()->sampAmxUnload(amx);
	return AMX_ERR_NONE;
}

PLUGIN_EXPORT void PLUGIN_CALL ProcessTick()
{
	AudioCastComponent::getInstance()->sampTick();
}
