# Solução de Problemas

## Os jogadores não ouvem nada

Confira `Radio_IsPlayerStreaming(playerid)` alguns segundos depois do `Radio_AddPlayer`:

- **0 - o jogo nunca conectou ao stream.**
  - A porta TCP (`audiocast_port`, padrão 8000) está fechada. Libere no firewall *e* no painel da hospedagem; muitas hospedagens bloqueiam portas extras por padrão.
  - `audiocast_public_host` não está configurado ou está errado. O log do servidor avisa na inicialização quando ele falta (`stream URLs use 127.0.0.1`). Tem que ser o endereço que os jogadores usam para chegar ao servidor, não `127.0.0.1` nem IP de rede local.
  - Um proxy ou redirecionamento usa outra porta: configure `audiocast_public_port`.
  - Teste do seu PC: coloque `audiocast_allow_public 1` e abra `http://<public_host>:<porta>/radio/1.mp3` no VLC ou no navegador.
- **1 - conectado, mas em silêncio.** Nada está tocando (`Radio_IsPlaying`), o volume está em 0 (`Radio_GetVolume`, `Radio_GetPlayerVolume`) ou o volume de rádio do jogo do jogador está desligado (Opções → Áudio do jogo).

## Músicas não carregam

O motivo chega em `OnRadioTrackError(..., error[])` e no log do servidor.

| Erro | Causa e solução |
|---|---|
| `Sign in to confirm you're not a bot` | Comum em IPs de VPS/datacenter. Dê ao plugin um login do YouTube com `audiocast_cookies`: veja [Cookies do YouTube](Cookies-do-YouTube). |
| `video is live, longer than audiocast_max_duration or has no usable audio` | Lives não são suportadas. Para vídeos longos (sets de DJ) aumente `audiocast_max_duration`. |
| `Requested format is not available` | Versões do plugin anteriores ao suporte a AAC só tocavam vídeos com áudio Opus. Atualize o plugin. Se continuar, o YouTube mudou algo: o yt-dlp se atualiza todo dia; apague `scriptfiles/audiocast/bin/` para forçar um download novo agora. |
| `not a YouTube link, video ID or search` | Só o YouTube é suportado; links de outros sites são recusados. |
| `yt-dlp is not available yet` | O primeiro download do yt-dlp falhou (sem internet, ou sem `curl`/`wget` no Linux). Ele tenta de novo sozinho. |
| `Video unavailable` / `Private video` | Vídeo bloqueado na região, privado, removido ou com restrição de idade. Os com restrição de idade precisam de `audiocast_cookies`. |

Teste uma música fora do jogo com a ferramenta que vem no zip; ela roda o mesmo código do plugin:

```
tools/audio-cast-check "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
```

## O áudio chega alguns segundos atrasado

É de propósito. Os jogadores ouvem a rádio cerca de `audiocast_burst_ms` (4 s) depois de o servidor produzi-la: o cliente do jogo guarda alguns segundos de qualquer stream, e dar o mesmo buffer a todos é o que os mantém sincronizados. Pausa, skip, seek e volume chegam aos ouvidos com o mesmo atraso. Os getters e o `OnRadioTrackStart`/`End` já seguem o que os jogadores ouvem.

## Jogadores fora de sincronia

Todos na mesma rádio ficam a cerca de um frame (≈24 ms) uns dos outros. Se um jogador fica para trás, a conexão dele não acompanha o bitrate (cada ouvinte precisa de `audiocast_bitrate` kbps, 128 kbps ≈ 16 KB/s). Diminua `audiocast_bitrate` ou confira a banda de upload do servidor: 100 ouvintes a 128 kbps precisam de cerca de 13 Mbit/s.

## Acentos aparecem errados no chat

No padrão, os títulos são convertidos para Windows-1252, que o chat do SA-MP mostra. Se o seu script (ou uma biblioteca, como um renderizador de textdraw UTF-8) espera UTF-8, configure `audiocast_utf8 1`.

## "503 Service Unavailable"

Há `audiocast_max_listeners` (padrão 500) conexões de stream abertas. Aumente se o servidor e a banda permitirem.

## Músicas demoram para começar

- Normal: 3-5 s depois do `Radio_Play`; busca por texto soma cerca de 1 s.
- `audiocast_normalize 1` baixa a música inteira antes de tocar.
- Com `audiocast_cache_minutes`, repetir uma música é instantâneo.
- Uma conexão lenta ou bloqueada entre a hospedagem e o YouTube também aparece aqui; meça o tempo do `audio-cast-check` no servidor.
