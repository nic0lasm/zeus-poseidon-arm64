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

Later, hot spots of the original code (rendering first) can be replaced one by one with native
code, with the original always available as a reference.

## Layout

```
src/win32/     the host: emulated CPU, process, and the Windows functions
src/formats/   readers for the game's data files (sprites, maps)
src/game/      native rendering experiments (sprite store, isometric terrain)
src/platform/  small SDL helpers
src/tools/     the three executables
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
| `kernel32.cpp` | start-up, memory, strings, time, files |
| `user32.cpp` | window, messages, mouse, keyboard (also GDI32, WINMM, OLE32, SHELL32) |
| `desktop.*` | the SDL window, message queue, 16-bit frame presentation, scripted input |
| `ddraw.cpp` | DirectDraw |
| `mss.cpp` | Miles Sound System (sound effects, voices, music) |
| `audio.*` | the host mixer and audio decoders |
| `bink.cpp` | Bink video |
| `media.cpp` | Intel JPEG library |

### Calling from the game into the host

Every imported function gets a 4-byte slot in a reserved, otherwise empty memory zone
(`0xF0000000`). The import table of the executable is filled with those addresses. When the
game calls one, the emulator stops, the host runs the matching C++ function, pops the arguments
as `stdcall` would, and resumes the game at the return address.

A function that has not been written stops the program and names itself. Nothing is silently
faked, with one deliberate exception: the few sound and video functions that are not written
yet (3D sound, mostly) do nothing and return zero.

### Calling from the host into the game

Windows calls back into programs — most importantly the window procedure. A host function can
request a guest call; the emulator then runs the game's function and hands the result to a
continuation. Calls can be chained, which is how a sequence of window messages is delivered
before a system call returns.

### Process environment

Heap at `0x10000000` (256 MB), stack at `0x0FE00000` (2 MB), a thread environment block
reachable through the `FS` segment (the Visual C++ 6 runtime uses it for exception handling).
Unicorn cannot set the `FS` base directly in 32-bit mode, so the host installs a small GDT.

Only the code sections of the executable are mapped executable, and read-only: the game never
writes to its own code. This matters for speed, because the emulator sends every write to an
executable region through a slow path that watches for self-modifying code.

### Files

The game believes it lives in `C:\Zeus`. Paths are resolved case-insensitively. Reads look in
the write directory first, then in the game folder; writes only ever go to the write directory.
The game folder is strictly read-only.

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
MP3 files are decoded in a background thread by macOS AudioToolbox, so no extra dependency is
needed. Volume, pan, loop counts, status polling and stream positions follow Miles semantics.
No 3D sound provider is offered, so the game falls back to plain stereo.

### Video

The intro is a Bink file. The game asks the Bink library for one frame at a time, has it copied
into the locked primary surface, and polls a wait function between frames. The host decodes the
video with FFmpeg straight to 16-bit 5-5-5 pixels, paces it on the file's frame rate, and plays
the audio track through the same mixer as the rest of the sound.

### Window messages

The host translates SDL events into Windows messages (mouse, keys, characters) and feeds the
game's own message loop. One lesson learned: the game sets the screen rectangle it copies every
frame only when its window procedure receives `WM_MOVE`. Without that message, the game runs
perfectly and shows a black screen. See [reverse-engineering.md](reverse-engineering.md).

## Safety

The game's code runs only inside the emulator. It has no access to the host's memory or file
system other than through the functions above, and those confine it to the two directories
given on the command line.
