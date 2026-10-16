# AudioCast

[English](README.md) | **Português** | [Wiki](https://github.com/devbluen/audio-cast/wiki)

Rádio sincronizada do YouTube para **open.mp** e **SA-MP**, 100% server-side.

Você informa uma música do YouTube (link ou busca) e todos os jogadores sintonizados escutam **o mesmo trecho, ao mesmo tempo**. Quem entra na rádio no meio da música começa na minutagem atual, como numa rádio de verdade. Não precisa de nada no cliente: o jogo toca o áudio com o `PlayAudioStreamForPlayer` nativo.

O mesmo arquivo (`audio-cast.dll` / `audio-cast.so`) funciona como componente do open.mp e como plugin do SA-MP.

## Como funciona

```
 Radio_Play("link")                         servidor (plugin)                         jogadores
 ─────────────────►  yt-dlp ─► decoder Opus/AAC ─► encoder MP3 ─► relógio da rádio ─► HTTP :8000 ─► PlayAudioStreamForPlayer
                    (gerenciado)   (interno)       (interno)      (só em memória)
```

1. O **yt-dlp** baixa o áudio da música direto para a memória. Os decoders (libopus para Opus, fdk-aac para AAC) e o encoder MP3 (LAME), embutidos no plugin, convertem tudo para o formato do stream (MP3 48 kHz estéreo, bitrate fixo) **enquanto baixa**: a música começa assim que os primeiros 3 segundos ficam prontos, normalmente 3–5 s depois do `Radio_Play`. Nada é gravado em disco.
2. Cada rádio tem **um relógio único**: os frames de áudio saem em tempo real, exatamente 1× a velocidade da música, tenha ouvinte ou não. Entre uma música e outra sai silêncio, então a conexão nunca cai.
3. O plugin tem um servidor HTTP embutido, no estilo Shoutcast/Icecast. Cada jogador recebe uma URL própria (com token). Ao conectar, ele recebe o mesmo "burst" de áudio recente que todos os outros receberam e, a partir daí, o ao vivo.

Como todo mundo recebe o mesmo burst e o mesmo fluxo em tempo real, todos tocam o mesmo frame no mesmo instante. Nos testes de rede, um ouvinte que entrou 1,7 s depois ficou alinhado com o primeiro com erro de cerca de 1 frame (≈24 ms).

## Requisitos no servidor

Não precisa instalar nada. Só é preciso uma **porta TCP** liberada no firewall (padrão `8000`), porque os jogadores baixam o stream dela.

- **Conversão de áudio:** é interna ao plugin (libopus + fdk-aac + LAME, linkadas estaticamente). Não precisa de ffmpeg.
- **yt-dlp:** é gerenciado pelo plugin. Na primeira inicialização ele baixa a build oficial do yt-dlp para o seu sistema e processador em `scriptfiles/audiocast/bin/` e instala as versões novas (confere uma vez por dia). É usada a build descompactada, que abre cerca de 1 segundo mais rápido em cada música. O yt-dlp continua sendo um programa separado de propósito: o YouTube muda com frequência e o yt-dlp recebe correções em dias, sem precisar esperar uma nova versão do plugin.
  - No Windows o download usa o WinHTTP do próprio sistema. No Linux, esse primeiro download precisa de `curl` ou `wget` (presentes em praticamente qualquer hospedagem).
  - Prefere usar o seu? Coloque o caminho em `audiocast_ytdlp`. O plugin usa como está e não o atualiza.
- **Deno:** é gerenciado do mesmo jeito, em `scriptfiles/audiocast/deno/` (~100 MB). O YouTube esconde o áudio atrás de desafios em JavaScript, e o yt-dlp precisa de um runtime de JavaScript para resolvê-los; sem ele muitos vídeos falham com `The page needs to be reloaded` ou `Sign in to confirm you're not a bot`. O Deno só tem builds 64 bits (Windows, e Linux com glibc); nos outros casos instale o Node.js e configure `audiocast_js_runtime node`.

## Download

Baixe o zip do sistema do seu servidor na página de [Releases](https://github.com/devbluen/audio-cast/releases):

- `audio-cast-<versão>-windows-x86.zip` - servidores Windows
- `audio-cast-<versão>-linux-x86.zip` - servidores Linux

Cada zip traz `components/` (open.mp), `plugins/` (SA-MP), `include/audio-cast.inc`, `tools/audio-cast-check`, `examples/radio.pwn` e `licenses/`. Builds de desenvolvimento de cada commit também ficam disponíveis como artefatos do workflow *Build AudioCast*, na aba Actions, um por plataforma.

## Instalação

**open.mp:** copie `audio-cast.dll` (Windows) ou `audio-cast.so` (Linux) para `components/`.

**SA-MP:** copie para `plugins/` e adicione ao `server.cfg`:

```
plugins audio-cast.dll        # Windows
plugins audio-cast.so         # Linux
```

Copie `audio-cast.inc` para a pasta de includes do compilador (`qawno/include` ou `pawno/include`).

Depois configure pelo menos o endereço público do servidor (veja abaixo).

## Configuração

Cada opção pode vir de três lugares, nesta ordem:

1. variável de ambiente (`AUDIOCAST_PORT`, `AUDIOCAST_PUBLIC_HOST`…);
2. `config.json` do open.mp, no formato `"audiocast_port": 8000` ou na seção `"audiocast": { "port": 8000 }`;
3. `server.cfg`, no formato `audiocast_port 8000`.

| Opção | Padrão | Descrição |
|---|---|---|
| `audiocast_public_host` | `network.public_addr` / `bind` | IP ou domínio **público** que vai na URL enviada ao jogador. **Configure isso.** Sem ele as URLs usam `127.0.0.1` e só funcionam na sua máquina. |
| `audiocast_port` | `8000` | Porta TCP do servidor de stream. |
| `audiocast_public_port` | = `port` | Porta na URL, se houver redirecionamento/proxy. |
| `audiocast_bind` | `0.0.0.0` | Interface em que o stream escuta. |
| `audiocast_burst_ms` | `4000` | Áudio recente enviado a cada ouvinte ao conectar. Todos recebem o mesmo valor, por isso ficam sincronizados. É também o quanto os jogadores ouvem "atrás" do servidor (veja *Tempo*). Valores menores encurtam esse atraso, mas não devem ficar abaixo do buffer do próprio cliente do jogo (cerca de 3–4 s), senão quem entra começa com um buraco. |
| `audiocast_bitrate` | `128` | kbps do MP3 (32–320). |
| `audiocast_normalize` | `0` | `1` = iguala o volume entre as músicas (cerca de -16 dBFS RMS, sem distorcer). A música inteira é baixada antes de tocar. |
| `audiocast_ytdlp` | `auto` | `auto` = o plugin baixa e atualiza o yt-dlp. Ou o caminho de um yt-dlp seu. |
| `audiocast_js_runtime` | `auto` | Runtime de JavaScript que o yt-dlp usa para resolver os desafios do YouTube. `auto` = o plugin baixa e atualiza o [Deno](https://deno.com) (~100 MB). `off` = nenhum. Ou um valor do `--js-runtimes` do yt-dlp, como `node` ou `deno:/caminho/do/deno`. |
| `audiocast_cookies` | | Arquivo de cookies do YouTube (`cookies.txt`, caminho relativo à pasta do servidor), para quando o YouTube pede "confirme que não é um robô" (comum em VPS/datacenter). Passo a passo: [wiki](https://github.com/devbluen/audio-cast/wiki/Cookies-do-YouTube). |
| `audiocast_data_dir` | `scriptfiles/audiocast` | Guarda o yt-dlp (~18 MB) e o Deno (~100 MB) gerenciados e arquivos temporários de vida curta. Músicas nunca são salvas aqui. |
| `audiocast_cache_minutes` | `0` | Quanto tempo uma música fica **em memória** depois que nenhuma rádio a usa, para tocar de novo na hora. `0` libera imediatamente. |
| `audiocast_max_duration` | `1800` | Duração máxima aceita, em segundos. Lives são sempre recusadas. |
| `audiocast_download_timeout` | `300` | Tempo limite de cada download/conversão, em segundos. |
| `audiocast_workers` | `2` | Downloads simultâneos. |
| `audiocast_max_listeners` | `500` | Conexões de stream simultâneas no total. |
| `audiocast_allow_public` | `0` | `1` = permite abrir `http://host:porta/radio/<id>.mp3` sem token (VLC, navegador, Discord bot…). |
| `audiocast_utf8` | `0` | `1` = troca texto com o Pawn em UTF-8. No padrão, títulos são convertidos para Windows-1252, que é o que o chat do SA-MP mostra (acentos funcionam). |

Exemplo de `server.cfg`:

```
plugins audio-cast.dll
audiocast_public_host 203.0.113.10
audiocast_port 8000
```

Exemplo de `config.json` (open.mp):

```json
"audiocast": {
    "public_host": "203.0.113.10",
    "port": 8000
}
```

## Uso rápido

```pawn
#include <open.mp>      // ou <a_samp>
#include <audio-cast>

new g_Radio;

public OnGameModeInit()
{
    g_Radio = Radio_Create("Rádio do Servidor");
    Radio_Play(g_Radio, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    Radio_Queue(g_Radio, "tiago iorc amei te ver");   // busca também funciona
    return 1;
}

// /radio
Radio_AddPlayer(playerid, g_Radio);     // entra: começa na minutagem atual
Radio_RemovePlayer(playerid);           // sai

public OnRadioTrackStart(radioid, requestid, const title[], duration_ms)
{
    // avisar os ouvintes, atualizar textdraw, etc.
    return 1;
}
```

Há um filterscript completo, com comandos (`/radio`, `/search`, `/volume`, `/play`...), em [examples/radio.pwn](examples/radio.pwn). As mensagens dele no jogo estão em inglês.

## API

### Rádios

| Native | Retorno |
|---|---|
| `Radio_Create(const name[] = "")` | ID da rádio. Ela pertence ao script que a criou e é destruída quando ele descarrega. |
| `Radio_Destroy(radioid)` | 1/0 |
| `Radio_IsValid(radioid)` | 1/0 |
| `Radio_GetName(radioid, name[], size)` | 1/0 |

### Reprodução

`query` pode ser um link do YouTube (watch, youtu.be, shorts, music, embed, live), um ID de vídeo ou texto de busca. Links de outros sites são recusados.

| Native | Descrição |
|---|---|
| `Radio_Play(radioid, const query[])` | Toca agora: a música atual é trocada assim que a nova termina de carregar. Retorna o ID do pedido (0 = falhou). Se vários `Radio_Play` carregarem ao mesmo tempo, vale o último. |
| `Radio_Queue(radioid, const query[])` | Coloca no fim da fila. Retorna o ID do pedido. |
| `Radio_Skip(radioid)` | Pula para a próxima da fila. |
| `Radio_Stop(radioid)` | Para a música e limpa a fila. Os ouvintes continuam conectados (silêncio). |
| `Radio_ClearQueue(radioid)` | Limpa a fila. Retorna quantos pedidos removeu. |
| `Radio_Pause(radioid)` / `Radio_Resume(radioid)` | Pausa e continua para todos ao mesmo tempo. |
| `Radio_Seek(radioid, position_ms)` | Pula para uma posição, para todos. |
| `Radio_IsPlaying` / `Radio_IsPaused` / `Radio_IsLoading` | Estado da rádio. |
| `Radio_GetPosition(radioid)` / `Radio_GetDuration(radioid)` | Em milissegundos. |
| `Radio_GetTitle(radioid, title[], size)` / `Radio_GetVideoId(...)` | Música atual. |
| `Radio_GetRequestId(radioid)` | ID do pedido da música atual. |
| `Radio_GetQueueSize(radioid)` | Itens na fila (inclusive os que estão carregando). |
| `Radio_GetListenerCount(radioid)` | Conexões ativas no stream. |
| `Radio_GetPublicUrl(radioid, url[], size)` | URL sem token (precisa de `audiocast_allow_public 1`). |
| `Radio_SetVolume(radioid, volume)` / `Radio_GetVolume(radioid)` | Volume de todos na rádio, de 0 (mudo) a 100 (padrão). Ouvido depois do burst que os jogadores já têm (cerca de `audiocast_burst_ms`). |

Os getters (`IsPlaying`, `IsPaused`, `GetPosition`, `GetTitle`, `GetRequestId`…) descrevem **o que os jogadores estão ouvindo agora**, e não o que o servidor está produzindo (veja *Tempo*).

### Busca

Lista resultados do YouTube sem tocar nada, por exemplo para mostrar um diálogo em que o jogador escolhe a música. Toque a escolhida com `Radio_Play`/`Radio_Queue` e o ID do vídeo.

| Native | Descrição |
|---|---|
| `Radio_Search(const query[], max_results = 5)` | Inicia uma busca (1–20 resultados). Retorna o ID da busca (0 = falhou). Os resultados chegam em `OnRadioSearchResults`. |
| `Radio_GetSearchCount(searchid)` | Quantidade de resultados, ou -1 enquanto a busca está rodando. |
| `Radio_GetSearchResult(searchid, index, videoid[], title[], &duration_ms, ...)` | Um resultado: ID do vídeo, título e duração. |
| `Radio_GetSearchChannel(searchid, index, channel[], size)` | Canal do resultado. |
| `Radio_FreeSearch(searchid)` | Libera os resultados. As buscas também são liberadas quando o script descarrega. |

```pawn
// /search <nome da música>
g_PlayerSearch[playerid] = Radio_Search(params, 8);

public OnRadioSearchResults(searchid, count, const error[])
{
    new videoid[16], title[96], duration;
    for (new i = 0; i < count; i++)
    {
        Radio_GetSearchResult(searchid, i, videoid, title, duration);
        // adiciona uma linha no diálogo...
    }
}
// quando o jogador escolhe a linha `listitem`:
Radio_GetSearchResult(searchid, listitem, videoid, title, duration);
Radio_Play(g_Radio, videoid);
```

### Ouvintes

| Função | Descrição |
|---|---|
| `Radio_AddPlayer(playerid, radioid, Float:x = 0.0, Float:y = 0.0, Float:z = 0.0, Float:distance = 50.0, bool:usepos = false)` | Sintoniza o jogador. Com posição, vira som 3D (caixa de som, balada…). |
| `Radio_RemovePlayer(playerid)` | Tira o jogador da rádio e para o áudio. |
| `Radio_GetPlayerRadio(playerid)` | Rádio do jogador (0 = nenhuma). |
| `Radio_IsPlayerStreaming(playerid)` | 1 enquanto o cliente do jogador está de fato conectado ao stream. Útil para detectar porta bloqueada. |
| `Radio_SetPlayerVolume(playerid, volume)` / `Radio_GetPlayerVolume(playerid)` | Volume de um jogador, de 0 a 100, em cima do da rádio (50 numa rádio em 50 = 25). Continua quando ele troca de rádio e volta a 100 quando sai do servidor. |
| `Radio_AttachPlayer` / `Radio_DetachPlayer` | Versões de baixo nível (só geram ou revogam a URL do jogador). |
| `Radio_FormatTime(ms, output[], size)` | Formata `m:ss`. |

### Volume

O `PlayAudioStreamForPlayer` não tem volume, então o plugin abaixa o volume no próprio stream: ele altera o ganho gravado em cada frame MP3, sem recodificar. Cada jogador pode ter o seu volume na mesma rádio.

- O volume que o jogador ouve é o da rádio vezes o dele: `Radio_SetVolume(radio, 50)` mais `Radio_SetPlayerVolume(player, 50)` = 25%.
- 0 é mudo e 100 é o volume original da música; não dá para passar disso.
- Os degraus são de 1,5 dB, então valores próximos podem soar iguais (50% = -6 dB, 10% ≈ -20 dB).
- Como pausa e skip, a mudança é ouvida depois do burst que o jogador já tem (cerca de `audiocast_burst_ms`).

```pawn
// /volume <0-100>
Radio_SetPlayerVolume(playerid, strval(params));
```

### Callbacks

```pawn
forward OnRadioTrackLoaded(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackStart(radioid, requestid, const title[], duration_ms);
forward OnRadioTrackEnd(radioid, requestid, reason);  // RADIO_END_FINISHED / SKIPPED / STOPPED / REPLACED
forward OnRadioTrackError(radioid, requestid, const query[], const error[]);
forward OnRadioListenerConnect(radioid, playerid);
forward OnRadioListenerDisconnect(radioid, playerid);
forward OnRadioSearchResults(searchid, count, const error[]);
```

`OnRadioTrackLoaded` é chamado quando a música fica pronta para tocar; `OnRadioTrackStart`/`OnRadioTrackEnd`, quando os jogadores **ouvem** ela começar ou terminar.

Os callbacks são chamados em todos os scripts (gamemode e filterscripts) que os definem.

## Testando a instalação

A ferramenta `audio-cast-check` carrega uma música exatamente como o plugin faz e, opcionalmente, toca a rádio por alguns segundos para você ouvir no VLC:

```
audio-cast-check "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
audio-cast-check "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast 8000 60
  → abra http://127.0.0.1:8000/radio/1.mp3 no VLC
audio-cast-check --search "tiago iorc" auto scriptfiles/audiocast
audio-cast-check --cookies scriptfiles/audiocast/cookies.txt "https://youtu.be/dQw4w9WgXcQ" auto scriptfiles/audiocast
```

## Observações

- **Tempo:** o cliente do jogo guarda alguns segundos de qualquer stream de áudio antes de tocar, e o AudioCast mantém todo mundo sincronizado dando a todos o mesmo buffer de `audiocast_burst_ms`. Por isso os jogadores ouvem a rádio cerca de 4 s depois de o servidor produzi-la. Os getters e o `OnRadioTrackStart`/`End` acompanham o que os jogadores ouvem, então as mensagens no chat batem com o áudio. Os comandos (`Radio_Pause`, `Skip`, `Seek`, `Play`, o volume) agem no servidor na hora e **chegam aos ouvidos depois desse atraso**: depois de pausar, a música ainda toca cerca de 4 s; depois de retomar, ela volta cerca de 4 s depois. Isso é inerente a transmitir para o cliente do jogo sem modificações.
- **Espaço em disco:** as músicas só existem em memória (cerca de 1 MB por minuto de música) enquanto alguma rádio toca ou tem a música na fila, mais o tempo de `audiocast_cache_minutes`. Depois disso são liberadas. No disco ficam só os programas yt-dlp e Deno. Sobras de um crash e músicas salvas pela versão 1.0 são apagadas na inicialização.
- **Tempo de carregamento:** a música fica pronta para tocar 3–5 s depois do `Radio_Play` (buscar por texto na mesma chamada soma cerca de 1 s); o resto carrega enquanto toca. Se duas rádios tocarem a mesma música, ela é baixada uma vez só. Com `audiocast_normalize 1` a música inteira precisa ser baixada antes, então demora mais.
- **Formatos:** o plugin usa o áudio Opus do YouTube, que praticamente todo vídeo tem. Vídeos sem ele (muitas vezes uploads recentes) tocam pelo áudio AAC (m4a).
- **Cortes limpos:** cada frame MP3 é codificado de forma independente (sem reservatório de bits), então pausar, pular, avançar e entrar no meio da música nunca geram estalos.
- **Bloqueio do YouTube:** em VPS é comum o YouTube pedir login. Exporte os cookies do navegador para um arquivo `cookies.txt` e aponte `audiocast_cookies` para ele; o log do servidor confirma se o arquivo foi encontrado e se tem login. Passo a passo: [Cookies do YouTube](https://github.com/devbluen/audio-cast/wiki/Cookies-do-YouTube).
- **Segurança:** o plugin nunca usa shell. O yt-dlp é chamado direto com argumentos, e só IDs de vídeo validados viram URL, então o texto digitado por jogadores não consegue injetar comandos. Cada jogador recebe um token próprio na URL, que é invalidado quando ele sai.
- **Banda:** cada ouvinte consome o bitrate configurado (128 kbps ≈ 16 KB/s).

## Compilando

No Windows, um único script compila **Windows e Linux** e junta tudo em `dist/`:

```powershell
.\build.ps1                         # Windows + Linux, versão 1.0.0
.\build.ps1 -Version 1.2.0          # versão gravada nos binários e no nome dos zips
.\build.ps1 -Target linux           # só uma plataforma (windows | linux)
.\build.ps1 -Clean -SkipTests       # recompila do zero, sem os testes
```

O script só precisa do **Visual Studio 2022** (carga de trabalho "Desenvolvimento para desktop com C++") e do **CMake**. O resto é baixado com hash SHA-256 conferido:

- **Linux:** o binário Linux é cross-compilado com o [Zig](https://ziglang.org/), baixado em `.tools/` no primeiro uso. Não precisa de máquina Linux, WSL nem Docker. O alvo é a glibc 2.17+, então carrega em praticamente qualquer hospedagem Linux.
- **Bibliotecas:** asio, libopus, fdk-aac e LAME são baixadas e compiladas pelo CMake (`cmake/dependencies.cmake`) e linkadas estaticamente.

A primeira execução leva alguns minutos; as seguintes só recompilam o que mudou. Resultado:

```
dist/
  windows/components/audio-cast.dll    → servidor open.mp: components/
  windows/plugins/audio-cast.dll       → servidor SA-MP:   plugins/
  windows/tools/audio-cast-check.exe
  linux/components/audio-cast.so       → servidor open.mp: components/
  linux/plugins/audio-cast.so          → servidor SA-MP:   plugins/
  linux/tools/audio-cast-check
  include/audio-cast.inc               → qawno/include ou pawno/include
  examples/radio.pwn
  audio-cast-<versão>-windows-x86.zip  pacotes prontos para distribuir
  audio-cast-<versão>-linux-x86.zip
```

Numa máquina Linux também dá para compilar nativamente (precisa de `gcc-multilib`/`g++-multilib`, CMake e Ninja):

```bash
cmake --preset linux-x86 && cmake --build --preset linux-x86 && ctest --preset linux-x86
```

Os servidores SA-MP e open.mp são 32 bits, então toda build é x86.

O GitHub Actions (`.github/workflows/build.yml`) compila cada plataforma num job próprio a cada push e publica um artefato por plataforma. Rodando o workflow manualmente com **Publish a GitHub release** marcado, ele também cria a release `v<versão>` com os dois zips, depois que as builds e os testes Linux passam.

O plugin inclui a LAME (LGPL), a libopus (BSD) e a biblioteca Fraunhofer FDK AAC (licença FDK). Se você distribuir o binário, siga os termos da LGPL para a LAME: por exemplo, publique o código-fonte deste projeto ou linke a LAME de forma substituível. A licença FDK pede que o texto dela acompanhe o binário (os zips de release trazem em `licenses/fdk-aac.txt`) e que o código-fonte da biblioteca esteja disponível ([github.com/mstorsjo/fdk-aac](https://github.com/mstorsjo/fdk-aac), sem modificações). Ela não concede direitos de patente do AAC.
