# Referência da API

Tudo está declarado em `audio-cast.inc`. As natives retornam 0 para rádio, jogador ou busca inválidos.

## Rádios

| Native | Descrição |
|---|---|
| `Radio_Create(const name[] = "")` | Cria uma rádio e retorna o ID (`INVALID_RADIO_ID` = 0 se falhar). A rádio pertence ao script que a criou e é destruída quando ele descarrega. |
| `Radio_Destroy(radioid)` | Destrói a rádio; os ouvintes são desconectados. |
| `Radio_IsValid(radioid)` | 1 se a rádio existe. |
| `Radio_GetName(radioid, name[], size = sizeof name)` | Nome da rádio (também enviado como nome do stream). |

## Reprodução

`query` é um link do YouTube (watch, youtu.be, shorts, music, embed, live), um ID de vídeo ou texto de busca como `"artista - música"`. Links de outros sites são recusados.

| Native | Descrição |
|---|---|
| `Radio_Play(radioid, const query[])` | Toca agora: a música atual é trocada assim que a nova carrega. Retorna o ID do pedido (0 = recusado). Se vários `Radio_Play` estiverem carregando, vale o último. |
| `Radio_Queue(radioid, const query[])` | Coloca no fim da fila. Retorna o ID do pedido. |
| `Radio_Skip(radioid)` | Próxima música da fila. 0 se nada estiver tocando. |
| `Radio_Stop(radioid)` | Para e limpa a fila. Os ouvintes continuam conectados, ouvindo silêncio. |
| `Radio_ClearQueue(radioid)` | Limpa a fila; retorna quantos pedidos removeu. |
| `Radio_Pause(radioid)` / `Radio_Resume(radioid)` | Para todos ao mesmo tempo. |
| `Radio_Seek(radioid, position_ms)` | Pula para uma posição, para todos. |
| `Radio_IsPlaying(radioid)` / `Radio_IsPaused(radioid)` / `Radio_IsLoading(radioid)` | Estado. |
| `Radio_GetPosition(radioid)` / `Radio_GetDuration(radioid)` | Milissegundos. |
| `Radio_GetRequestId(radioid)` | ID do pedido da música atual. |
| `Radio_GetTitle(radioid, title[], size = sizeof title)` | Título da música atual. |
| `Radio_GetVideoId(radioid, videoid[], size = sizeof videoid)` | ID do vídeo atual no YouTube (11 caracteres). |
| `Radio_GetQueueSize(radioid)` | Pedidos na fila, inclusive os que ainda carregam. |
| `Radio_GetListenerCount(radioid)` | Conexões de stream abertas. |
| `Radio_GetPublicUrl(radioid, url[], size = sizeof url)` | URL do stream sem token. Precisa de `audiocast_allow_public 1`. |

**Tempo.** Os jogadores ouvem o stream cerca de `audiocast_burst_ms` (4 s) depois de o servidor produzi-lo. Os getters e os callbacks de música seguem o que os jogadores **ouvem**. Os comandos agem no servidor na hora e chegam aos ouvidos depois desse atraso.

## Volume

| Native | Descrição |
|---|---|
| `Radio_SetVolume(radioid, volume)` / `Radio_GetVolume(radioid)` | Volume de todos na rádio, de 0 (mudo) a 100 (padrão). |
| `Radio_SetPlayerVolume(playerid, volume)` / `Radio_GetPlayerVolume(playerid)` | Volume próprio de um jogador, de 0 a 100. Continua quando ele troca de rádio; volta a 100 quando ele sai do servidor. |

O volume que o jogador ouve é o da rádio vezes o dele (50 × 50 = 25%). Ele é aplicado alterando o ganho gravado em cada frame MP3, então não custa nada para o servidor e cada jogador pode ter um diferente. Só abaixa a música a partir do volume original, em degraus de 1,5 dB (50% = -6 dB, 10% ≈ -20 dB). As mudanças são ouvidas depois do atraso do burst.

## Ouvintes

