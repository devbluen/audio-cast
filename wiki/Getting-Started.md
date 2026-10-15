# Getting Started

## 1. Download

Get the zip for your server's system from [Releases](https://github.com/devbluen/audio-cast/releases):

| Server | File |
|---|---|
| Windows | `audio-cast-<version>-windows-x86.zip` |
| Linux | `audio-cast-<version>-linux-x86.zip` |

The zip contains:

```
components/audio-cast.dll|.so   open.mp
plugins/audio-cast.dll|.so      SA-MP (same file)
include/audio-cast.inc          Pawn include
tools/audio-cast-check          tests your setup
examples/radio.pwn              complete filterscript
licenses/                       third-party licenses
```

## 2. Install

**open.mp:** copy `components/audio-cast.dll` (or `.so`) to the server's `components/` folder.

**SA-MP:** copy `plugins/audio-cast.dll` (or `.so`) to `plugins/` and add it to `server.cfg`:

```
plugins audio-cast.dll
```

Copy `include/audio-cast.inc` to `qawno/include` (open.mp) or `pawno/include` (SA-MP).

Nothing else is installed: the audio conversion is built into the plugin, and the plugin downloads and updates [yt-dlp](https://github.com/yt-dlp/yt-dlp) by itself into `scriptfiles/audiocast/bin/`. On Linux that first download needs `curl` or `wget`.

## 3. Open a port and set the public address

Players download the audio straight from your server, so:

1. Open **TCP port 8000** (or the one in `audiocast_port`) in the firewall / hosting panel.
2. Set the address players use to reach the server:

```
audiocast_public_host 203.0.113.10
```

Without it the stream URLs point to `127.0.0.1` and only work on the server machine itself. See [Configuration](Configuration) for every option.

## 4. First radio

```pawn
#include <open.mp>      // or <a_samp>
#include <audio-cast>

new g_Radio;

public OnGameModeInit()
{
    g_Radio = Radio_Create("Server Radio");
    Radio_Play(g_Radio, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    Radio_Queue(g_Radio, "daft punk get lucky");   // search text works too
    return 1;
}

public OnPlayerSpawn(playerid)
{
    Radio_AddPlayer(playerid, g_Radio);   // starts at the current position
    return 1;
}
```

Start the server and spawn: the song plays a few seconds later. If nothing is heard, see [Troubleshooting](Troubleshooting).

## 5. Next steps

- [Recipes](Recipes): /radio command, search dialog, boombox, per-player volume, several radios.
- [API Reference](API-Reference): every native and callback.
- `examples/radio.pwn`: a ready filterscript with `/radio`, `/search`, `/volume`, `/play`, `/queue`, `/skip`...
