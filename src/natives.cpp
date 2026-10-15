/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "natives.hpp"
#include "audiocast.hpp"
#include "log.hpp"
#include <algorithm>
#include <string>

namespace
{
AudioCastComponent& component()
{
	return *AudioCastComponent::getInstance();
}

bool hasParams(const cell* params, std::size_t count)
{
	return static_cast<std::size_t>(params[0]) / sizeof(cell) >= count;
}

std::string readString(AMX* amx, cell address)
{
	IPawnScript* script = component().script(amx);
	cell* physical = nullptr;
	if (!script || script->GetAddr(address, &physical) != AMX_ERR_NONE || !physical) return {};
	int length = 0;
	if (script->StrLen(physical, &length) != AMX_ERR_NONE || length <= 0) return {};
	std::string text(static_cast<std::size_t>(length) + 1, '\0');
	script->GetString(text.data(), physical, false, text.size());
	text.resize(static_cast<std::size_t>(length));
	return text;
}

bool writeString(AMX* amx, cell address, cell size, const std::string& value)
{
	IPawnScript* script = component().script(amx);
	cell* physical = nullptr;
	if (!script || size <= 0 || script->GetAddr(address, &physical) != AMX_ERR_NONE || !physical) return false;
	return script->SetString(physical, StringView(value.data(), value.size()), false, false, static_cast<size_t>(size)) == AMX_ERR_NONE;
}

std::shared_ptr<Station> radioParam(const cell* params, std::size_t index = 1)
{
	return component().radio(static_cast<int>(params[index]));
}

// native Radio_Create(const name[] = "");
cell n_Create(AMX* amx, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().createRadio(amx, component().fromPawn(readString(amx, params[1])));
}

// native Radio_Destroy(radioid);
cell n_Destroy(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().destroyRadio(static_cast<int>(params[1])) ? 1 : 0;
}

// native Radio_IsValid(radioid);
cell n_IsValid(AMX*, cell* params)
{
	return hasParams(params, 1) && radioParam(params) ? 1 : 0;
}

// native Radio_Play(radioid, const query[]);
cell n_Play(AMX* amx, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	return component().requestTrack(static_cast<int>(params[1]), component().fromPawn(readString(amx, params[2])), true);
}

// native Radio_Queue(radioid, const query[]);
cell n_Queue(AMX* amx, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	return component().requestTrack(static_cast<int>(params[1]), component().fromPawn(readString(amx, params[2])), false);
}

// native Radio_Skip(radioid);
cell n_Skip(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station && station->skip() ? 1 : 0;
}

// native Radio_Stop(radioid);
cell n_Stop(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	if (!station) return 0;
	station->stop();
	return 1;
}

// native Radio_ClearQueue(radioid);
cell n_ClearQueue(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station ? static_cast<cell>(station->clearQueue()) : 0;
}

// native Radio_Pause(radioid);
cell n_Pause(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station && station->setPaused(true) ? 1 : 0;
}

// native Radio_Resume(radioid);
cell n_Resume(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station && station->setPaused(false) ? 1 : 0;
}

// native Radio_Seek(radioid, position_ms);
cell n_Seek(AMX*, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	const auto station = radioParam(params);
	return station && station->seek(static_cast<std::uint32_t>(std::max<cell>(params[2], 0))) ? 1 : 0;
}

// native Radio_IsPlaying(radioid);
cell n_IsPlaying(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station && station->status().playing ? 1 : 0;
}

// native Radio_IsPaused(radioid);
cell n_IsPaused(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station && station->status().paused ? 1 : 0;
}

// native Radio_IsLoading(radioid);
cell n_IsLoading(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station && station->status().loading ? 1 : 0;
}

// native Radio_GetPosition(radioid);
cell n_GetPosition(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station ? static_cast<cell>(station->status().positionMs) : 0;
}

// native Radio_GetDuration(radioid);
cell n_GetDuration(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station ? static_cast<cell>(station->status().durationMs) : 0;
}

// native Radio_GetRequestId(radioid);
cell n_GetRequestId(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station ? static_cast<cell>(station->status().requestId) : 0;
}

// native Radio_GetTitle(radioid, title[], size = sizeof title);
cell n_GetTitle(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const auto station = radioParam(params);
	if (!station) return 0;
	const StationStatus status = station->status();
	writeString(amx, params[2], params[3], component().toPawn(status.title));
	return status.playing ? 1 : 0;
}

// native Radio_GetVideoId(radioid, videoid[], size = sizeof videoid);
cell n_GetVideoId(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const auto station = radioParam(params);
	if (!station) return 0;
	const StationStatus status = station->status();
	writeString(amx, params[2], params[3], status.videoId);
	return status.playing ? 1 : 0;
}

// native Radio_GetName(radioid, name[], size = sizeof name);
cell n_GetName(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const auto station = radioParam(params);
	if (!station) return 0;
	return writeString(amx, params[2], params[3], component().toPawn(station->name())) ? 1 : 0;
}

// native Radio_GetQueueSize(radioid);
cell n_GetQueueSize(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	const auto station = radioParam(params);
	return station ? static_cast<cell>(station->status().queueSize) : 0;
}

// native Radio_GetListenerCount(radioid);
cell n_GetListenerCount(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().listenerCount(static_cast<int>(params[1]));
}

// native Radio_SetVolume(radioid, volume);
cell n_SetVolume(AMX*, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	return component().setRadioVolume(static_cast<int>(params[1]), static_cast<int>(params[2])) ? 1 : 0;
}

// native Radio_GetVolume(radioid);
cell n_GetVolume(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().radioVolume(static_cast<int>(params[1]));
}

// native Radio_GetPublicUrl(radioid, url[], size = sizeof url);
cell n_GetPublicUrl(AMX* amx, cell* params)
{
	if (!hasParams(params, 3)) return 0;
	const std::string url = component().publicUrl(static_cast<int>(params[1]));
	if (url.empty()) return 0;
	return writeString(amx, params[2], params[3], url) ? 1 : 0;
}

// native Radio_AttachPlayer(playerid, radioid, url[], size = sizeof url);
cell n_AttachPlayer(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	std::string url;
	if (!component().attachPlayer(static_cast<int>(params[1]), static_cast<int>(params[2]), url)) return 0;
	if (static_cast<std::size_t>(std::max<cell>(params[4], 0)) <= url.size())
	{
		AudioCastLogWarning("Radio_AttachPlayer: the url buffer needs at least " + std::to_string(url.size() + 1) + " cells");
		component().detachPlayer(static_cast<int>(params[1]));
		return 0;
	}
	return writeString(amx, params[3], params[4], url) ? 1 : 0;
}

// native Radio_DetachPlayer(playerid);
cell n_DetachPlayer(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().detachPlayer(static_cast<int>(params[1])) ? 1 : 0;
}

// native Radio_GetPlayerRadio(playerid);
cell n_GetPlayerRadio(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().playerRadio(static_cast<int>(params[1]));
}

// native Radio_IsPlayerStreaming(playerid);
cell n_IsPlayerStreaming(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().playerStreaming(static_cast<int>(params[1])) ? 1 : 0;
}

// native Radio_SetPlayerVolume(playerid, volume);
cell n_SetPlayerVolume(AMX*, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	return component().setPlayerVolume(static_cast<int>(params[1]), static_cast<int>(params[2])) ? 1 : 0;
}

// native Radio_GetPlayerVolume(playerid);
cell n_GetPlayerVolume(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().playerVolume(static_cast<int>(params[1]));
}

// native Radio_Search(const query[], max_results = 5);
cell n_Search(AMX* amx, cell* params)
{
	if (!hasParams(params, 2)) return 0;
	return component().startSearch(amx, component().fromPawn(readString(amx, params[1])), static_cast<int>(params[2]));
}

// native Radio_GetSearchCount(searchid);
cell n_GetSearchCount(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return -1;
	return component().searchCount(static_cast<int>(params[1]));
}

// native Radio_GetSearchResult(searchid, index, videoid[], title[], &duration_ms,
//     videoid_size = sizeof videoid, title_size = sizeof title);
cell n_GetSearchResult(AMX* amx, cell* params)
{
	if (!hasParams(params, 7)) return 0;
	const SearchEntry* entry = component().searchEntry(static_cast<int>(params[1]), static_cast<int>(params[2]));
	if (!entry) return 0;
	writeString(amx, params[3], params[6], entry->videoId);
	writeString(amx, params[4], params[7], component().toPawn(entry->title));
	IPawnScript* script = component().script(amx);
	cell* duration = nullptr;
	if (script && script->GetAddr(params[5], &duration) == AMX_ERR_NONE && duration)
	{
		*duration = static_cast<cell>(entry->durationMs);
	}
	return 1;
}

// native Radio_GetSearchChannel(searchid, index, channel[], size = sizeof channel);
cell n_GetSearchChannel(AMX* amx, cell* params)
{
	if (!hasParams(params, 4)) return 0;
	const SearchEntry* entry = component().searchEntry(static_cast<int>(params[1]), static_cast<int>(params[2]));
	if (!entry) return 0;
	return writeString(amx, params[3], params[4], component().toPawn(entry->channel)) ? 1 : 0;
}

// native Radio_FreeSearch(searchid);
cell n_FreeSearch(AMX*, cell* params)
{
	if (!hasParams(params, 1)) return 0;
	return component().freeSearch(static_cast<int>(params[1])) ? 1 : 0;
}

// An exception must never unwind through the AMX's C code.
template <cell (*Native)(AMX*, cell*)>
cell AMX_NATIVE_CALL guarded(AMX* amx, cell* params)
{
	try
	{
		return Native(amx, params);
	}
	catch (const std::exception& exception)
	{
		AudioCastLogWarning(std::string("native failed: ") + exception.what());
	}
	catch (...)
	{
		AudioCastLogWarning("native failed");
	}
	return 0;
}

const AMX_NATIVE_INFO kNatives[] = {
	{ "Radio_Create", guarded<n_Create> },
	{ "Radio_Destroy", guarded<n_Destroy> },
	{ "Radio_IsValid", guarded<n_IsValid> },
	{ "Radio_Play", guarded<n_Play> },
	{ "Radio_Queue", guarded<n_Queue> },
	{ "Radio_Skip", guarded<n_Skip> },
	{ "Radio_Stop", guarded<n_Stop> },
	{ "Radio_ClearQueue", guarded<n_ClearQueue> },
	{ "Radio_Pause", guarded<n_Pause> },
	{ "Radio_Resume", guarded<n_Resume> },
	{ "Radio_Seek", guarded<n_Seek> },
	{ "Radio_IsPlaying", guarded<n_IsPlaying> },
	{ "Radio_IsPaused", guarded<n_IsPaused> },
	{ "Radio_IsLoading", guarded<n_IsLoading> },
	{ "Radio_GetPosition", guarded<n_GetPosition> },
	{ "Radio_GetDuration", guarded<n_GetDuration> },
	{ "Radio_GetRequestId", guarded<n_GetRequestId> },
	{ "Radio_GetTitle", guarded<n_GetTitle> },
	{ "Radio_GetVideoId", guarded<n_GetVideoId> },
	{ "Radio_GetName", guarded<n_GetName> },
	{ "Radio_GetQueueSize", guarded<n_GetQueueSize> },
	{ "Radio_GetListenerCount", guarded<n_GetListenerCount> },
	{ "Radio_GetPublicUrl", guarded<n_GetPublicUrl> },
	{ "Radio_SetVolume", guarded<n_SetVolume> },
	{ "Radio_GetVolume", guarded<n_GetVolume> },
	{ "Radio_AttachPlayer", guarded<n_AttachPlayer> },
	{ "Radio_DetachPlayer", guarded<n_DetachPlayer> },
	{ "Radio_GetPlayerRadio", guarded<n_GetPlayerRadio> },
	{ "Radio_IsPlayerStreaming", guarded<n_IsPlayerStreaming> },
	{ "Radio_SetPlayerVolume", guarded<n_SetPlayerVolume> },
	{ "Radio_GetPlayerVolume", guarded<n_GetPlayerVolume> },
	{ "Radio_Search", guarded<n_Search> },
	{ "Radio_GetSearchCount", guarded<n_GetSearchCount> },
	{ "Radio_GetSearchResult", guarded<n_GetSearchResult> },
	{ "Radio_GetSearchChannel", guarded<n_GetSearchChannel> },
	{ "Radio_FreeSearch", guarded<n_FreeSearch> },
};
}

int RegisterAudioCastNatives(IPawnScript& script)
{
	return script.Register(kNatives, static_cast<int>(sizeof(kNatives) / sizeof(kNatives[0])));
}
