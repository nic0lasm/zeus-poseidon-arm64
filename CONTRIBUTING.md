# Contributing

The most valuable help today is **playing and reporting**. Questions and ideas are welcome in
the [Discussions](https://github.com/nic0lasm/zeus-poseidon-arm64/discussions).

## Reporting a problem

When the game needs a Windows function the host does not provide yet, it stops and the last
line in the terminal names it. Open an issue with:

- that last line, or the `dernier-arret.txt` report saved in the write directory;
- what you were doing in the game;
- your Mac model and macOS version;
- the SHA-256 of your `Zeus.exe` (`shasum -a 256 original/gog/app/Zeus.exe`).

For visual glitches, a screenshot helps. A `--script` sequence that reproduces the problem is
ideal (see [docs/getting-started.md](docs/getting-started.md)).

## Ground rules

1. **No game files, ever.** No executable, image, sound, text or data from the game in commits,
   issues or pull requests — and no saved games from commercial adventures. Screenshots in
   issues are fine.
2. **No copy-protection circumvention.** Only the DRM-free GOG edition is executed.
3. **The game's code runs only inside the emulator** of `src/win32/`.
4. **The game folder is read-only.** Anything the game writes goes to the write directory.
5. **Evidence over guesswork.** A statement about a file format or about the game's behaviour
   cites an offset, an address or a dump, and says whether it is *verified* or a *hypothesis*.
   When emulating Windows, prefer documented Windows behaviour and say so when unsure.

## Code

- C++17, CMake, SDL2; target arm64 macOS. Builds must stay free of warnings
  (`-Wall -Wextra -Wpedantic`).
- Python tools use the standard library only.
- Identifiers are in English. Comments, notes and messages are historically in French;
  either language is welcome in contributions.
- A host function that is not implemented must fail loudly, not return a made-up value.

## Where help is wanted

- **Play sessions**: adventures played through, with what worked and what did not.
- **Other editions**: does your copy (other language, other store) start? Send the SHA-256 of
  its `Zeus.exe` and what happens.
- **Missing Windows functions**: each one is small and self-contained (`src/win32/`).
- **3D sound, reverb and filters** in `src/win32/mss.cpp`, currently ignored.
- **File formats**: anything not yet described in `formats/`.

## Before opening a pull request

```bash
cmake --build build
./build/zeus-run --selftest
./build/zeus-run --run original/gog/app/Zeus.exe --headless --frames 40 \
    --screenshot menu.bmp --write-dir build/trial
```

The capture should show the main menu.

For a change that must not alter the game's behaviour, compare frame logs before and after:
run the same `--script` with `--deterministic --frame-log` on both builds; the two files must be
identical. Describe in the pull request what you verified and what
remains a hypothesis, and add a line to the "Unreleased" section of `CHANGELOG.md`.
