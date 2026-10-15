# Recipes

Short, copyable examples. They use `strcmp` commands to stay dependency-free; adapt them to your command processor. A complete filterscript is in `examples/radio.pwn`.

## /radio: tune in and out

```pawn
if (!strcmp(cmdtext, "/radio", true))
{
    if (Radio_GetPlayerRadio(playerid) == g_Radio)
    {
        Radio_RemovePlayer(playerid);
        SendClientMessage(playerid, -1, "You left the radio.");
    }
    else
    {
        Radio_AddPlayer(playerid, g_Radio);
        SendClientMessage(playerid, -1, "Tuned in!");
    }
    return 1;
}
```

## /volume: each player's own volume

```pawn
if (!strcmp(cmdtext, "/volume", true, 7))
{
    new volume = strval(cmdtext[8]);
    if (cmdtext[7] != ' ' || volume < 0 || volume > 100)
        return SendClientMessage(playerid, -1, "Usage: /volume <0-100>");
    Radio_SetPlayerVolume(playerid, volume);
    return 1;
}
```

The volume follows the player to other radios and resets when they leave. To turn a whole radio down for everyone (a quieter lobby radio, a fade before an announcement), use `Radio_SetVolume(radioid, volume)`.

## Announce the song to its listeners

```pawn
public OnRadioTrackStart(radioid, requestid, const title[], duration_ms)
{
    new length[8], message[144];
    Radio_FormatTime(duration_ms, length);
    format(message, sizeof message, "Now playing: %s (%s)", title, length);
    for (new i = 0, j = GetPlayerPoolSize(); i <= j; i++)
    {
        if (Radio_GetPlayerRadio(i) == radioid) SendClientMessage(i, 0x8BC34AFF, message);
    }
    return 1;
}
```

`OnRadioTrackStart` fires when players **hear** the song start, so the message matches the audio.

## Search dialog

```pawn
#define DIALOG_SEARCH (7750)
new g_Search[MAX_PLAYERS];

// /search <text>
g_Search[playerid] = Radio_Search(cmdtext[8], 8);

public OnRadioSearchResults(searchid, count, const error[])
{
    for (new p = 0, j = GetPlayerPoolSize(); p <= j; p++)
    {
        if (g_Search[p] != searchid) continue;
        if (count == 0) return SendClientMessage(p, -1, "Nothing found.");

        new list[2048], videoid[16], title[96], duration, length[8];
        for (new i = 0; i < count; i++)
        {
            Radio_GetSearchResult(searchid, i, videoid, title, duration);
            Radio_FormatTime(duration, length);
            format(list, sizeof list, "%s%s\t%s\n", list, title, length);
        }
        ShowPlayerDialog(p, DIALOG_SEARCH, DIALOG_STYLE_TABLIST, "Pick a song", list, "Play", "Cancel");
    }
    return 1;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    if (dialogid != DIALOG_SEARCH) return 0;
    if (response)
    {
        new videoid[16], title[96], duration;
        Radio_GetSearchResult(g_Search[playerid], listitem, videoid, title, duration);
        Radio_Queue(g_Radio, videoid);
    }
    Radio_FreeSearch(g_Search[playerid]);
    g_Search[playerid] = 0;
    return 1;
}
```

## Boombox / club speaker (3D sound)

```pawn
// A speaker at the club: players near it hear the radio from that point.
Radio_AddPlayer(playerid, g_ClubRadio, 1833.0, -1682.0, 13.5, 40.0, true);
```

The game fades the sound with distance. Players who walk away keep the connection; remove them with `Radio_RemovePlayer` when they leave the area (for example from an area/streamer callback).

## Several radios

```pawn
new g_Pop, g_Rock;

public OnGameModeInit()
{
    g_Pop = Radio_Create("Pop");
    g_Rock = Radio_Create("Rock");
    Radio_Queue(g_Pop, "dua lipa levitating");
    Radio_Queue(g_Rock, "ac/dc back in black");
    return 1;
}

// Switching: adding a player to another radio moves them.
Radio_AddPlayer(playerid, g_Rock);
```

Each radio has its own clock, queue and volume. A song requested on several radios is downloaded once.

## Keep the music going

Refill the queue when it runs out, for example from a playlist array:

```pawn
new const g_Playlist[][] = { "dQw4w9WgXcQ", "daft punk get lucky", "queen bohemian rhapsody" };
new g_Next;

public OnRadioTrackEnd(radioid, requestid, reason)
{
    if (radioid == g_Radio && Radio_GetQueueSize(radioid) == 0 && reason == RADIO_END_FINISHED)
    {
        Radio_Queue(radioid, g_Playlist[g_Next]);
        g_Next = (g_Next + 1) % sizeof g_Playlist;
    }
    return 1;
}
```

Queue the next song a little before the current one ends if you want no gap: loading takes 3-5 s.

## Report errors to whoever asked

```pawn
new g_RequestOwner[64] = { INVALID_PLAYER_ID, ... };

// when a player requests:
new request = Radio_Play(g_Radio, cmdtext[6]);
if (request) g_RequestOwner[request % sizeof g_RequestOwner] = playerid;

public OnRadioTrackError(radioid, requestid, const query[], const error[])
{
    new owner = g_RequestOwner[requestid % sizeof g_RequestOwner];
    if (owner != INVALID_PLAYER_ID)
    {
        new message[144];
        format(message, sizeof message, "Could not play '%s': %s", query, error);
        SendClientMessage(owner, 0xE57373FF, message);
    }
    return 1;
}
```

## Listening outside the game

With `audiocast_allow_public 1`, `Radio_GetPublicUrl(radioid, url)` returns a URL that opens in VLC or a browser, or can be relayed by a Discord bot. It plays in sync with the players.
