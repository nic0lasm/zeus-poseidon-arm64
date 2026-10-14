# Zeus + Poseidon on Apple Silicon

[![build](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml/badge.svg)](https://github.com/nic0lasm/zeus-poseidon-arm64/actions/workflows/build.yml)

**Play *Zeus: Master of Olympus* and its *Poseidon* expansion on an Apple Silicon Mac: the
original game, with no Windows, no Wine and no virtual machine.**

*[Version française](README.fr.md)*

<p align="center">
  <img src="docs/images/city.png" width="49%" alt="A city running in the first tutorial">
  <img src="docs/images/tutorials.png" width="49%" alt="The tutorials screen">
</p>

*Zeus* (Impressions Games, 2000) is a 32-bit Windows game. Recent Macs can no longer run it:
they have an ARM processor, and macOS dropped 32-bit programs years ago. This project makes the
game run again by executing its original, unmodified code inside a small sandboxed x86 emulator,
and by re-implementing natively the Windows functions the game relies on (files, window, mouse,
keyboard, DirectDraw, sound and video).

The result is the real game — same simulation, same rules, same save files — not a remake.

> **This repository contains no game files.** You need your own copy of the game.
> See [Legal](#legal).

## Status

| Milestone | What you get | State |
|---|---|---|
| 1. The game starts | the real main menu in a Mac window | done |
| 2. Playable | mouse, keyboard, tutorials and adventures, building, saving and loading | done |
| 3. Complete | music, sound effects, videos, full screen | done |
| 4. Ready to share | performance, a self-contained `.app`, automated builds, a first release | done |
| 5. French edition | French texts and voices taken from your own original CDs | done |
| 6. No emulator | the game's hot code in native code, then the rest | **partly**: drawing and start-up |

What works today: the whole game as far as automated and hands-on testing can tell — menus,
leader profiles, the sixteen tutorials, Zeus and Poseidon adventures, the adventure editor, a
running city with its walkers, gods, monsters and disasters, save and load, music, ambience,
3D sound, sound effects and the intro video, window or full screen, the mouse wheel, and a
pause when the game is in the background. Save files are the game's own format and stay
compatible with the original. The game starts in under a second.

The city view can fill a wide screen: pick the largest resolution in the game's display options
(it reads, for instance, *1324 x 768*).

The game runs in English out of the box; French is
possible with a language pack built from your own French copy ([docs/languages.md](docs/languages.md)).

Details: [docs/roadmap.md](docs/roadmap.md).

## Download

A ready-made application is on the
[releases page](https://github.com/nic0lasm/zeus-poseidon-arm64/releases). It contains no game
file: on first launch it asks for your GOG installer of *Zeus + Poseidon* and unpacks it. The
application is not signed by Apple, so the first time you must right-click it and choose
*Open*.

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
./build/zeus-run --run original/gog/app/Zeus.exe
```

Press **⌘F** (or **⌘↩**) to switch between window and full screen. `python3 tools/make_app.py` builds a double-clickable `Zeus Poseidon.app` that asks for your GOG
installer on first launch and unpacks it. The game folder is never modified: everything the game writes (saves, settings) goes to
`run/zeus/`. Full instructions and troubleshooting: [docs/getting-started.md](docs/getting-started.md).

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
        └─────────────┬──────────────┘
                      │
    SDL2 · ffmpeg · macOS ImageIO and AudioToolbox
```

The game imports 202 Windows functions. Each one is a small native function in `src/win32/`;
a function that is not written yet stops the program and prints its name, which is what drives
the work. The game sees a private `C:\Zeus` folder: reads fall through to your game files,
writes go to a separate directory.

More: [docs/architecture.md](docs/architecture.md).

## Also in this repository

The project started as a study of the game's data and code, and that work is documented here
for anyone building tools for the *Zeus* / *Poseidon* community:

- **File formats** — [`formats/sg3.md`](formats/sg3.md) (sprites, `.sg3` + `.555`) and
  [`formats/map.md`](formats/map.md) (maps and saved games, `.map` / `.sav` / `.pak`), each
  with a reference reader in Python.
- **Extraction tools** — pure-Python readers for the original CD images, InstallShield
  cabinets and Wise patch installers ([`tools/`](tools/)).
- **Viewers** — `zeus-sprites` (browse every sprite of the game) and `zeus-map` (isometric
  terrain of any map).
- **Reverse-engineering notes** — imports, symbols, engine behaviour
  ([`analysis/notes/`](analysis/notes/), [docs/reverse-engineering.md](docs/reverse-engineering.md)).

## Community

This is a project by and for players of the game. Come and say how it runs on your Mac, ask
questions or share ideas in the
[Discussions](https://github.com/nic0lasm/zeus-poseidon-arm64/discussions). Bugs go to the
[issue tracker](https://github.com/nic0lasm/zeus-poseidon-arm64/issues).

Reports from real play sessions are the most useful contribution right now. Code, format
research and translations are welcome too: see [CONTRIBUTING.md](CONTRIBUTING.md).

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
