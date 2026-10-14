# Zeus + Poseidon on Apple Silicon

[![Latest release](https://img.shields.io/github/v/release/nic0lasm/zeus-poseidon-arm64?label=download&color=1f6feb)](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest)
[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple%20Silicon-333?logo=apple)](docs/getting-started.md)
[![build](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml/badge.svg)](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml)
[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-green)](LICENSE)

**Play *Zeus: Master of Olympus* and its *Poseidon* expansion on an Apple Silicon Mac — the
original game, with no Windows, no Wine and no virtual machine.**

*[Version française](README.fr.md)*

<p align="center">
  <img src="docs/images/city-wide-fr.jpg" width="100%" alt="A large city in wide mode (1324 × 768), with a French language pack">
</p>
<p align="center">
  <img src="docs/images/menu-fr.png" width="49%" alt="The main menu with a French language pack">
  <img src="docs/images/tutorials.png" width="49%" alt="The tutorials screen">
</p>
<p align="center"><sub>A large city in wide mode, and the main menu in French — both built from your own copy of the game.</sub></p>

*Zeus* (Impressions Games, 2000) is a 32-bit Windows game, and recent Macs can no longer run it.
This project runs the game's **original, unmodified code** inside a small sandboxed x86
emulator, and rewrites natively the Windows functions it relies on: files, window, mouse,
keyboard, DirectDraw, sound and video. The result is the real game — same simulation, same
rules, same save files — not a remake.

