/*
 *  AudioCast - synchronized radio example (filterscript)
 *
 *  Works the same on open.mp and SA-MP.  Commands:
 *    /radio                     tune in or out
 *    /search <song name>        list YouTube results and pick one
 *    /play <link or search>     play now (RCON admin)
 *    /queue <link or search>    add to the queue (RCON admin)
 *    /skip  /stop  /pause  /resume  (RCON admin)
 *    /nowplaying                show the song and its position
 */

#define FILTERSCRIPT

#include <a_samp>
#include <audio-cast>

#define COLOR_RADIO     (0x8BC34AFF)
#define COLOR_ERROR     (0xE57373FF)
// Any number your scripts do not use for other dialogs.
#define DIALOG_RADIO_SEARCH (7750)

new g_Radio = INVALID_RADIO_ID;
// Who asked for each song, to report errors back to them.
new g_RequestOwner[64] = { INVALID_PLAYER_ID, ... };
// The search each player is looking at.
new g_PlayerSearch[MAX_PLAYERS];

public OnFilterScriptInit()
{
	g_Radio = Radio_Create("Server Radio");
	print("[radio] AudioCast ready. Use /radio to listen.");
	return 1;
}

static stock MessageListeners(radioid, color, const message[])
{
	for (new i = 0; i < MAX_PLAYERS; i++)
	{
		if (IsPlayerConnected(i) && Radio_GetPlayerRadio(i) == radioid)
		{
			SendClientMessage(i, color, message);
		}
	}
}

static stock ShowNowPlaying(playerid)
{
	new title[128], message[180], position[16], duration[16];
	if (!Radio_GetTitle(g_Radio, title))
	{
		SendClientMessage(playerid, COLOR_RADIO, "[Radio] Nothing is playing right now.");
		return;
	}
	Radio_FormatTime(Radio_GetPosition(g_Radio), position);
	Radio_FormatTime(Radio_GetDuration(g_Radio), duration);
	format(message, sizeof message, "[Radio] Now playing: %s (%s / %s)%s", title, position, duration,
		Radio_IsPaused(g_Radio) ? " [paused]" : "");
	SendClientMessage(playerid, COLOR_RADIO, message);
}

static stock Request(playerid, const query[], bool:now)
{
	if (!IsPlayerAdmin(playerid))
	{
		return SendClientMessage(playerid, COLOR_ERROR, "[Radio] Admins only.");
	}
	if (query[0] == EOS)
	{
		if (now) return SendClientMessage(playerid, COLOR_ERROR, "Usage: /play <YouTube link or search>");
		return SendClientMessage(playerid, COLOR_ERROR, "Usage: /queue <YouTube link or search>");
	}

	new requestid = now ? Radio_Play(g_Radio, query) : Radio_Queue(g_Radio, query);
	if (!requestid)
	{
		return SendClientMessage(playerid, COLOR_ERROR, "[Radio] Invalid request.");
	}
	g_RequestOwner[requestid % sizeof g_RequestOwner] = playerid;
	return SendClientMessage(playerid, COLOR_RADIO, "[Radio] Loading song...");
}

static stock CloseSearch(playerid)
{
	if (g_PlayerSearch[playerid])
	{
		Radio_FreeSearch(g_PlayerSearch[playerid]);
		g_PlayerSearch[playerid] = 0;
	}
}

public OnPlayerConnect(playerid)
{
	g_PlayerSearch[playerid] = 0;
	return 1;
}

public OnPlayerDisconnect(playerid, reason)
{
	CloseSearch(playerid);
	return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
	if (!strcmp(cmdtext, "/radio", true))
	{
		if (Radio_GetPlayerRadio(playerid) == g_Radio)
		{
			Radio_RemovePlayer(playerid);
			SendClientMessage(playerid, COLOR_RADIO, "[Radio] You left the radio.");
		}
		else
		{
			// Whoever joins now hears the same moment as everyone else.
			Radio_AddPlayer(playerid, g_Radio);
			SendClientMessage(playerid, COLOR_RADIO, "[Radio] Tuned in! Use /radio again to leave.");
			ShowNowPlaying(playerid);
		}
		return 1;
	}
	if (!strcmp(cmdtext, "/search", true, 7) && (cmdtext[7] == ' ' || cmdtext[7] == EOS))
	{
		if (cmdtext[7] == EOS || cmdtext[8] == EOS)
		{
			SendClientMessage(playerid, COLOR_ERROR, "Usage: /search <song name>");
			return 1;
		}
		CloseSearch(playerid);
		g_PlayerSearch[playerid] = Radio_Search(cmdtext[8], 8);
		SendClientMessage(playerid, COLOR_RADIO, "[Radio] Searching...");
		return 1;
	}
	if (!strcmp(cmdtext, "/nowplaying", true))
	{
		ShowNowPlaying(playerid);
		return 1;
	}
	if (!strcmp(cmdtext, "/play", true, 5) && (cmdtext[5] == ' ' || cmdtext[5] == EOS))
	{
		if (cmdtext[5] == EOS) Request(playerid, "", true);
		else Request(playerid, cmdtext[6], true);
		return 1;
	}
	if (!strcmp(cmdtext, "/queue", true, 6) && (cmdtext[6] == ' ' || cmdtext[6] == EOS))
	{
		if (cmdtext[6] == EOS) Request(playerid, "", false);
		else Request(playerid, cmdtext[7], false);
		return 1;
	}

	if (IsPlayerAdmin(playerid))
	{
		if (!strcmp(cmdtext, "/skip", true))
		{
			if (!Radio_Skip(g_Radio)) SendClientMessage(playerid, COLOR_ERROR, "[Radio] Nothing is playing.");
			return 1;
		}
		if (!strcmp(cmdtext, "/stop", true))
		{
			Radio_Stop(g_Radio);
			MessageListeners(g_Radio, COLOR_RADIO, "[Radio] The radio was stopped.");
			return 1;
		}
		if (!strcmp(cmdtext, "/pause", true))
		{
			if (Radio_Pause(g_Radio)) MessageListeners(g_Radio, COLOR_RADIO, "[Radio] Paused.");
			return 1;
		}
		if (!strcmp(cmdtext, "/resume", true))
		{
			if (Radio_Resume(g_Radio)) MessageListeners(g_Radio, COLOR_RADIO, "[Radio] Resumed.");
			return 1;
		}
	}
	return 0;
}

