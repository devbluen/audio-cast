# AudioCast

**English** | [Português](README.pt-BR.md) | [Wiki](https://github.com/devbluen/audio-cast/wiki)

Synchronized YouTube radio for **open.mp** and **SA-MP**, 100% server-side.

Give it a YouTube song (link or search) and every tuned-in player hears **the same part of the song at the same time**. A player who joins mid-song starts at the current position, just like a real radio. Nothing is needed on the client: the game plays the audio through the native `PlayAudioStreamForPlayer`.

The same file (`audio-cast.dll` / `audio-cast.so`) works as an open.mp component and as a SA-MP plugin.

## How it works

```
 Radio_Play("link")                         server (plugin)                         players
 ─────────────────►  yt-dlp ─► Opus/AAC decoder ─► MP3 encoder ─► radio clock ─► HTTP :8000 ─► PlayAudioStreamForPlayer
                     (managed)   (built in)       (built in)      (memory only)
```

1. **yt-dlp** fetches the song's audio straight into memory. The plugin's built-in decoders (libopus for Opus, fdk-aac for AAC) and MP3 encoder (LAME) convert it to the stream format (MP3, 48 kHz stereo, constant bitrate) **while it downloads**: the song starts as soon as its first 3 seconds are ready, usually 3–5 s after `Radio_Play`. Nothing is written to disk.
2. Each radio has **a single clock**: audio frames leave in real time, at exactly 1× the song's speed, whether anyone is listening or not. Silence is sent between songs, so connections never drop.
3. The plugin has a built-in Shoutcast/Icecast-style HTTP server. Each player gets their own URL (with a token). On connect they receive the same "burst" of recent audio everyone else received, then the live stream.

Since everyone gets the same burst and the same real-time stream, everyone plays the same frame at the same moment. In network tests, a listener who joined 1.7 s later lined up with the first one to within about one frame (≈24 ms).

## Server requirements

Nothing to install. You only need an open **TCP port** in the firewall (default `8000`), because players download the stream from it.

- **Audio conversion** is built into the plugin (libopus + fdk-aac + LAME, linked statically). ffmpeg is not needed.
- **yt-dlp** is managed by the plugin. On first start it downloads the official build for your OS and CPU into `scriptfiles/audiocast/bin/`, and it installs new releases (checked once a day). The unpacked build is used because it starts about a second faster on every song. yt-dlp stays a separate program on purpose: YouTube changes often, and yt-dlp gets fixes within days, without waiting for a new plugin release.
  - Windows uses the built-in WinHTTP. Linux needs `curl` or `wget` for that first download (present on virtually every host).
  - Prefer your own copy? Set `audiocast_ytdlp` to its path and the plugin will use it as is, without updating it.
- **Deno** is managed the same way, into `scriptfiles/audiocast/deno/` (~100 MB). YouTube hides its audio behind JavaScript challenges, and yt-dlp needs a JavaScript runtime to solve them; without one many videos fail with `The page needs to be reloaded` or `Sign in to confirm you're not a bot`. Deno has 64-bit builds only (Windows, and Linux with glibc); elsewhere install Node.js and set `audiocast_js_runtime node`.

## Download

Get the zip for your server's system from the [Releases](https://github.com/devbluen/audio-cast/releases) page:

- `audio-cast-<version>-windows-x86.zip` - Windows servers
- `audio-cast-<version>-linux-x86.zip` - Linux servers

Each zip has `components/` (open.mp), `plugins/` (SA-MP), `include/audio-cast.inc`, `tools/audio-cast-check`, `examples/radio.pwn` and `licenses/`. Development builds of every commit are also available as artifacts of the *Build AudioCast* workflow in the Actions tab, one per platform.

## Installation

**open.mp:** copy `audio-cast.dll` (Windows) or `audio-cast.so` (Linux) to `components/`.

**SA-MP:** copy it to `plugins/` and add it to `server.cfg`:

```
plugins audio-cast.dll        # Windows
plugins audio-cast.so         # Linux
```

Copy `audio-cast.inc` to your compiler's include folder (`qawno/include` or `pawno/include`).

Then set at least the server's public address (see below).

## Configuration

Each option can come from three places, in this order:

1. an environment variable (`AUDIOCAST_PORT`, `AUDIOCAST_PUBLIC_HOST`…);
2. open.mp's `config.json`, as `"audiocast_port": 8000` or in an `"audiocast": { "port": 8000 }` section;
3. `server.cfg`, as `audiocast_port 8000`.

| Option | Default | Description |
|---|---|---|
| `audiocast_public_host` | `network.public_addr` / `bind` | **Public** IP or domain written into the URL sent to players. **Set this.** Without it URLs use `127.0.0.1` and only work on your own machine. |
| `audiocast_port` | `8000` | TCP port of the stream server. |
| `audiocast_public_port` | = `port` | Port in the URL, when behind port forwarding or a proxy. |
| `audiocast_bind` | `0.0.0.0` | Interface the stream server listens on. |
| `audiocast_burst_ms` | `4000` | Recent audio sent to each listener on connect. Everyone gets the same amount, which is what keeps them in sync. It is also how far behind the server players hear (see *Timing*). Lower values shorten that delay but should not go below the game client's own buffer (about 3–4 s), or players who join start with a gap. |
| `audiocast_bitrate` | `128` | MP3 kbps (32–320). |
| `audiocast_normalize` | `0` | `1` = even out the loudness of songs (to about -16 dBFS RMS, never clipping). The whole song is downloaded before it plays. |
| `audiocast_ytdlp` | `auto` | `auto` = the plugin downloads and updates yt-dlp. Or the path of your own yt-dlp. |
| `audiocast_js_runtime` | `auto` | JavaScript runtime yt-dlp uses to solve YouTube's challenges. `auto` = the plugin downloads and updates [Deno](https://deno.com) (~100 MB). `off` = none. Or a yt-dlp `--js-runtimes` value such as `node` or `deno:/path/to/deno`. |
| `audiocast_cookies` | | YouTube cookies file (`cookies.txt`, path relative to the server folder), for when YouTube asks to "confirm you're not a bot" (common on VPS/datacenter IPs). Step by step: [wiki](https://github.com/devbluen/audio-cast/wiki/YouTube-Cookies). |
| `audiocast_data_dir` | `scriptfiles/audiocast` | Holds the managed yt-dlp (~18 MB), Deno (~100 MB) and short-lived temporary files. Songs are never saved here. |
| `audiocast_cache_minutes` | `0` | How long a song stays **in memory** after no radio uses it, so replaying it is instant. `0` frees it right away. |
| `audiocast_max_duration` | `1800` | Longest accepted song, in seconds. Live streams are always rejected. |
| `audiocast_download_timeout` | `300` | Time limit for each download/conversion, in seconds. |
| `audiocast_workers` | `2` | Simultaneous downloads. |
| `audiocast_max_listeners` | `500` | Total simultaneous stream connections. |
| `audiocast_allow_public` | `0` | `1` = allow `http://host:port/radio/<id>.mp3` without a token (VLC, browser, Discord bot…). |
| `audiocast_utf8` | `0` | `1` = exchange text with Pawn as UTF-8. By default titles are converted to Windows-1252, which is what the SA-MP chat displays (accents work). |
| `audiocast_metadata_codepage` | `1252` | Code page of the song title the game client shows over the stream: a Windows code page from `1250` to `1258` (`1250` central Europe, `1251` Cyrillic, `1252` western Europe, ...) or `0` for plain ASCII. Letters the code page lacks lose their accent. |
| `audiocast_log_songs` | `0` | `1` = write "loaded <id> (<title>)" to the server console for every song. Errors and warnings are always shown. |

`server.cfg` example:

```
plugins audio-cast.dll
audiocast_public_host 203.0.113.10
audiocast_port 8000
```

`config.json` example (open.mp):

```json
"audiocast": {
    "public_host": "203.0.113.10",
    "port": 8000
}
```

## Quick start

```pawn
#include <open.mp>      // or <a_samp>
#include <audio-cast>

new g_Radio;

public OnGameModeInit()
{
    g_Radio = Radio_Create("Server Radio");
    Radio_Play(g_Radio, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    Radio_Queue(g_Radio, "daft punk get lucky");   // search works too
    return 1;
}

// /radio
Radio_AddPlayer(playerid, g_Radio);     // join: starts at the current position
Radio_RemovePlayer(playerid);           // leave

public OnRadioTrackStart(radioid, requestid, const title[], duration_ms)
{
    // tell the listeners, update a textdraw, etc.
    return 1;
}
```

A complete filterscript with commands (`/radio`, `/search`, `/volume`, `/play`...) is in [examples/radio.pwn](examples/radio.pwn).

## API

### Radios

| Native | Returns |
|---|---|
| `Radio_Create(const name[] = "")` | The radio ID. It belongs to the script that created it and is destroyed when that script unloads. |
| `Radio_Destroy(radioid)` | 1/0 |
| `Radio_IsValid(radioid)` | 1/0 |
| `Radio_GetName(radioid, name[], size)` | 1/0 |

### Playback

`query` can be a YouTube link (watch, youtu.be, shorts, music, embed, live), a video ID or search text. Links to other sites are rejected.

| Native | Description |
|---|---|
| `Radio_Play(radioid, const query[])` | Play now: the current song is replaced as soon as the new one finishes loading. Returns the request ID (0 = failed). If several `Radio_Play` calls are loading at once, the latest one wins. |
| `Radio_Queue(radioid, const query[])` | Add to the end of the queue. Returns the request ID. |
| `Radio_Skip(radioid)` | Skip to the next song in the queue. |
| `Radio_Stop(radioid)` | Stop the song and clear the queue. Listeners stay connected (silence). |
| `Radio_ClearQueue(radioid)` | Clear the queue. Returns how many requests were removed. |
| `Radio_Pause(radioid)` / `Radio_Resume(radioid)` | Pause and resume for everyone at once. |
| `Radio_Seek(radioid, position_ms)` | Jump to a position, for everyone. |
| `Radio_IsPlaying` / `Radio_IsPaused` / `Radio_IsLoading` | Radio state. |
| `Radio_GetPosition(radioid)` / `Radio_GetDuration(radioid)` | In milliseconds. |
| `Radio_GetTitle(radioid, title[], size)` / `Radio_GetVideoId(...)` | Current song. |
| `Radio_GetRequestId(radioid)` | Request ID of the current song. |
| `Radio_GetQueueSize(radioid)` | Queued items (including those still loading). |
| `Radio_GetListenerCount(radioid)` | Active stream connections. |
| `Radio_GetPublicUrl(radioid, url[], size)` | URL without a token (needs `audiocast_allow_public 1`). |
| `Radio_SetVolume(radioid, volume)` / `Radio_GetVolume(radioid)` | Volume for everyone on the radio, 0 (mute) to 100 (default). Heard after the burst players already hold (about `audiocast_burst_ms`). |

The getters (`IsPlaying`, `IsPaused`, `GetPosition`, `GetTitle`, `GetRequestId`…) describe **what players hear right now**, not what the server is producing (see *Timing*).

### Search

Lists YouTube results without playing anything, for example to show a dialog where the player picks a song. Play the chosen one with `Radio_Play`/`Radio_Queue` and its video ID.

| Native | Description |
|---|---|
| `Radio_Search(const query[], max_results = 5)` | Starts a search (1–20 results). Returns a search ID (0 = failed). The results arrive in `OnRadioSearchResults`. |
| `Radio_GetSearchCount(searchid)` | Number of results, or -1 while the search is still running. |
| `Radio_GetSearchResult(searchid, index, videoid[], title[], &duration_ms, ...)` | One result: video ID, title and duration. |
| `Radio_GetSearchChannel(searchid, index, channel[], size)` | The result's channel. |
| `Radio_FreeSearch(searchid)` | Frees the results. Searches are also freed when their script unloads. |

```pawn
// /search <song name>
g_PlayerSearch[playerid] = Radio_Search(params, 8);

public OnRadioSearchResults(searchid, count, const error[])
{
    new videoid[16], title[96], duration;
    for (new i = 0; i < count; i++)
    {
        Radio_GetSearchResult(searchid, i, videoid, title, duration);
        // add a dialog row...
    }
}
// when the player picks row `listitem`:
Radio_GetSearchResult(searchid, listitem, videoid, title, duration);
Radio_Play(g_Radio, videoid);
```

### Listeners

| Function | Description |
|---|---|
| `Radio_AddPlayer(playerid, radioid, Float:x = 0.0, Float:y = 0.0, Float:z = 0.0, Float:distance = 50.0, bool:usepos = false)` | Tunes the player in. With a position it becomes a 3D sound (boombox, club speaker…). |
| `Radio_RemovePlayer(playerid)` | Removes the player from the radio and stops the audio. |
| `Radio_GetPlayerRadio(playerid)` | The player's radio (0 = none). |
| `Radio_IsPlayerStreaming(playerid)` | 1 while the player's client is actually connected to the stream. Useful to detect a blocked port. |
| `Radio_SetPlayerVolume(playerid, volume)` / `Radio_GetPlayerVolume(playerid)` | One player's volume, 0 to 100, on top of the radio's (50 on a radio at 50 = 25). Kept when they change radio, reset when they leave. |
| `Radio_AttachPlayer` / `Radio_DetachPlayer` | Low-level versions (they only create or revoke the player's URL). |
| `Radio_FormatTime(ms, output[], size)` | Formats as `m:ss`. |

### Volume

`PlayAudioStreamForPlayer` has no volume, so the plugin lowers it in the stream itself: it edits the gain stored in each MP3 frame, without re-encoding. Every player can have their own volume on the same radio.

- The volume a player hears is the radio's times their own: `Radio_SetVolume(radio, 50)` plus `Radio_SetPlayerVolume(player, 50)` = 25%.
- 0 is mute and 100 the song's original level; it cannot go above that.
- Steps are 1.5 dB, so close values may sound the same (50% = -6 dB, 10% ≈ -20 dB).
- Like pause and skip, a change is heard after the burst the player already holds (about `audiocast_burst_ms`).

```pawn
// /volume <0-100>
Radio_SetPlayerVolume(playerid, strval(params));
```

### Callbacks

```pawn
forward OnRadioTrackLoaded(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackStart(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackEnd(radioid, requestid, reason);  // RADIO_END_FINISHED / SKIPPED / STOPPED / REPLACED
forward OnRadioTrackError(radioid, requestid, const query[], const error[]);
forward OnRadioListenerConnect(radioid, playerid);
forward OnRadioListenerDisconnect(radioid, playerid);
forward OnRadioSearchResults(searchid, count, const error[]);
```

Callbacks are called in every script (gamemode and filterscripts) that defines them. `OnRadioTrackLoaded` fires when a song is ready to play; `OnRadioTrackStart`/`OnRadioTrackEnd` fire when players actually **hear** it start or end.

## Checking your setup

The `audio-cast-check` tool loads a song exactly like the plugin does and can optionally play the radio for a few seconds so you can listen in VLC:

```
audio-cast-check "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
audio-cast-check "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast 8000 60
  → open http://127.0.0.1:8000/radio/1.mp3 in VLC
audio-cast-check --search "daft punk" auto scriptfiles/audiocast
audio-cast-check --cookies scriptfiles/audiocast/cookies.txt "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
```

## Notes

- **Timing:** game clients buffer a few seconds of any audio stream before playing, and AudioCast keeps everyone in sync by giving all of them the same `audiocast_burst_ms` of buffer. So players hear the radio about 4 s after the server produces it. The getters and `OnRadioTrackStart`/`End` follow what players hear, so chat messages match the audio. Commands (`Radio_Pause`, `Skip`, `Seek`, `Play`, the volume) act on the server right away and **reach the ears after that delay**: after a pause, the song keeps playing for about 4 s; after a resume, it comes back about 4 s later. This is inherent to streaming to the unmodified game client.
- **Disk space:** songs only live in memory, about 1 MB per minute of music, while a radio plays or queues them (plus `audiocast_cache_minutes`). They are freed when no radio uses them. On disk there are only the yt-dlp and Deno programs. Leftovers from a crash, and songs saved by version 1.0, are deleted on startup.
- **Loading time:** a song is ready to play 3–5 s after `Radio_Play` (searching by text in the same call adds about a second); the rest loads while it plays. If two radios play the same song, it is fetched only once. With `audiocast_normalize 1` the whole song must be downloaded first, so it takes longer.
- **Formats:** the plugin uses YouTube's Opus audio stream, which practically every video has. Videos without one (often recent uploads) play from their AAC (m4a) stream instead.
- **Clean cuts:** every MP3 frame is encoded on its own (no bit reservoir), so pausing, seeking, skipping and joining mid-song never produce clicks.
- **YouTube blocking:** on a VPS YouTube often asks you to sign in. Export your browser's cookies to a `cookies.txt` file and point `audiocast_cookies` at it; the server log confirms whether the file was found and has a sign-in. Step by step: [YouTube Cookies](https://github.com/devbluen/audio-cast/wiki/YouTube-Cookies).
- **Security:** the plugin never uses a shell. yt-dlp is called directly with arguments, and only validated video IDs are turned into URLs, so text typed by players cannot inject commands. Each player gets their own token in the URL, which is revoked when they leave.
- **Bandwidth:** each listener uses the configured bitrate (128 kbps ≈ 16 KB/s).

## Building

On Windows, one script builds **both Windows and Linux** and collects everything in `dist/`:

```powershell
.\build.ps1                         # Windows + Linux, version 1.0.0
.\build.ps1 -Version 1.2.0          # version embedded in the binaries and zip names
.\build.ps1 -Target linux           # only one platform (windows | linux)
.\build.ps1 -Clean -SkipTests       # rebuild from scratch, without the tests
```

It only needs **Visual Studio 2022** (the "Desktop development with C++" workload) and **CMake**. The rest is downloaded with checked SHA-256 hashes:

- **Linux:** the Linux binary is cross-compiled with [Zig](https://ziglang.org/), downloaded into `.tools/` on first use. No Linux machine, WSL or Docker is needed. It targets glibc 2.17+, so it loads on practically any Linux host.
- **Libraries:** asio, libopus, fdk-aac and LAME are downloaded and compiled by CMake (`cmake/dependencies.cmake`) and linked statically.

The first run takes a few minutes; later runs only rebuild what changed. Result:

```
dist/
  windows/components/audio-cast.dll    → open.mp server: components/
  windows/plugins/audio-cast.dll       → SA-MP server:   plugins/
  windows/tools/audio-cast-check.exe
  linux/components/audio-cast.so       → open.mp server: components/
  linux/plugins/audio-cast.so          → SA-MP server:   plugins/
  linux/tools/audio-cast-check
  include/audio-cast.inc               → qawno/include or pawno/include
  examples/radio.pwn
  audio-cast-<version>-windows-x86.zip ready-to-share packages
  audio-cast-<version>-linux-x86.zip
```

On a Linux machine you can also build natively (needs `gcc-multilib`/`g++-multilib`, CMake and Ninja):

```bash
cmake --preset linux-x86 && cmake --build --preset linux-x86 && ctest --preset linux-x86
```

SA-MP and open.mp servers are 32-bit, so every build is x86.

GitHub Actions (`.github/workflows/build.yml`) builds each platform in its own job on every push and uploads one artifact per platform. Running the workflow by hand with **Publish a GitHub release** checked also creates the `v<version>` release with both zips, after the builds and the Linux tests pass.

The plugin links LAME (LGPL), libopus (BSD) and the Fraunhofer FDK AAC library (FDK license). If you distribute the binary, follow the LGPL terms for LAME: for example, ship this project's source or link LAME so it can be replaced. The FDK license asks that its text goes with the binary (the release zips have it in `licenses/fdk-aac.txt`) and that the library's source is available ([github.com/mstorsjo/fdk-aac](https://github.com/mstorsjo/fdk-aac), unmodified). It grants no patent rights for AAC.
