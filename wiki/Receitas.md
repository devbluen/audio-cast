# Receitas

Exemplos curtos para copiar. Eles usam comandos com `strcmp` para não depender de nada; adapte ao seu processador de comandos. Um filterscript completo está em `examples/radio.pwn`.

## /radio: entrar e sair da rádio

```pawn
if (!strcmp(cmdtext, "/radio", true))
{
    if (Radio_GetPlayerRadio(playerid) == g_Radio)
    {
        Radio_RemovePlayer(playerid);
        SendClientMessage(playerid, -1, "Você saiu da rádio.");
    }
    else
    {
        Radio_AddPlayer(playerid, g_Radio);
        SendClientMessage(playerid, -1, "Sintonizado!");
    }
    return 1;
}
```

## /volume: volume próprio de cada jogador

```pawn
if (!strcmp(cmdtext, "/volume", true, 7))
{
    new volume = strval(cmdtext[8]);
    if (cmdtext[7] != ' ' || volume < 0 || volume > 100)
        return SendClientMessage(playerid, -1, "Uso: /volume <0-100>");
    Radio_SetPlayerVolume(playerid, volume);
    return 1;
}
```

O volume acompanha o jogador em outras rádios e volta a 100 quando ele sai. Para abaixar uma rádio inteira para todos (uma rádio de lobby mais baixa, abaixar antes de um aviso), use `Radio_SetVolume(radioid, volume)`.

## Anunciar a música para os ouvintes

```pawn
public OnRadioTrackStart(radioid, requestid, const title[], duration_ms)
{
    new length[8], message[144];
    Radio_FormatTime(duration_ms, length);
    format(message, sizeof message, "Tocando agora: %s (%s)", title, length);
    for (new i = 0, j = GetPlayerPoolSize(); i <= j; i++)
    {
        if (Radio_GetPlayerRadio(i) == radioid) SendClientMessage(i, 0x8BC34AFF, message);
    }
    return 1;
}
```

O `OnRadioTrackStart` é chamado quando os jogadores **ouvem** a música começar, então a mensagem bate com o áudio.

## Diálogo de busca

```pawn
#define DIALOG_SEARCH (7750)
new g_Search[MAX_PLAYERS];

// /search <texto>
g_Search[playerid] = Radio_Search(cmdtext[8], 8);

public OnRadioSearchResults(searchid, count, const error[])
{
    for (new p = 0, j = GetPlayerPoolSize(); p <= j; p++)
    {
        if (g_Search[p] != searchid) continue;
        if (count == 0) return SendClientMessage(p, -1, "Nada encontrado.");

        new list[2048], videoid[16], title[96], duration, length[8];
        for (new i = 0; i < count; i++)
        {
            Radio_GetSearchResult(searchid, i, videoid, title, duration);
            Radio_FormatTime(duration, length);
            format(list, sizeof list, "%s%s\t%s\n", list, title, length);
        }
        ShowPlayerDialog(p, DIALOG_SEARCH, DIALOG_STYLE_TABLIST, "Escolha a música", list, "Tocar", "Cancelar");
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

## Caixa de som / balada (som 3D)

```pawn
// Uma caixa de som na balada: quem está perto ouve a rádio vindo daquele ponto.
Radio_AddPlayer(playerid, g_RadioBalada, 1833.0, -1682.0, 13.5, 40.0, true);
```

O jogo abaixa o som com a distância. Jogadores que se afastam continuam conectados; tire-os com `Radio_RemovePlayer` quando saírem da área (por exemplo, num callback de área do streamer).

## Várias rádios

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

// Trocar: adicionar o jogador a outra rádio troca ele de rádio.
Radio_AddPlayer(playerid, g_Rock);
```

Cada rádio tem seu próprio relógio, fila e volume. Uma música pedida em várias rádios é baixada uma vez só.

## Música sem parar

Reabasteça a fila quando ela acabar, por exemplo a partir de uma playlist:

```pawn
new const g_Playlist[][] = { "dQw4w9WgXcQ", "tiago iorc amei te ver", "queen bohemian rhapsody" };
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

Para não ter pausa entre as músicas, coloque a próxima na fila um pouco antes da atual acabar: carregar leva 3-5 s.

## Avisar o erro para quem pediu

```pawn
new g_RequestOwner[64] = { INVALID_PLAYER_ID, ... };

// quando um jogador pede:
new request = Radio_Play(g_Radio, cmdtext[6]);
if (request) g_RequestOwner[request % sizeof g_RequestOwner] = playerid;

public OnRadioTrackError(radioid, requestid, const query[], const error[])
{
    new owner = g_RequestOwner[requestid % sizeof g_RequestOwner];
    if (owner != INVALID_PLAYER_ID)
    {
        new message[144];
        format(message, sizeof message, "Não deu para tocar '%s': %s", query, error);
        SendClientMessage(owner, 0xE57373FF, message);
    }
    return 1;
}
```

## Ouvir fora do jogo

Com `audiocast_allow_public 1`, `Radio_GetPublicUrl(radioid, url)` retorna uma URL que abre no VLC ou no navegador, ou que um bot de Discord pode retransmitir. Ela toca sincronizada com os jogadores.
