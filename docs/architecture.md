# Architecture

## The idea

Rewriting a city-builder from its machine code is years of work, and any rewrite drifts from
the original in a thousand small ways. Running the game under a full Windows layer works, but
depends on Intel-to-ARM translation that Apple is retiring.

This project takes a third road: **keep the game's own code, replace everything around it.**
`Zeus.exe` is loaded into an emulated 32-bit x86 processor. The game is a plain Win32 program
from 2002 that talks to the outside world through 202 imported functions. Each of those is
implemented natively. The game cannot tell the difference, and because the simulation is the
original code, fidelity comes for free.

Hot spots of the original code can then be replaced one by one with native code, with the
original always available as a reference: today the pixel-drawing routines and one start-up
loop run natively (see below), the rest of the game is emulated.

## Layout

```
src/win32/     the host: emulated CPU, process, and the Windows functions
src/formats/   readers for the game's data files (sprites, maps)
src/game/      native rendering experiments (sprite store, isometric terrain)
src/platform/  small SDL helpers
src/tools/     the executables; zeus_run/ holds zeus-run, split by role (below)
src/tests/     zeus-tests: the host parts that do not need the game (see tests/README.md)
tools/         Python tools: extraction, format readers, executable triage, Ghidra scripts
formats/       file-format specifications
analysis/notes reverse-engineering notes kept under version control
```

## The host (`src/win32/`)

| File | Role |
|---|---|
| `cpu.*` | x86-32 emulation (Unicorn), guest memory, the bridge between guest and host calls |
| `pe_image.*` | loads the PE executable and lists its imports |
| `process.*` | stack, heap, thread block, binding of every import to a host function |
| `vfs.*` | the file system the game sees |
| `sprite_clip.h` | the game's sprite clipping (FUN_00509c20, FUN_00509cd0), shared by the native dispatcher and the tests |
| `game_gog.h` | every address of the GOG executable the host uses, named, and the check of expected bytes |
| `native.cpp` | game functions rewritten for the host |
| `widescreen.cpp` | wide display mode, in place of the game's 1024×768 |
| `language.cpp` | in-memory text adjustments for language packs |
| `text.*` | conversion between the game's Windows-1252 text and UTF-8 |
| `kernel32.cpp` | start-up, environment, time; `kernel32_memory.cpp`: heap, virtual memory, character sets; `kernel32_files.cpp`: files and folders (state shared through `kernel32_state.h`) |
| `user32.cpp` | window, messages, mouse, keyboard (also GDI32, WINMM, OLE32, SHELL32) |
| `desktop.*` | the SDL window, message queue, 16-bit frame presentation, scripted input |
| `ddraw.cpp` | DirectDraw |
| `mss.cpp` | Miles Sound System (sound effects, voices, music) |
| `audio.*` | the host mixer and audio decoders |
| `bink.cpp` | Bink video |
| `media.cpp` | Intel JPEG library |

### The launcher (`src/tools/zeus_run/`)

| File | Role |
|---|---|
| `main.cpp` | command line and the list of options |
| `launch.cpp` | runs the game in the emulated processor, end-of-run reports |
| `app.cpp` | application mode: asks for the game and the language on first launch |
| `dialogs.cpp` | system commands and AppleScript dialogs, French or English |
| `packs.cpp` | builds a language pack from the original CDs |
| `diagnostics.cpp` | `--selftest`, `--bench`, `--inspect`, `--check-audio` |

### Calling from the game into the host

Every imported function gets a 4-byte slot in a reserved memory zone (`0xF0000000`). The import
table of the executable is filled with those addresses. Each slot holds a `ret n` instruction.
When the game calls one, a hook runs the matching C++ function and sets the result; the game
then executes the `ret n` itself and carries on. The emulator is not stopped, which keeps a
system call around 100 ns.

A function that has not been written stops the program and names itself. Nothing is silently
faked, with one deliberate exception: the few Miles and Bink functions the game imports but
never needs do nothing and return zero.

### Native game functions

`native.cpp` replaces a few functions of the game itself. The first byte of the original
function becomes a `ret`, and a hook runs the host version just before it, so entering it
costs about 75 ns. A loop inside a function can be replaced the same way: its first
instruction becomes a jump to its exit, and the hook leaves registers and stack as the loop
would have. A function can also get a native fast path while keeping its original code: its
first instructions move to a trampoline, which runs the host version and either returns to the
caller or carries on into the original (`Cpu::add_fast_path`, used by the sprite dispatcher).
Each replacement checks the original bytes first (`gog::matches`, `game_gog.h`) and is proven by
the frame logs of the scenarios, which must be identical with and without `--no-native`.
Every address of the game the host touches is listed in `game_gog.h`, with the original
function that uses it.

