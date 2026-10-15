# Compilando

## No Windows (as duas plataformas)

Um único script compila **Windows e Linux** e junta tudo em `dist/`:

```powershell
.\build.ps1                         # Windows + Linux, versão 1.0.0
.\build.ps1 -Version 1.2.0          # versão gravada nos binários e no nome dos zips
.\build.ps1 -Target linux           # só uma plataforma (windows | linux)
.\build.ps1 -Clean -SkipTests       # do zero, sem os testes
```

Requisitos: **Visual Studio 2022** com "Desenvolvimento para desktop com C++" e **CMake**. O resto é baixado e conferido por SHA-256:

- [Zig](https://ziglang.org/), em `.tools/`, faz a cross-compilação do binário Linux (glibc 2.17+). Não precisa de máquina Linux, WSL nem Docker.
- asio, libopus, fdk-aac, LAME e miniz são baixadas pelo CMake (`cmake/dependencies.cmake`) e linkadas estaticamente.

A primeira execução leva alguns minutos. Se uma pasta de build foi configurada por uma versão antiga, rode uma vez com `-Clean`.

## No Linux

Precisa de `gcc-multilib`/`g++-multilib`, CMake e Ninja:

```bash
cmake --preset linux-x86 && cmake --build --preset linux-x86 && ctest --preset linux-x86
```

Toda build é 32 bits (x86), como os servidores SA-MP e open.mp.

## Testes

| Teste | Cobre |
|---|---|
| `audio-cast-tests` | Interpretação de links e buscas, texto, frames MP3, o relógio e a fila da rádio. |
| `audio-cast-stream-tests` | O servidor HTTP por loopback: ouvintes que entram em momentos diferentes ficam sincronizados, pausa/retomada, volume da rádio e por ouvinte. |
| `audio-cast-transcoder-tests` | Decodificação de Opus/WebM e AAC/MP4 (AAC-LC, HE-AAC, mono, fragmentos) chegando em pedaços, codificação MP3, volume. |

## GitHub Actions

O `.github/workflows/build.yml` roda a cada push e pull request:

- um job por plataforma roda `build.ps1 -Target <plataforma>` no Windows e publica seu próprio artefato (`audio-cast-<versão>-windows-x86`, `audio-cast-<versão>-linux-x86`);
- um job Linux 32 bits nativo roda os testes e confere os símbolos exportados.

**Publicar uma release:** Actions → *Build AudioCast* → *Run workflow*, informe a versão e marque **Publish a GitHub release**. Depois que as duas builds e os testes Linux passam, ele cria a tag e a release `v<versão>` com os dois zips e notas geradas automaticamente. Ele não sobrescreve uma release que já existe.

**A wiki** é publicada a partir da pasta `wiki/` pelo `.github/workflows/wiki.yml` sempre que essa pasta muda na `main`.

## Licenças

O plugin inclui a LAME (LGPL), a libopus (BSD) e a biblioteca Fraunhofer FDK AAC (licença FDK). Ao distribuir binários, siga a LGPL da LAME (por exemplo, publique o código-fonte deste projeto) e mantenha o texto da licença FDK junto com o binário; os zips de release trazem em `licenses/fdk-aac.txt`. A licença FDK não concede direitos de patente do AAC.
