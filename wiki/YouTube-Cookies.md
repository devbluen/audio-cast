# YouTube Cookies

## When do I need this?

Only when songs fail with:

```
Sign in to confirm you're not a bot
```

YouTube often asks this of VPS and datacenter IPs. With cookies, yt-dlp uses **your** YouTube sign-in and the block goes away. If songs already play, you do not need cookies.

> **Use a secondary account.** The cookies file gives access to the account, like a password. Do not share it or commit it to GitHub. An account used by a server may be limited by YouTube, so do not use your main one.

## Step 1 - Export cookies.txt

1. Install an extension that exports in **cookies.txt (Netscape)** format:
   - Chrome / Edge / Opera: **Get cookies.txt LOCALLY**
   - Firefox: **cookies.txt**
2. In the extension's settings, allow it to run in **private/incognito windows**.
3. Open a **private window**, sign in to your Google account and open **youtube.com**.
4. Still on the YouTube tab, click the extension and export the cookies (**Export** / **Current Site**). Save as `cookies.txt`.
5. **Close the private window** right away, without signing out (do not click "Sign out").

Why a private window? While YouTube is open in a browser it rotates its cookies now and then, and exported ones stop working. Closing the private window means nothing else uses those cookies, so they last much longer.

**Check the file** in a text editor. It should start like this:

```
# Netscape HTTP Cookie File
.youtube.com	TRUE	/	TRUE	17...	LOGIN_INFO	AFmmF2sw...
.youtube.com	TRUE	/	TRUE	17...	__Secure-3PSID	g.a000...
```

If it starts with `[` or `{` (JSON), the extension used the wrong format: pick **Netscape** / **cookies.txt** in it.

## Step 2 - Put the file on the server

Upload `cookies.txt` to the server folder (FTP or the host's file manager). Suggested place:

```
server/
  omp-server.exe   (or samp-server.exe / omp-server / samp03svr)
  server.cfg
  scriptfiles/
    audiocast/
      cookies.txt      <- here
```

## Step 3 - Configure

The path is **relative to the server folder** (where `omp-server`/`samp-server` is). Use **one** of:

**server.cfg**

```
audiocast_cookies scriptfiles/audiocast/cookies.txt
```

- No comment (`#`) on the same line: it would become part of the path.
- A full path works too: `audiocast_cookies C:\server\scriptfiles\audiocast\cookies.txt` (quotes are optional, even with spaces).

**config.json (open.mp)**

```json
"audiocast": {
    "cookies": "scriptfiles/audiocast/cookies.txt"
}
```

In JSON use `/` in the path (or doubled `\\`: `"C:\\server\\cookies.txt"`).

**Environment variable:** `AUDIOCAST_COOKIES=scriptfiles/audiocast/cookies.txt`.

## Step 4 - Restart and check the log

The option is read when the server starts, so **restart the server**. The log shows one of these:

| Log message | Meaning |
|---|---|
| `using the YouTube sign-in from C:\...\cookies.txt` | Done, it works. |
| `... was not found, so songs load without cookies. A relative path starts at the server folder (...)` | Wrong path. The message shows the full path the plugin looked at and the server folder: compare with where the file really is. |
| `... is not a cookies.txt file (Netscape format)` | Not a cookies.txt. Export again in Netscape format (Step 1). |
| `... has YouTube cookies but no sign-in` / `has no youtube.com cookies` | Exported while signed out, or from another site. Sign in, open youtube.com and export from that tab. |

With `was not found` or `is not a cookies.txt file` the plugin ignores the file and songs are fetched without cookies until you fix it; nothing breaks, only YouTube's block remains.

## Test without the game

The tool in the zip loads a song exactly like the plugin, using the cookies:

```
tools/audio-cast-check --cookies scriptfiles/audiocast/cookies.txt "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
```

Run it from the server folder. If it ends with `fully loaded`, everything is set.

## After it works

- **yt-dlp updates the file** on every song (it writes the renewed cookies back). Keep the file writable and do not worry if its content changes.
- **Cookies expire.** If `Sign in to confirm you're not a bot` comes back weeks later, repeat Step 1 and replace the file.
- `The page needs to be reloaded` after setting up cookies? yt-dlp needs a JavaScript runtime to use them: see [Troubleshooting](Troubleshooting). The plugin installs one (Deno) by itself; the log shows `using deno ...`.
- Still blocked with valid cookies? Some IPs are blocked harder (`HTTP Error 429: Too Many Requests` means too many requests from your IP); wait a few hours or use another account.
