# Primeiros Passos

## 1. Download

Baixe o zip do sistema do seu servidor em [Releases](https://github.com/devbluen/audio-cast/releases):

| Servidor | Arquivo |
|---|---|
| Windows | `audio-cast-<versão>-windows-x86.zip` |
| Linux | `audio-cast-<versão>-linux-x86.zip` |

O zip contém:

```
components/audio-cast.dll|.so   open.mp
plugins/audio-cast.dll|.so      SA-MP (o mesmo arquivo)
include/audio-cast.inc          include do Pawn
tools/audio-cast-check          testa a instalação
examples/radio.pwn              filterscript completo
licenses/                       licenças de terceiros
```

## 2. Instalação

**open.mp:** copie `components/audio-cast.dll` (ou `.so`) para a pasta `components/` do servidor.

**SA-MP:** copie `plugins/audio-cast.dll` (ou `.so`) para `plugins/` e adicione ao `server.cfg`:

```
plugins audio-cast.dll
```

Copie `include/audio-cast.inc` para `qawno/include` (open.mp) ou `pawno/include` (SA-MP).

Não precisa instalar mais nada: a conversão de áudio é interna ao plugin, e o plugin baixa e atualiza sozinho o [yt-dlp](https://github.com/yt-dlp/yt-dlp) e o [Deno](https://deno.com) (o runtime de JavaScript que o yt-dlp precisa para o YouTube) em `scriptfiles/audiocast/`. A primeira inicialização baixa cerca de 60 MB. No Linux esse download precisa de `curl` ou `wget`.

## 3. Libere a porta e configure o endereço público

Os jogadores baixam o áudio direto do seu servidor, então:

1. Libere a **porta TCP 8000** (ou a de `audiocast_port`) no firewall / painel da hospedagem.
2. Informe o endereço que os jogadores usam para chegar ao servidor:

```
audiocast_public_host 203.0.113.10
```

Sem isso as URLs do stream apontam para `127.0.0.1` e só funcionam na própria máquina do servidor. Veja todas as opções em [Configuração](Configuracao).

## 4. Primeira rádio

```pawn
#include <open.mp>      // ou <a_samp>
#include <audio-cast>

new g_Radio;

public OnGameModeInit()
{
    g_Radio = Radio_Create("Rádio do Servidor");
    Radio_Play(g_Radio, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    Radio_Queue(g_Radio, "tiago iorc amei te ver");   // busca por texto também funciona
    return 1;
}

public OnPlayerSpawn(playerid)
{
    Radio_AddPlayer(playerid, g_Radio);   // começa na minutagem atual
    return 1;
}
```

Inicie o servidor e dê spawn: a música começa alguns segundos depois. Se não ouvir nada, veja [Solução de Problemas](Solucao-de-Problemas).

## 5. Próximos passos

- [Receitas](Receitas): comando /radio, diálogo de busca, caixa de som, volume por jogador, várias rádios.
- [Referência da API](Referencia-da-API): todas as natives e callbacks.
- `examples/radio.pwn`: filterscript pronto com `/radio`, `/search`, `/volume`, `/play`, `/queue`, `/skip`...
