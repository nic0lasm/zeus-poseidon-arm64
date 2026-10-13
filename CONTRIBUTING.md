# Contributing

Thank you for your interest. The project is young; the most valuable help today is **playing
and reporting**.

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

## Before opening a pull request

```bash
cmake --build build
./build/zeus-run --selftest
./build/zeus-run --run original/gog/app/Zeus.exe --headless --frames 40 \
    --screenshot menu.bmp --write-dir build/trial
```

The capture should show the main menu. Describe in the pull request what you verified and what
remains a hypothesis.
