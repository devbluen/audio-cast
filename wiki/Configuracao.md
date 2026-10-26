# Configuração

Cada opção pode vir de três lugares. Vale o primeiro encontrado:

1. variável de ambiente: `AUDIOCAST_PORT`, `AUDIOCAST_PUBLIC_HOST`...
2. `config.json` do open.mp: `"audiocast_port": 8000`, ou dentro de uma seção `"audiocast": { "port": 8000 }`;
3. `server.cfg`: `audiocast_port 8000`.

## Opções

| Opção | Padrão | Descrição |
|---|---|---|
| `audiocast_public_host` | `network.public_addr` / `bind` | IP ou domínio público que vai na URL enviada aos jogadores. **Configure isso.** |
| `audiocast_port` | `8000` | Porta TCP do servidor de stream embutido. |
| `audiocast_public_port` | = `port` | Porta na URL, quando um proxy ou redirecionamento usa outra. |
| `audiocast_bind` | `0.0.0.0` | Interface em que o servidor de stream escuta. |
| `audiocast_burst_ms` | `4000` | Áudio enviado a cada ouvinte ao conectar. Mantém todos sincronizados e é também o quanto os jogadores ouvem atrás do servidor. Não use menos que cerca de 3000 (o buffer do próprio cliente do jogo). |
| `audiocast_bitrate` | `128` | kbps do MP3, 32-320. Cada ouvinte consome essa banda (128 kbps ≈ 16 KB/s). |
| `audiocast_normalize` | `0` | `1` = iguala o volume entre as músicas (cerca de -16 dBFS RMS, sem distorcer). As músicas demoram mais para começar: o arquivo inteiro é baixado antes. |
| `audiocast_ytdlp` | `auto` | `auto` = yt-dlp gerenciado, atualizado todo dia. Ou o caminho de um yt-dlp seu (usado como está, nunca atualizado). |
| `audiocast_js_runtime` | `auto` | Runtime de JavaScript que o yt-dlp usa para resolver os desafios do YouTube. `auto` = o plugin baixa e atualiza o [Deno](https://deno.com) (~100 MB). `off` = nenhum. Ou um valor do `--js-runtimes` do yt-dlp, como `node` ou `deno:/caminho/do/deno`. |
| `audiocast_cookies` | | Arquivo de cookies do YouTube, para quando o YouTube pede "confirme que não é um robô". Passo a passo: [Cookies do YouTube](Cookies-do-YouTube). |
| `audiocast_data_dir` | `scriptfiles/audiocast` | yt-dlp e Deno gerenciados e arquivos temporários de vida curta. Músicas nunca são gravadas em disco. |
| `audiocast_cache_minutes` | `0` | Mantém a música em memória por esse tempo depois que nenhuma rádio a usa, para tocar de novo na hora. |
| `audiocast_max_duration` | `1800` | Duração máxima aceita, em segundos. Lives são sempre recusadas. |
| `audiocast_download_timeout` | `300` | Tempo limite de cada download, em segundos. |
| `audiocast_workers` | `2` | Downloads simultâneos. |
| `audiocast_max_listeners` | `500` | Total de conexões de stream. As que passarem recebem `503`. |
| `audiocast_allow_public` | `0` | `1` = permite `http://host:porta/radio/<id>.mp3` sem token de jogador (VLC, navegador, bot de Discord). |
| `audiocast_utf8` | `0` | `1` = troca texto com o Pawn em UTF-8. No padrão os títulos são convertidos para Windows-1252, que o chat do SA-MP mostra corretamente. |
| `audiocast_metadata_codepage` | `1252` | Código de página do título que o cliente do jogo mostra no stream: um código de página do Windows de `1250` a `1258` (`1250` Europa central, `1251` cirílico, `1252` Europa ocidental, ...) ou `0` para ASCII puro. Letras que faltam no código de página perdem o acento. |
| `audiocast_log_songs` | `0` | `1` = escreve "loaded <id> (<título>)" no console do servidor a cada música. Erros e avisos sempre aparecem. |

## Exemplos

`server.cfg`:

```
plugins audio-cast.so
audiocast_public_host radio.exemplo.com.br
audiocast_port 8000
audiocast_bitrate 128
audiocast_cache_minutes 10
```

`config.json` (open.mp):

```json
"audiocast": {
    "public_host": "radio.exemplo.com.br",
    "port": 8000,
    "cache_minutes": 10
}
```

## Atrás de proxy ou redirecionamento de porta

Se os jogadores chegam ao stream por uma porta diferente da que o servidor escuta (Docker, NAT, proxy reverso), configure as duas:

```
audiocast_port 8000            # onde o plugin escuta
audiocast_public_port 28000    # o que vai na URL
```

## Memória e disco

As músicas só existem em memória, cerca de 1 MB por minuto de música, enquanto alguma rádio toca ou tem a música na fila (mais `audiocast_cache_minutes`). Uma música tocada em várias rádios é baixada uma vez só. No disco ficam só os programas yt-dlp (~18 MB) e Deno (~100 MB).
