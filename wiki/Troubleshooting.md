# Troubleshooting

## Players hear nothing

Check `Radio_IsPlayerStreaming(playerid)` a few seconds after `Radio_AddPlayer`:

- **0 - the game never connected to the stream.**
  - The TCP port (`audiocast_port`, default 8000) is closed. Open it in the firewall *and* in the hosting panel; many hosts block extra ports by default.
  - `audiocast_public_host` is not set or wrong. The server log warns at startup when it is missing (`stream URLs use 127.0.0.1`). It must be the address players use to reach the server, not `127.0.0.1` or a LAN IP.
  - A proxy or port forward uses another port: set `audiocast_public_port`.
  - Test from your own PC: set `audiocast_allow_public 1`, then open `http://<public_host>:<port>/radio/1.mp3` in VLC or a browser.
- **1 - connected, but silent.** Nothing is playing (`Radio_IsPlaying`), the volume is 0 (`Radio_GetVolume`, `Radio_GetPlayerVolume`), or the player's in-game radio volume is off (game Options → Audio).

## Songs fail to load

The reason arrives in `OnRadioTrackError(..., error[])` and in the server log.

| Error | Cause and fix |
|---|---|
| `Sign in to confirm you're not a bot` | Common on VPS/datacenter IPs. Give the plugin a YouTube sign-in with `audiocast_cookies`: see [YouTube Cookies](YouTube-Cookies). |
| `video is live, longer than audiocast_max_duration or has no usable audio` | Live streams are not supported. For long videos (DJ sets) raise `audiocast_max_duration`. |
| `Requested format is not available` | Plugin versions before AAC support could only play videos with Opus audio. Update the plugin. If it persists, YouTube changed something: yt-dlp updates itself daily; delete `scriptfiles/audiocast/bin/` to force a fresh download now. |
| `not a YouTube link, video ID or search` | Only YouTube is supported; links to other sites are rejected. |
| `yt-dlp is not available yet` | The first yt-dlp download failed (no internet, or no `curl`/`wget` on Linux). It is retried automatically. |
| `Video unavailable` / `Private video` | Region-locked, private, removed or age-restricted video. Age-restricted ones need `audiocast_cookies`. |

Try a song outside the game with the bundled tool; it runs the same code as the plugin:

```
tools/audio-cast-check "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
```

## The audio is a few seconds late

By design. Players hear the radio about `audiocast_burst_ms` (4 s) after the server produces it: the game client buffers a few seconds of any stream, and giving everyone the same buffer is what keeps them in sync. Pause, skip, seek and volume changes reach the ears after the same delay. The getters and `OnRadioTrackStart`/`End` already follow what players hear.

## Players are out of sync

Everyone in the same radio is within about one frame (≈24 ms) of each other. If one player is behind, their connection cannot keep up with the bitrate (each listener needs `audiocast_bitrate` kbps, 128 kbps ≈ 16 KB/s). Lower `audiocast_bitrate`, or check the server's upload bandwidth: 100 listeners at 128 kbps need about 13 Mbit/s.

## Accents look wrong in chat

By default titles are converted to Windows-1252, which the SA-MP chat displays. If your script (or a library such as a UTF-8 textdraw renderer) expects UTF-8, set `audiocast_utf8 1`.

## "503 Service Unavailable"

`audiocast_max_listeners` (default 500) stream connections are open. Raise it if your server and bandwidth allow.

## Songs take long to start

- Normal: 3-5 s after `Radio_Play`; search text adds about a second.
- `audiocast_normalize 1` downloads the whole song before it plays.
- Replays are instant with `audiocast_cache_minutes`.
- A slow or blocked connection to YouTube from the host also shows here; time `audio-cast-check` on the server.
