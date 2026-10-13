# Getting started

## Requirements

- A Mac with Apple Silicon (developed on an M1 MacBook Air; any M-series Mac should do).
- Xcode command line tools: `xcode-select --install`.
- [Homebrew](https://brew.sh).
- Your own copy of the **GOG edition** of *Zeus + Poseidon* (installer
  `setup_zeus_and_poseidon_2.1.0.10.exe`).

Only the GOG edition is supported: it is DRM-free. The original CD executables are wrapped by
SecuROM copy protection; this project does not run them and does not circumvent the protection.

## Install the build dependencies

```bash
brew install cmake ninja pkgconf sdl2 unicorn innoextract
```

| Package | Used for |
|---|---|
| `cmake`, `ninja`, `pkgconf` | build |
| `sdl2` | window, input, display |
| `unicorn` | x86 emulation |
| `innoextract` | unpacking the GOG installer |

## Unpack the game

From the repository root:

```bash
innoextract -d original/gog /path/to/setup_zeus_and_poseidon_2.1.0.10.exe
```

The game ends up in `original/gog/app/`. Nothing under `original/` is tracked by git, and the
host never writes there.

The executable this project was developed against:

```
original/gog/app/Zeus.exe   2,527,232 bytes
SHA-256  382bcd286192a06eb8fdc45f5c37390a19999f5db9c940c3ce2dfcd775e90a57
```

Check yours with `shasum -a 256 original/gog/app/Zeus.exe`. Another build of the game may work
but has not been tried.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/zeus-run --selftest      # checks the emulator and the host-call mechanism
```

## Play

```bash
./build/zeus-run --run original/gog/app/Zeus.exe
```

The game opens in an 800×600 window. Saves and settings are written to `run/zeus/`.

At startup the terminal prints `Unable to load BINK!`: that is the intro video, not supported
yet. The game carries on.

## Options

| Option | Effect |
|---|---|
| `--write-dir DIR` | where the game's writes go (default `run/zeus`) |
| `--trace` | print every Windows call |
| `--counts` | print, on exit, how many times each Windows function was called |
| `--headless` | hidden window |
| `--frames N`, `--seconds S` | stop after N displayed frames or S seconds |
| `--screenshot FILE.bmp` | save the last displayed frame |
| `--script "T:ACTION; …"` | replay input at T seconds: `click X,Y`, `rclick X,Y`, `move X,Y`, `key CODE` (Windows virtual key), `char CODE`, `shot FILE.bmp` |

Example — reach the main menu without a window and capture it:

```bash
./build/zeus-run --run original/gog/app/Zeus.exe --headless --frames 40 \
    --screenshot menu.bmp --write-dir build/trial
```

## If the game stops

A Windows function that the host does not provide yet stops the program with a message such as:

```
arrêt : kernel32.dll!CompareStringW : fonction système non fournie
```

Please open an issue with that line and what you were doing. Messages are currently in French;
"fonction système non fournie" means "system function not provided".

## The other tools

```bash
./build/zeus-sprites --data original/gog/app     # sprite browser
./build/zeus-map --data original/gog/app --map Sandbox
```

`zeus-sprites`: ←/→ page, ↑/↓ file, +/- zoom, Esc. `zeus-map`: arrows or drag to scroll, wheel
or +/- to zoom, N/P to switch map. Both accept `--screenshot FILE.bmp`.