> **This repository contains no game files.** You need your own copy of the game (GOG edition).
> See [Legal](#legal).

## Play

**[⬇ Download Zeus-Poseidon-macos-arm64.zip](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/latest/download/Zeus-Poseidon-macos-arm64.zip)** — for a Mac with an **Apple Silicon** chip
(M1 or later; it does not run on an Intel Mac). You also need your own copy of the game: the
GOG installer, see step 3.

1. Open the downloaded zip (double-click it), then double-click **Zeus Poseidon**.
2. macOS refuses to open it the first time, because the application is not notarised by Apple.
   Click **Done** (or *OK*), then open **Apple menu → System Settings → Privacy & Security**,
   scroll down to the message about “Zeus Poseidon” and click **Open Anyway**; confirm with your
   password or Touch ID, then click **Open**. This is needed once only. *(Written for macOS 15
   from Apple's documentation; the exact wording may differ slightly on your version, and has
   still to be checked by a human on a clean Mac.)*
3. Pick your GOG installer (`setup_zeus_and_poseidon_….exe`) — the file picker opens in Downloads — or drop it on the application icon. It is unpacked once (about a
   minute, 800 MB); then play. To get it from a Mac: sign in at **GOG.com → your library →
   Zeus + Poseidon → Download offline backup game installers → Windows** (download the `.exe`
   and the `.bin` files next to it, if any).

**In French?** Hold the Option key (⌥) at launch and choose *Another language, from the original
CDs…*: the application builds a French pack from the images of your own CDs
([details](docs/languages.md)).

## What you get

- **The whole game** — the sixteen tutorials, the Zeus and Poseidon adventures, the adventure
  editor, gods, heroes, monsters and disasters, save and load.
- **Sound and video** — music, ambience, 3D sound, effects and the intro video.
- **A modern window** — window or full screen (⌘F), sharp at any size, and a wide mode fitted to
  your screen: pick the largest resolution in the game's display options.
- **Your language** — English out of the box, French (and other editions, see below) built from
  your own CDs.
- **Small comforts** — mouse wheel in lists, pause when you switch to another application, start
  in under a second.
- **Faithful by proof** — eight replayable scenarios compare every frame with a reference, so a
  change that alters a single pixel of the game cannot slip in.

## Where we are going

The aim: keep *Zeus* and *Poseidon* playable for as long as people want to play them — the
original game, on the machines people own, proven identical to the original.

### Now — 1.1 *Cool and quiet*
As little CPU as the game needs, so that a laptop stays cool and silent.
[Milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/1)

- [x] Native sprite dispatcher: 42 % less computing time in a large city ([#21](https://github.com/nic0lasm/zeus-poseidon-arm64/pull/21))
- [x] The game at its real speed: 45 simulation ticks a second instead of 30 ([#32](https://github.com/nic0lasm/zeus-poseidon-arm64/pull/32))
- [ ] Native city drawing loop ([#34](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/34))
- [ ] Native control panel texts ([#35](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/35))
- [ ] Can unseen redraws be skipped? — research ([#36](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/36))
- [x] Taller wide modes, without a black band under the panel ([#23](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/23))

**Target:** a small city under 20 % of one core (about 42 % today), the large city of the last
tutorial under 35 %.

### Next — 1.2 *Every edition*
German, Spanish, Italian, Polish, Dutch, Swedish, Czech… packs built from your own CDs.
**Testers wanted**: if you own another edition, your report is the whole step
([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)).

### Then — 2.0 *Everywhere*
The same host on **Linux** (x86-64 and arm64) and **Intel Macs**. Only four small pieces are
macOS-specific today. [Milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/3)

- [ ] Portable MP3 decoding ([#37](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/37)) · JPEG decoding ([#38](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/38)) · first-launch dialogs ([#39](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/39))
- [ ] Linux build in the automated build, then an AppImage ([#40](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/40))
- [ ] One universal Mac application, Intel and Apple Silicon ([#41](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/41))

### Later — 3.0 *Recompiled*
**No emulator at all.** Your own `Zeus.exe` translated to native code *on your machine* — like a
language pack, nothing from the game is ever distributed — with the emulator kept as the
reference that proves every translated function. Faster, lighter, and the way to an iPad
edition. [Milestone](https://github.com/nic0lasm/zeus-poseidon-arm64/milestone/4)

- [ ] Equivalence harness: a native function checked against the original on every call ([#26](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/26))
- [ ] First translated function, x86-32 to C ([#42](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/42))
- [ ] The recompiler, then the whole game without an emulator ([#27](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/27))

### Exploring — the City Building Series
*Emperor: Rise of the Middle Kingdom* was built on the engine of *Zeus*. Could the same host bring
it back — and *Pharaoh*, *Caesar III*? Measurements first
([#28](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/28),
[#29](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/29)).

The full plan, what each step must achieve and a look back on 1.0: [docs/roadmap.md](docs/roadmap.md).

## Version history

| Version | Highlights |
|---|---|
| [**1.0.1**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0.1) | the game at its real speed (it ran at two thirds of it since 0.2); native sprite dispatcher |
| [**1.0**](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v1.0) | first stable version: the code of 0.9, played through by hand |
| [0.9](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.9) | release candidate: starts in under a second, real resolution shown in wide mode, lighter on memory |
| [0.4](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.4) | 3D sound, native drawing routines, mouse wheel, pause in the background, much wider automated tests |
| [0.3](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.3) | wide screen; language packs built by the application from the original CDs |
| [0.2](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.2) | French; half the CPU; the application opens the GOG installer itself |
| [0.1](https://github.com/nic0lasm/zeus-poseidon-arm64/releases/tag/v0.1) | first preview: menus, tutorials, building, save and load, music, sound, intro video, full screen |

The archives of 0.1 and 0.2 were withdrawn (they carried build paths of the machine that made
them); every change is described in [CHANGELOG.md](CHANGELOG.md).

## Build from source

Requirements: an Apple Silicon Mac, the Xcode command line tools, [Homebrew](https://brew.sh),
and the GOG edition of *Zeus + Poseidon*.

```bash
brew install cmake ninja pkgconf sdl2 unicorn ffmpeg innoextract
git clone https://github.com/nic0lasm/zeus-poseidon-arm64.git
cd zeus-poseidon-arm64

# Unpack your own GOG installer (the game is never installed on the Mac, only unpacked)
innoextract -d original/gog /path/to/setup_zeus_and_poseidon_2.1.0.10.exe

cmake -S . -B build -G Ninja && cmake --build build
./build/zeus-run --run original/gog/app/Zeus.exe --wide auto
```

`python3 tools/make_app.py` builds a double-clickable `Zeus Poseidon.app`. The game folder is
never modified: everything the game writes (saves, settings) goes to `run/zeus/`. Options,
scripted runs and troubleshooting: [docs/getting-started.md](docs/getting-started.md).

## How it works

```
        Zeus.exe (original x86 code, unmodified)
                      │ runs inside
        ┌─────────────▼──────────────┐
        │  sandboxed x86 emulator    │   Unicorn engine, 32-bit address space
        └─────────────┬──────────────┘
                      │ every Windows call lands on
        ┌─────────────▼──────────────┐
        │  native host (C++17)       │   KERNEL32 · USER32 · GDI32 · DirectDraw
        │  src/win32/                │   Miles Sound · Bink · Intel JPEG
        └─────────────┬──────────────┘   + hot game routines rewritten natively
                      │
    SDL2 · FFmpeg (Bink, MP3) · macOS ImageIO
```

The game imports 202 Windows functions. Each one is a small native function in `src/win32/`; a
function that is not written yet stops the program and names itself, which is what drove the
work. The game's hottest routines — sprite drawing, its dispatcher, a start-up loop — also run
natively, each proven identical frame by frame. The game sees a private `C:\Zeus` folder: reads
fall through to your game files, writes go to a separate directory.

More: [docs/architecture.md](docs/architecture.md) and the findings in
[docs/reverse-engineering.md](docs/reverse-engineering.md).

## Also in this repository

The project started as a study of the game's data and code, and that work is documented here
for anyone building tools for the *Zeus* / *Poseidon* community:

- **File formats** — [`formats/sg3.md`](formats/sg3.md) (sprites, `.sg3` + `.555`),
  [`formats/map.md`](formats/map.md) (maps and saved games, `.map` / `.sav` / `.pak`) and
  [`formats/eng.md`](formats/eng.md) (texts and in-game help, `.eng`), each
  with a reference reader in Python; more to come ([#30](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/30)).
- **Extraction tools** — pure-Python readers for the original CD images, InstallShield
  cabinets and Wise patch installers ([`tools/`](tools/)).
- **Viewers** — `zeus-sprites` (browse every sprite of the game) and `zeus-map` (isometric
  terrain of any map).
- **Reverse-engineering notes** — imports, symbols, engine behaviour
  ([`analysis/notes/`](analysis/notes/), [docs/reverse-engineering.md](docs/reverse-engineering.md)).

## Community

This is a project by and for the players of the game. Say how it runs on your Mac, ask questions
or share ideas in the [Discussions](https://github.com/nic0lasm/zeus-poseidon-arm64/discussions);
bugs go to the [issue tracker](https://github.com/nic0lasm/zeus-poseidon-arm64/issues).

The most useful help right now:

- **Play and report** — an adventure played through, on your Mac model.
- **Another language edition?** Try a pack from your CDs and tell us ([#24](https://github.com/nic0lasm/zeus-poseidon-arm64/issues/24)).
- **Code** — issues labelled [good first issue](https://github.com/nic0lasm/zeus-poseidon-arm64/labels/good%20first%20issue)
  and [help wanted](https://github.com/nic0lasm/zeus-poseidon-arm64/labels/help%20wanted); see
  [CONTRIBUTING.md](CONTRIBUTING.md).

## Legal

This is an independent, non-commercial fan project. It is not affiliated with, endorsed by or
supported by Impressions Games, Sierra, Activision or GOG. *Zeus: Master of Olympus* and
*Poseidon: Master of Atlantis* are trademarks of their respective owners.

The repository contains only original source code, tools and documentation. It ships no
executable, artwork, sound, text or data from the game, and it does not circumvent any copy
protection: it only runs the DRM-free GOG edition, from files you provide.

## License

GPL-2.0-or-later — see [LICENSE](LICENSE). This covers the code and documentation of this
repository only, never the game.

The x86 emulation relies on the [Unicorn engine](https://www.unicorn-engine.org/) (GPL-2.0);
videos are decoded with [FFmpeg](https://ffmpeg.org/). See [docs/third-party.md](docs/third-party.md).
