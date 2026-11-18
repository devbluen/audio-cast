# Configuration

Each option can be set in three places. The first one found wins:

1. an environment variable: `AUDIOCAST_PORT`, `AUDIOCAST_PUBLIC_HOST`...
2. open.mp's `config.json`: `"audiocast_port": 8000`, or inside an `"audiocast": { "port": 8000 }` section;
3. `server.cfg`: `audiocast_port 8000`.

## Options

| Option | Default | Description |
|---|---|---|
| `audiocast_public_host` | `network.public_addr` / `bind` | Public IP or domain put into the URL sent to players. **Set this.** |
| `audiocast_port` | `8000` | TCP port of the built-in stream server. |
| `audiocast_public_port` | = `port` | Port in the URL, when a proxy or port forward uses a different one. |
| `audiocast_bind` | `0.0.0.0` | Interface the stream server listens on. |
| `audiocast_burst_ms` | `4000` | Audio sent to each listener on connect. It keeps everyone in sync and is also how far behind the server players hear. Do not go below about 3000 (the game client's own buffer). |
| `audiocast_bitrate` | `128` | MP3 kbps, 32-320. Each listener uses this much bandwidth (128 kbps ≈ 16 KB/s). |
| `audiocast_normalize` | `0` | `1` = even out the loudness of songs (about -16 dBFS RMS, never clipping). Songs take longer to start: the whole file is downloaded first. |
| `audiocast_ytdlp` | `auto` | `auto` = managed yt-dlp, updated daily. Or the path of your own yt-dlp (used as is, never updated). |
| `audiocast_js_runtime` | `auto` | JavaScript runtime yt-dlp uses to solve YouTube's challenges. `auto` = the plugin downloads and updates [Deno](https://deno.com) (~100 MB). `off` = none. Or a yt-dlp `--js-runtimes` value such as `node` or `deno:/path/to/deno`. |
| `audiocast_cookies` | | YouTube cookies file, for when YouTube asks to "confirm you're not a bot". Step by step: [YouTube Cookies](YouTube-Cookies). |
| `audiocast_data_dir` | `scriptfiles/audiocast` | Managed yt-dlp, Deno and short-lived temporary files. Songs are never stored on disk. |
| `audiocast_cache_minutes` | `0` | Keep a song in memory this long after no radio uses it, so replaying it is instant. |
| `audiocast_max_duration` | `1800` | Longest accepted song, in seconds. Live streams are always rejected. |
| `audiocast_download_timeout` | `300` | Time limit per download, in seconds. |
| `audiocast_workers` | `2` | Simultaneous downloads. |
| `audiocast_max_listeners` | `500` | Total stream connections. Extra ones get `503`. |
| `audiocast_allow_public` | `0` | `1` = allow `http://host:port/radio/<id>.mp3` without a player token (VLC, browser, Discord bots). |
| `audiocast_utf8` | `0` | `1` = exchange text with Pawn as UTF-8. By default titles are converted to Windows-1252, which the SA-MP chat shows correctly. |
| `audiocast_metadata_codepage` | `1252` | Code page of the song title the game client shows over the stream: a Windows code page from `1250` to `1258` (`1250` central Europe, `1251` Cyrillic, `1252` western Europe, ...) or `0` for plain ASCII. Letters the code page lacks lose their accent. |
| `audiocast_log_songs` | `0` | `1` = write "loaded <id> (<title>)" to the server console for every song. Errors and warnings are always shown. |

## Examples

`server.cfg`:

```
plugins audio-cast.so
audiocast_public_host radio.example.com
audiocast_port 8000
audiocast_bitrate 128
audiocast_cache_minutes 10
```

`config.json` (open.mp):

```json
"audiocast": {
    "public_host": "radio.example.com",
    "port": 8000,
    "cache_minutes": 10
}
```

## Behind a proxy or port forward

If players reach the stream on a different port than the one the server listens on (Docker, NAT, a reverse proxy), set both:

```
audiocast_port 8000            # where the plugin listens
audiocast_public_port 28000    # what goes into the URL
```

## Memory and disk

Songs live only in memory, about 1 MB per minute of music, while a radio plays or queues them (plus `audiocast_cache_minutes`). A song played on several radios is downloaded once. On disk there are only the yt-dlp (~18 MB) and Deno (~100 MB) programs.
