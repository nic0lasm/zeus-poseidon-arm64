# Third-party software

The program is distributed under the GNU General Public License, version 2 or later
(see `LICENSE`). Its source code is at https://github.com/nic0lasm/zeus-poseidon-arm64.

It contains no file from the game.

| Component | Use | Licence | Source |
|---|---|---|---|
| Unicorn Engine | x86 emulation | GPL-2.0 | https://github.com/unicorn-engine/unicorn |
| SDL 3 and sdl2-compat | window, input, audio output | zlib | https://github.com/libsdl-org |
| innoextract | unpacking the GOG installer (separate helper program) | zlib | https://constexpr.org/innoextract/ |
| Boost, liblzma, zstd | used by innoextract | BSL-1.0, 0BSD, BSD-3-Clause | https://www.boost.org, https://tukaani.org/xz/, https://github.com/facebook/zstd |
| CPython (python-build-standalone) | runs the project's pack-building tools inside the application | PSF-2.0 | https://github.com/astral-sh/python-build-standalone |
| FFmpeg | Bink video and audio, MP3 decoding | LGPL-2.1-or-later (reduced build: Bink and MP3 demuxers, Bink and MP3 decoders, libswscale, libswresample) | https://ffmpeg.org |

A redistributable application bundle links FFmpeg statically, from the reduced build produced by
`tools/build_ffmpeg.py`, which records the exact source archive and configuration. A bundle
built against a system FFmpeg may fall under different terms and is for personal use only.

macOS frameworks (ImageIO, CoreGraphics) are used through the system and are not included.