public OnRadioSearchResults(searchid, count, const error[])
{
	new playerid = INVALID_PLAYER_ID;
	for (new i = 0; i < MAX_PLAYERS; i++)
	{
		if (IsPlayerConnected(i) && g_PlayerSearch[i] == searchid)
		{
			playerid = i;
			break;
		}
	}
	if (playerid == INVALID_PLAYER_ID)
	{
		Radio_FreeSearch(searchid);
		return 1;
	}
	if (count == 0)
	{
		CloseSearch(playerid);
		SendClientMessage(playerid, COLOR_ERROR, "[Radio] Nothing found.");
		return 1;
	}

	// Song, channel and duration in a table; the chosen row is played.
	new list[1800] = "Song\tChannel\tDuration\n", row[180];
	new videoid[16], title[96], channel[48], duration, time[16];
	for (new i = 0; i < count; i++)
	{
		Radio_GetSearchResult(searchid, i, videoid, title, duration);
		Radio_GetSearchChannel(searchid, i, channel);
		Radio_FormatTime(duration, time);
		if (strlen(title) > 60) strdel(title, 57, strlen(title)), strcat(title, "...");
		format(row, sizeof row, "%s\t%s\t%s\n", title, channel, time);
		strcat(list, row);
	}
	new button[8] = "Queue";
	if (IsPlayerAdmin(playerid)) button = "Play";
	ShowPlayerDialog(playerid, DIALOG_RADIO_SEARCH, DIALOG_STYLE_TABLIST_HEADERS, "Radio - search results", list, button, "Cancel");
	return 1;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
	if (dialogid != DIALOG_RADIO_SEARCH) return 0;

	new searchid = g_PlayerSearch[playerid];
	new videoid[16], title[96], duration;
	if (response && searchid && Radio_GetSearchResult(searchid, listitem, videoid, title, duration))
	{
		// Admins play it right away; everyone else adds it to the queue.
		new requestid = IsPlayerAdmin(playerid) ? Radio_Play(g_Radio, videoid) : Radio_Queue(g_Radio, videoid);
		if (requestid)
		{
			g_RequestOwner[requestid % sizeof g_RequestOwner] = playerid;
			new message[144];
			format(message, sizeof message, "[Radio] Loading %s...", title);
			SendClientMessage(playerid, COLOR_RADIO, message);
		}
	}
	CloseSearch(playerid);
	return 1;
}

public OnRadioTrackLoaded(radioid, requestid, const title[], duration_ms)
{
	new owner = g_RequestOwner[requestid % sizeof g_RequestOwner];
	if (owner != INVALID_PLAYER_ID && IsPlayerConnected(owner) && Radio_GetRequestId(radioid) != requestid)
	{
		new message[160];
		format(message, sizeof message, "[Radio] Queued: %s", title);
		SendClientMessage(owner, COLOR_RADIO, message);
	}
	return 1;
}

public OnRadioTrackStart(radioid, requestid, const title[], duration_ms)
{
	new message[160], duration[16];
	Radio_FormatTime(duration_ms, duration);
	format(message, sizeof message, "[Radio] Now playing: %s (%s)", title, duration);
	MessageListeners(radioid, COLOR_RADIO, message);
	return 1;
}

public OnRadioTrackError(radioid, requestid, const query[], const error[])
{
	new owner = g_RequestOwner[requestid % sizeof g_RequestOwner];
	if (owner != INVALID_PLAYER_ID && IsPlayerConnected(owner))
	{
		new message[144];
		format(message, sizeof message, "[Radio] Could not load '%s': %s", query, error);
		SendClientMessage(owner, COLOR_ERROR, message);
	}
	printf("[radio] error loading '%s': %s", query, error);
	return 1;
}

public OnRadioListenerConnect(radioid, playerid)
{
	printf("[radio] player %d connected to radio %d's stream (%d listeners)", playerid, radioid, Radio_GetListenerCount(radioid));
	return 1;
}