| Função | Descrição |
|---|---|
| `Radio_AddPlayer(playerid, radioid, Float:posX = 0.0, Float:posY = 0.0, Float:posZ = 0.0, Float:distance = 50.0, bool:usepos = false)` | Sintoniza o jogador (um stock que chama `Radio_AttachPlayer` e `PlayAudioStreamForPlayer`). Com `usepos = true` o áudio vira som 3D naquela posição. Adicionar o jogador a outra rádio troca ele de rádio. |
| `Radio_RemovePlayer(playerid)` | Tira o jogador e para o áudio dele. |
| `Radio_AttachPlayer(playerid, radioid, url[], size = sizeof url)` | Baixo nível: cria a URL privada do jogador (`RADIO_URL_SIZE` células sempre bastam) sem tocá-la. |
| `Radio_DetachPlayer(playerid)` | Baixo nível: revoga a URL; o stream para. |
| `Radio_GetPlayerRadio(playerid)` | Rádio do jogador, 0 = nenhuma. |
| `Radio_IsPlayerStreaming(playerid)` | 1 enquanto o jogo do jogador está de fato conectado ao stream. 0 alguns segundos depois do `Radio_AddPlayer` normalmente indica porta fechada ou `audiocast_public_host` errado. |
| `Radio_FormatTime(milliseconds, output[], size = sizeof output)` | Formata como `m:ss`. |

## Busca

| Native | Descrição |
|---|---|
| `Radio_Search(const query[], max_results = 5)` | Busca no YouTube (1-20 resultados) sem tocar. Retorna o ID da busca; os resultados chegam em `OnRadioSearchResults`. |
| `Radio_GetSearchCount(searchid)` | Quantidade de resultados, -1 enquanto roda. |
| `Radio_GetSearchResult(searchid, index, videoid[], title[], &duration_ms, videoid_size = sizeof videoid, title_size = sizeof title)` | Um resultado. |
| `Radio_GetSearchChannel(searchid, index, channel[], size = sizeof channel)` | Canal do resultado. |
| `Radio_FreeSearch(searchid)` | Libera os resultados (também liberados quando o script descarrega). |

## Callbacks

Chamados em todos os scripts (gamemode e filterscripts) que os definem.

```pawn
forward OnRadioTrackLoaded(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackStart(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackEnd(radioid, requestid, reason);
forward OnRadioTrackError(radioid, requestid, const query[], const error[]);
forward OnRadioListenerConnect(radioid, playerid);
forward OnRadioListenerDisconnect(radioid, playerid);
forward OnRadioSearchResults(searchid, count, const error[]);
```

| Callback | Quando |
|---|---|
| `OnRadioTrackLoaded` | Uma música pedida está pronta para tocar (os primeiros segundos já foram convertidos). |
| `OnRadioTrackStart` | Os jogadores começam a ouvir uma música. |
| `OnRadioTrackEnd` | Os jogadores param de ouvi-la. `reason`: `RADIO_END_FINISHED`, `RADIO_END_SKIPPED`, `RADIO_END_STOPPED`, `RADIO_END_REPLACED`. |
| `OnRadioTrackError` | Um pedido falhou: link inválido, live, longo demais, bloqueio do YouTube... `error` diz o motivo (em inglês). |
| `OnRadioListenerConnect` / `Disconnect` | O jogo de um jogador abriu ou fechou o stream. `playerid` é `-1` para ouvintes públicos (VLC, navegador), então confira antes de usar. |
| `OnRadioSearchResults` | Uma busca terminou. `count` = 0 com `error` preenchido quando falhou. |

## Constantes

```pawn
#define INVALID_RADIO_ID   (0)
#define RADIO_URL_SIZE     (160)
#define RADIO_END_FINISHED (0)
#define RADIO_END_SKIPPED  (1)
#define RADIO_END_STOPPED  (2)
#define RADIO_END_REPLACED (3)
```

Defina `AUDIOCAST_NO_HOOKS` antes do include para desativar o hook de `OnPlayerDisconnect` (ele desconecta o jogador da rádio e reseta o volume dele; o SA-MP precisa dele, o open.mp faz isso sozinho).
