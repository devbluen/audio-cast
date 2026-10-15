# Building

## On Windows (both platforms)

One script builds **Windows and Linux** and collects everything in `dist/`:

```powershell
.\build.ps1                         # Windows + Linux, version 1.0.0
.\build.ps1 -Version 1.2.0          # version embedded in the binaries and zip names
.\build.ps1 -Target linux           # one platform only (windows | linux)
.\build.ps1 -Clean -SkipTests       # from scratch, without the tests
```

Requirements: **Visual Studio 2022** with "Desktop development with C++", and **CMake**. Everything else is downloaded and checked by SHA-256:

- [Zig](https://ziglang.org/), into `.tools/`, cross-compiles the Linux binary (glibc 2.17+). No Linux machine, WSL or Docker needed.
- asio, libopus, fdk-aac, LAME and miniz are downloaded by CMake (`cmake/dependencies.cmake`) and linked statically.

The first run takes a few minutes. If a build folder was configured by an older version, run once with `-Clean`.

## On Linux

Needs `gcc-multilib`/`g++-multilib`, CMake and Ninja:

```bash
cmake --preset linux-x86 && cmake --build --preset linux-x86 && ctest --preset linux-x86
```

Every build is 32-bit (x86), like SA-MP and open.mp servers.

## Tests

| Test | Covers |
|---|---|
| `audio-cast-tests` | Query parsing, text, MP3 frames, the radio clock and queue. |
| `audio-cast-stream-tests` | The HTTP server over loopback: listeners joining at different times stay in sync, pause/resume, radio and per-listener volume. |
| `audio-cast-transcoder-tests` | Opus/WebM and AAC/MP4 (AAC-LC, HE-AAC, mono, fragments) decoding while arriving in pieces, MP3 encoding, volume. |

## GitHub Actions

`.github/workflows/build.yml` runs on every push and pull request:

- one job per platform runs `build.ps1 -Target <platform>` on Windows and uploads its own artifact (`audio-cast-<version>-windows-x86`, `audio-cast-<version>-linux-x86`);
- a native 32-bit Linux job runs the tests and checks the exported symbols.

**Publishing a release:** Actions → *Build AudioCast* → *Run workflow*, enter the version and check **Publish a GitHub release**. After both builds and the Linux tests pass, it creates the `v<version>` tag and release with both zips and generated notes. It refuses to overwrite an existing release.

**The wiki** is published from the `wiki/` folder by `.github/workflows/wiki.yml` whenever that folder changes on `main`.

## Licenses

The plugin links LAME (LGPL), libopus (BSD) and the Fraunhofer FDK AAC library (FDK license). When distributing binaries, follow the LGPL for LAME (for example, ship this project's source) and keep the FDK license text with the binary; the release zips include it in `licenses/fdk-aac.txt`. The FDK license grants no patent rights for AAC.
