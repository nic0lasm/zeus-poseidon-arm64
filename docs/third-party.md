# Third-party software

The program is distributed under the GNU General Public License, version 2 or later
(see `LICENSE`). Its source code is at https://github.com/nic0lasm/zeus-poseidon-arm64.

It contains no file from the game.

| Component | Use | Licence | Source |
|---|---|---|---|
| Unicorn Engine | x86 emulation | GPL-2.0 | https://github.com/unicorn-engine/unicorn |
| SDL 3 and sdl2-compat | window, input, audio output | zlib | https://github.com/libsdl-org |
| FFmpeg | Bink video and audio decoding | LGPL-2.1-or-later (reduced build: Bink demuxer and decoders, libswscale, libswresample) | https://ffmpeg.org |

A redistributable application bundle links FFmpeg statically, from the reduced build produced by
`tools/build_ffmpeg.py`, which records the exact source archive and configuration. A bundle
built against a system FFmpeg may fall under different terms and is for personal use only.

macOS frameworks (ImageIO, CoreGraphics, AudioToolbox) are used through the system and are not
included.