### Calling from the host into the game

Windows calls back into programs — most importantly the window procedure. A host function can
request a guest call; the emulator is then stopped and restarted on the game's function, and
its result goes to a continuation. Calls can be chained, which is how a sequence of window messages is delivered
before a system call returns.

### Process environment

The whole 32-bit address space of the game is one 4 GB block reserved by the host (pages are
only committed when touched). The emulator and the host share it, so a host function reaches
guest memory by plain pointer arithmetic.

Heap at `0x10000000` (256 MB), stack at `0x0FE00000` (2 MB), a thread environment block
reachable through the `FS` segment (the Visual C++ 6 runtime uses it for exception handling).
Unicorn cannot set the `FS` base directly in 32-bit mode, so the host installs a small GDT.

Only the code sections of the executable are mapped executable, and read-only: the game never
writes to its own code.

The emulated processor runs with paging enabled: an identity page table that marks everything
except code as non-executable. This is purely for speed. Without paging, the emulator treats
every page as executable and sends each memory write through a slow path that watches for
self-modifying code; a write then costs about 30 ns against under 1 ns for other instructions.
With non-executable data pages, writes take the fast path. `zeus-run --bench` measures this.

The game's main loop polls its message queue without ever waiting and redraws on every turn.
When the queue is empty the host paces the loop at 60 turns per second, which frees the
processor; the game keeps time from the clock, not from its loop count.

### Files

The game believes it lives in `C:\Zeus`. Paths are resolved case-insensitively. Reads look in
the write directory first, then in the game folder; writes only ever go to the write directory.
The game folder is strictly read-only, which lets the host keep the small files it has read
in memory (4 MB or less): the game reopens the same ones constantly. The same path can hand the
game a patched copy of a file — the wide mode uses it to show the real resolution in a menu —
without touching the file itself. An optional language pack sits between the two: its files
replace those of the game folder (see [languages.md](languages.md)). File names are converted
between the game's Windows-1252 and the host's UTF-8.

### Display

The game uses DirectDraw 16-bit surfaces (5-5-5). DirectDraw objects are COM objects: the host
builds, in guest memory, objects whose method tables point at host functions. Surfaces live in
guest memory, since the game locks them once and draws straight into them. Whenever the game
copies to the primary surface, the frame is uploaded to an SDL texture. The picture is scaled
to the window with its proportions kept: at integer factors each game pixel is a crisp square;
otherwise it is first enlarged to the next integer factor without smoothing, then reduced with
smoothing, which keeps pixels sharp and even.

The intro screens are JPEG files decoded through Intel's JPEG library in the original; the host
decodes them with macOS ImageIO.

### Sound

The game drives the Miles Sound System: *samples* (whole WAV files handed over in memory, for
effects and ambience) and *streams* (files opened by name, for MP3 music). The host maps both
onto voices of a small software mixer feeding SDL's audio output. WAV data is read directly;
MP3 files are decoded in a background thread by FFmpeg, which is already linked for the videos. Volume, pan, loop counts, status polling and stream positions follow Miles semantics.
The host offers one software 3D sound provider, which the game uses for ambience placed around
the listener: the position becomes a volume (linear fall-off between Miles's default distances,
1 and 200) and a pan. Reverb and filters need no code: this build of the game never turns them
on (see [reverse-engineering.md](reverse-engineering.md)).

### Video

The intro is a Bink file. The game asks the Bink library for one frame at a time, has it copied
into the locked primary surface, and polls a wait function between frames. The host decodes the
video with FFmpeg straight to 16-bit 5-5-5 pixels, paces it on the file's frame rate, and plays
the audio track through the same mixer as the rest of the sound.

### Window messages

The host translates SDL events into Windows messages (mouse, keys, characters) and feeds the
game's own message loop. One lesson learned: the game sets the screen rectangle it copies every
frame only when its window procedure receives `WM_MOVE`. Without that message, the game runs
perfectly and shows a black screen. The wheel becomes `WM_MOUSEWHEEL`, and when the window
stays in the background for more than 0.4 s the game receives the activation messages Windows
would send, which make it pause and silence its sounds. See
[reverse-engineering.md](reverse-engineering.md).

## Safety

The game's code runs only inside the emulator. It has no access to the host's memory or file
system other than through the functions above, and those confine it to the two directories
given on the command line.
