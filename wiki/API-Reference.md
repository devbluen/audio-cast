# API Reference

Everything is declared in `audio-cast.inc`. Natives return 0 for an invalid radio, player or search ID.

## Radios

| Native | Description |
|---|---|
| `Radio_Create(const name[] = "")` | Creates a radio and returns its ID (`INVALID_RADIO_ID` = 0 on failure). The radio belongs to the calling script and is destroyed when that script unloads. |
| `Radio_Destroy(radioid)` | Destroys the radio; its listeners are disconnected. |
| `Radio_IsValid(radioid)` | 1 if the radio exists. |
| `Radio_GetName(radioid, name[], size = sizeof name)` | The radio's name (also sent as the stream name). |

## Playback

`query` is a YouTube link (watch, youtu.be, shorts, music, embed, live), a video ID, or search text such as `"artist - song"`. Links to other sites are rejected.

| Native | Description |
|---|---|
| `Radio_Play(radioid, const query[])` | Plays now: the current song is replaced as soon as the new one has loaded. Returns a request ID (0 = rejected). If several `Radio_Play` calls are loading, the latest wins. |
| `Radio_Queue(radioid, const query[])` | Adds to the end of the queue. Returns a request ID. |
| `Radio_Skip(radioid)` | Next song in the queue. 0 if nothing is playing. |
| `Radio_Stop(radioid)` | Stops and clears the queue. Listeners stay connected and hear silence. |
| `Radio_ClearQueue(radioid)` | Clears the queue; returns how many requests were removed. |
| `Radio_Pause(radioid)` / `Radio_Resume(radioid)` | For everyone at once. |
| `Radio_Seek(radioid, position_ms)` | Jumps to a position, for everyone. |
| `Radio_IsPlaying(radioid)` / `Radio_IsPaused(radioid)` / `Radio_IsLoading(radioid)` | State. |
| `Radio_GetPosition(radioid)` / `Radio_GetDuration(radioid)` | Milliseconds. |
| `Radio_GetRequestId(radioid)` | Request ID of the current song. |
| `Radio_GetTitle(radioid, title[], size = sizeof title)` | Current song title. |
| `Radio_GetVideoId(radioid, videoid[], size = sizeof videoid)` | Current YouTube video ID (11 characters). |
| `Radio_GetQueueSize(radioid)` | Queued requests, including those still loading. |
| `Radio_GetListenerCount(radioid)` | Open stream connections. |
| `Radio_GetPublicUrl(radioid, url[], size = sizeof url)` | Stream URL without a token. Needs `audiocast_allow_public 1`. |

**Timing.** Players hear the stream about `audiocast_burst_ms` (4 s) after the server produces it. The getters and the track callbacks follow what players **hear**. Commands act on the server at once and reach the ears after that delay.

## Volume

| Native | Description |
|---|---|
| `Radio_SetVolume(radioid, volume)` / `Radio_GetVolume(radioid)` | Volume of everyone on the radio, 0 (mute) to 100 (default). |
| `Radio_SetPlayerVolume(playerid, volume)` / `Radio_GetPlayerVolume(playerid)` | One player's own volume, 0 to 100. Kept when they change radio; reset to 100 when they leave the server. |

The volume a player hears is the radio's times their own (50 × 50 = 25%). It is applied by editing the gain stored in each MP3 frame, so it is free for the server and every player can have a different one. It can only lower the song from its original level, in steps of 1.5 dB (50% = -6 dB, 10% ≈ -20 dB). Changes are heard after the burst delay.

## Listeners

| Function | Description |
|---|---|
| `Radio_AddPlayer(playerid, radioid, Float:posX = 0.0, Float:posY = 0.0, Float:posZ = 0.0, Float:distance = 50.0, bool:usepos = false)` | Tunes the player in (a stock that calls `Radio_AttachPlayer` and `PlayAudioStreamForPlayer`). With `usepos = true` the audio is a 3D sound at that position. Adding a player to another radio moves them. |
| `Radio_RemovePlayer(playerid)` | Takes the player out and stops their audio. |
| `Radio_AttachPlayer(playerid, radioid, url[], size = sizeof url)` | Low level: creates the player's private stream URL (`RADIO_URL_SIZE` cells is always enough) without playing it. |
| `Radio_DetachPlayer(playerid)` | Low level: revokes the URL; the stream stops. |
| `Radio_GetPlayerRadio(playerid)` | The player's radio, 0 = none. |
| `Radio_IsPlayerStreaming(playerid)` | 1 while the player's game is actually connected to the stream. 0 a few seconds after `Radio_AddPlayer` usually means a closed port or a wrong `audiocast_public_host`. |
| `Radio_FormatTime(milliseconds, output[], size = sizeof output)` | Formats as `m:ss`. |

## Search

| Native | Description |
|---|---|
| `Radio_Search(const query[], max_results = 5)` | Searches YouTube (1-20 results) without playing. Returns a search ID; results arrive in `OnRadioSearchResults`. |
| `Radio_GetSearchCount(searchid)` | Number of results, -1 while running. |
| `Radio_GetSearchResult(searchid, index, videoid[], title[], &duration_ms, videoid_size = sizeof videoid, title_size = sizeof title)` | One result. |
| `Radio_GetSearchChannel(searchid, index, channel[], size = sizeof channel)` | The result's channel. |
| `Radio_FreeSearch(searchid)` | Frees the results (also freed when the script unloads). |

## Callbacks

Called in every script (gamemode and filterscripts) that defines them.

```pawn
forward OnRadioTrackLoaded(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackStart(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackEnd(radioid, requestid, reason);
forward OnRadioTrackError(radioid, requestid, const query[], const error[]);
forward OnRadioListenerConnect(radioid, playerid);
forward OnRadioListenerDisconnect(radioid, playerid);
forward OnRadioSearchResults(searchid, count, const error[]);
```

| Callback | When |
|---|---|
| `OnRadioTrackLoaded` | A requested song is ready to play (its first seconds are converted). |
| `OnRadioTrackStart` | Players start hearing a song. |
| `OnRadioTrackEnd` | Players stop hearing it. `reason`: `RADIO_END_FINISHED`, `RADIO_END_SKIPPED`, `RADIO_END_STOPPED`, `RADIO_END_REPLACED`. |
| `OnRadioTrackError` | A request failed: invalid link, live stream, too long, blocked by YouTube... `error` says why (in English). |
| `OnRadioListenerConnect` / `Disconnect` | A player's game opened or closed the stream. `playerid` is `-1` for public listeners (VLC, browser), so check it before using it. |
| `OnRadioSearchResults` | A search finished. `count` = 0 with `error` set when it failed. |

## Constants

```pawn
#define INVALID_RADIO_ID   (0)
#define RADIO_URL_SIZE     (160)
#define RADIO_END_FINISHED (0)
#define RADIO_END_SKIPPED  (1)
#define RADIO_END_STOPPED  (2)
#define RADIO_END_REPLACED (3)
```

Define `AUDIOCAST_NO_HOOKS` before including to disable the include's `OnPlayerDisconnect` hook (it detaches the player and resets their volume; SA-MP needs it, open.mp does it by itself).
