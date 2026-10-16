# Rendering

How `Zeus.exe` (GOG edition, SHA-256 `382bcd28…75e90a57`) puts pixels on the screen: surfaces,
sprite banks, the sprite dispatcher, clipping, the pixel routines, the isometric view and the
order in which the city is drawn. The goal is a description precise enough to rewrite the
subsystem by hand without reading the machine code again.

Every claim carries its evidence (an address, an offset, a measurement) and a status:

- **verified**: read in the code (decompilation checked against the listing where widths
  matter) and, where noted, confirmed by a measurement or by the native code that already
  replaces it and keeps the eight scenarios identical;
- **hypothesis**: inferred from names, data or context, not proven.

Addresses are those of the GOG executable. `FUN_xxxxxxxx` names are Ghidra's. The native code
mentioned is in `src/win32/native.cpp`, `src/win32/native_draw.cpp` and
`src/win32/sprite_clip.h`; every address the host touches is listed in `src/win32/game_gog.h`.

## 1. Measurements used in this page

Scenario `working-city` (last tutorial, a large city), run for its full 300 s with
`zeus-run --headless --deterministic`, once with `--no-native` (the original code alone) and
once with the native functions; `--count` on every function named here, `--profile` on the
native run. In this run the city view is drawn **463 times** (`0x0044A830`), the main loop
turns 8,398 times (`0x00514020`) and 3,725 frames are presented. The deterministic clock does
not run at wall-clock speed, so the counts below are given **per run** and **per city frame**;
section 12 adds the real-time rate (about 45 city frames per second). Measurement logs are not
kept in the repository; the commands are given so that they can be repeated.

## 2. Frame lifecycle

| Step | Function | What it does | Status |
|---|---|---|---|
| One turn of the main loop | `0x00513580` | asks `0x005168A0` whether a simulation tick is due, then draws | verified (`docs/reverse-engineering.md`) |
| Draw the current screen | `0x00514020` | `switch` on the screen number, byte `0x00F2A6DD`; the city screens are `0x01`, `0x20` and `0x48` | verified |
| City screen | `0x00514020` case `0x01`/`0x20`/`0x48` | when `0x00665E2C == 0`: city view `0x0051CAB0`, then the interface (`0x0058EA20`, `0x0058EA50`, `0x0058F130`, `0x004542B0`, `0x0059C8A0`, `0x0059D040`, `0x0059D280`, `0x0059E5A0`, `0x00521D30`) and `0x005C5D60` (a debug overlay drawn only when `0x0160AC9C`/`0x0160ACA0` are set) | verified (calls); roles of the interface calls: hypothesis |
| Clear the frame | `0x0050B410` | fills `width × height` pixels with 0 unless `0x0063C040` is set | verified, native |
| Present | `0x0050C1B0` | see section 3.3 | verified |

The game draws everything into one 16-bit frame buffer in system memory, then copies it to the
screen. Nothing is drawn by DirectDraw itself except that final copy.

## 3. Surfaces

### 3.1 Objects

| Address | Object | Status |
|---|---|---|
| `0x00F2AB20` | `IDirectDraw` | verified |
| `0x00F2AB48` | clipper (window mode) | verified |
| `0x00F2AB54` | primary surface | verified (`0x00434380`: created with `DDSCAPS_PRIMARYSURFACE` = `0x200`) |
| `0x00F2AB50` | back buffer, `[0x00F2A6A0] × [0x00F2A69C]` pixels | verified (`0x00434820` called from `0x00434380`) |
| `0x00F2AB4C` | 16-bit buffer used instead of the back buffer when the desktop is in 24 or 32 bits (window mode) | verified (`0x00434380`, end) |
| `0x00F0E624` | JPEG backdrop surface | verified (`docs/reverse-engineering.md`) |

Creation is in `0x00433E20` (display depth, window or full screen) and `0x00434380`
(surfaces). `0x00434250` releases everything and clears the frame buffer pointer.

### 3.2 The frame buffer

- **Pointer locked once.** `0x00434380` calls `Lock` on the back buffer (or on `0x00F2AB4C`),
  stores `lpSurface` in **`0x00F9C100`** and calls `Unlock` immediately. Every drawing routine
  then writes through that pointer, without locking again. **verified** (`0x00434380`; the host
  keeps surface memory fixed, which is why this works).
- **Geometry.** `0x0050B980` sets `0x00F139B8`/`0x00F139B0` (800×600, 1024×768 or 640×480 from
  the mode at `0x00F2A770`), then: bytes per row `0x00F2A698` = width × 2, pixels per row
  `0x00F2A6A0`, rows `0x00F2A69C`, screen width/height `0x00F2A6B4`/`0x00F2A6B0`, menu offsets
  `0x00F2A6AC` = (width − 640)/2 and `0x00F2A6A8` = (height − 480)/2. **verified**
- **Pixel format.** `0x00434380` reads the surface description (`GetSurfaceDesc`, method
  `+0x58`) and stores in `0x00F2AB24`: `0x565` if the green mask is `0x07E0`, `0x555` otherwise
  (`0x655` and `0x556` are recognised and reported as unsupported); index in `0x00F9C0E4`
  (0 = 555, 2 = 565). **verified.** The host always announces 5-5-5; the native routines refuse
  5-6-5 rather than guess (`require_555`).
- **Colour encoding.** `0RRRRRGGGGGBBBBB`; magenta `0xF81F` is the transparent key of
  uncompressed images; pure green `0x03E0` is the key of isometric bases (`formats/sg3.md`).
  **verified**

### 3.3 Presentation (`0x0050C1B0`)

- An elapsed-time accumulator `0x00F9C0F0` grows by `0x0050BFA0()` each call; the frame is
  presented when it reaches `0x32` or when the flag `0x00F2A6CC` forces it (argument 1 forces it
  too). Then the accumulator and the flag are cleared. **verified**; that the unit is the
  millisecond (20 frames per second) is a **hypothesis**.
- Full screen (`0x00F2A764 != 0`): `BltFast(primary, 0, 0, back buffer, &0x00F2AB08, 0)`.
- Window (`0x00F2A764 == 0`): if the desktop is not 16-bit (`0x00F2A760`, bits per pixel), lock
  `0x00F2AB4C` and the back buffer, convert with `0x004139B0` (24-bit, from 555),
  `0x00413A30` (24-bit, from 565), `0x004138B0` (32-bit, from 555) or `0x00413930` (32-bit, from
  565), unlock; then `Blt(primary, &window rectangle, back buffer, &(0, 0, w, h), 0, 0)`.
  **verified** (calls); the four converters were not read: **hypothesis** for their exact
  rounding.

## 4. Sprite banks

All images live in one object, **`0x012AEBE8`** (the `this` of the dispatcher), made of banks
of `0xCC44` bytes. **verified** (`0x005ACF50`, `0x00458170`, `0x00457900`).

| Offset in a bank | Content | Status |
|---|---|---|
| `+0x1000` | pointer to the image entries, `0x48` bytes each | verified |
| `+0x1004` | pointer to the groups, 8 bytes each, `+4` = first image id of the group | verified (`kTerrainGroups` = bank 5 + `0x1004`, `kFontGroups` = bank 0 + `0x1004`) |
| `+0x110C` | file names of the bitmaps, `0x104` bytes each | verified (external images, section 5.2) |
| `+0xDC34` | number of entries | verified |
| `+0xDC38` | rank of the last group | verified (`kTerrainGroupCount`, `kLastFontGroup`) |
| `0x3586D8` (from the object) | buffer of the external image currently loaded | verified |

**Image id** = bank × 16384 + number, split with signed division (`id / 16384`, `id % 16384`).
Id 0 and number ≤ 0 in bank 0 draw nothing; a number ≥ the entry count draws nothing (the ground
loop instead falls back to entry 0 there). **verified.** Banks seen: 4 (`0x10001`, building
props), 5 (terrain, `0x14000 + …`), 15 (`0x3C000 + …`, full-screen backgrounds). **verified**
(constants in `0x00458170`, `0x00457900`, `0x005ACF50`).

An entry is the `.sg3` image record (`formats/sg3.md`, 72 bytes) after loading:

| Offset | Type | Field | Status |
|---|---|---|---|
| `+0x00` | u32 | address of the pixels (the file offset becomes a pointer); ≤ 0: nothing to draw | verified |
| `+0x04` | u32 | data length | verified |
| `+0x08` | u32 | length of the uncompressed isometric base; the compressed top follows it | verified (`0x005ADF00` adds it) |
| `+0x0C` | u32 | bank whose name table holds the external file name | verified (`0x005ACF50`) |
| `+0x10` | i32 | mirror link: the image is entry `n + link`, flipped | verified |
| `+0x14`, `+0x16` | i16 | width, height | verified |
| `+0x32` | u8 | type: 0, 2, 10, 12, 13, 20, 30 (`0x1E`) | verified |
| `+0x33` | u8 | compressed (run-length) | verified |
| `+0x34` | u8 | pixels in an external `.555`, loaded at draw time | verified |
| `+0x35` | u8 | has a compressed top above its base (buildings) | verified (`0x005ADF00`, `0x00458170`) |
| `+0x38` | u8 | bitmap index (file name) | verified |
| `+0x3B` | u8 | draw as shadow (`0x7C00` pixels darken) | verified |
| `+0x3E` | u8 | mirrored | verified |
| `+0x40` | u32 | address of the alpha mask (Poseidon images), 0 if none | verified |

`0x0040FCF0(n)` returns group `n` of bank 0 and `0x0040FD10(n)` an image id from a group number
(called 3.9 million times in the run). **verified** (calls); exact arithmetic of `0x0040FD10`:
see `tools/annotate_groups.py`.

## 5. The sprite dispatcher, `0x005ACF50`

`int __thiscall FUN_005acf50(banks, id, x, y, mode, colour*, mirror)` — 4,016 bytes, 366 call
sites. Called **8,215,286** times in the run with the original code, on every screen (menus
included: most of these calls are the 5.3 million opaque copies, section 14). Returns the visible width (or the image width on some paths), 0 when
nothing was drawn, −1 for mode 1 without a colour. **verified**

### 5.1 Steps

1. Store the arguments in `0x00F139E0` (id), `0x00F139DC` (x), `0x00F139D8` (y).
2. Find the entry (section 4). If `+0x3E` is set, follow the mirror link (to entry 0 if it
   points past the count) and set the mirror flag. XOR it with the `mirror` argument.
3. Fill the drawing state (section 6): image address `0x00F13A18`, width `0x00F13A1E`, height
   `0x00F13A1C`, type `0x00F13A14`, target `0x00F13A40`/`0x00F13A3C` = (x, y).
4. **External image** (`+0x34`), section 5.2.
5. **Map backgrounds drawn in one block.** On screens `0x14`, `0x15`, `0x1A`, `0x2D`, `0x34`,
   on screen 3 with sub-mode `0x00781894` ∈ {`0x14`, `0x15`}, and on screen 9 with sub-screen
   `0x00FFA180 == 0x14`: if `id == [[0x0136F3E8] + 4] + 0x3C000`, copy the window
   `[0x00F139D4]` columns × `[0x00F139D0]` rows starting at (`[0x00F139CC]`, `[0x00F139C8]`) of
   the image with `0x00413DC0`, and return. **verified** (`0x005AD65B`–`0x005AD6D1`)
6. **Isometric images** (type `0x1E`) by width: 58 → height forced to 30, drawn as a diamond;
   26 and 10 → height 14 and 6, drawn by `0x00401000`, a one-byte `RET`: *never drawn*;
   118, 178, 238, 298, 358 (bases of 2 to 6 tiles) → `0x0043FF10`, `0x00440170`, `0x004406B0`,
   `0x00440FE0`, `0x00441E50`, which return directly. **verified**
7. Clip (section 7). If invisible (vertical state 5), return 0.
8. Choose the pixel routine (table below), then, if the overlay colour `0x0078C570` is not 0,
   apply the overlay routine over the same pixels (section 9).

### 5.2 External images (`+0x34`)

The pixels are not in memory: the file name is built from the bitmap name
(bank `[entry + 0x0C]`, `+0x110C + [entry + 0x38] × 0x104`) with an extension appended
(`0x0050A720`, string at `0x0063C078`), the previous buffer is freed (`0x005AE730`), the buffer
`0x00F9C104` takes its place, `0x0050A860` reads `length` bytes at `offset − 1`, then
`0x00509F90` (compressed) or `0x0050A000` (uncompressed) prepares them. The image is then
clipped and drawn with a reduced set of routines: run-length (`0x004161B0`, `0x004163B0`,
`0x004165D0`, mirrored `0x00415490`, `0x004158E0`, `0x00415680`) or keyed copies (`0x00415F50`,
`0x00416010`, mirrored `0x00415E30`, `0x00415EC0`); no alpha, no tint, no overlay. An image wider than a frame row is a
full-screen backdrop: centred window (`(1024 − width)/2`, `(768 − height)/2`) read by
`0x005AE770` and copied by `0x00413DC0`. **verified** (control flow); 0 calls in the run.

### 5.3 Pixel routines by case

Clipping state `cx` (`0x00F13991`): 0 whole, 1 clipped on the left, 2 on the right. `M`: mirror.

| Image | `cx = 0` | `cx = 1` | `cx = 2` | Calls in the run (original) |
|---|---|---|---|---|
| type 0, `mode = 1` (tint, colour `*colour`; null pointer: returns −1) | `0x005B86A0` | `0x005B8B60` | `0x005B8910` | 0 |
| type 0, compressed, alpha mask, not M | `0x00413E30` | `0x00414CE0` | `0x00414540` | 4,317 / 0 / 0 |
| type 0, compressed, alpha mask, M | `0x00414190` | `0x004150B0` | `0x00414910` | 0 |
| type 0, compressed, not M | `0x004161B0` | `0x004163B0` | `0x004165D0` | 2,384,169 / 5,563 / 5,703 |
| type 0, compressed, M | `0x00415490` | `0x004158E0` | `0x00415680` | 1,020 / 57 / 2 |
| type 0, uncompressed, not M | `0x00415F50` | `0x00416010` | `0x00416010` | 95,842 / 467 (both) |
| any uncompressed, M | `0x00415E30` | `0x00415EC0` | `0x00415EC0` | 0 |
| type 30, width 58 | `0x00435070` | `0x004377B0` | `0x00438A90` | 163,764 / 8,682 / 8,691 |
| type 20 (glyph mask) | `0x00416C30` | `0x00416F00` | `0x00416FA0` | 934 / 0 / 0 |
| type 13 (32 × 32) | general case | `0x00417290` | `0x00417290` | 0 |
| type 12 (24 × 24) | `0x004171E0` | `0x004171E0` | `0x004171E0` | 0 |
| type 10 (16 × 16) | general case | `0x00417140` | `0x00417140` | 0 |
| type 2 | general case | `0x00417100` | `0x00417100` | 0 |
| other types, uncompressed, not M | `0x00413DC0` (opaque) if `cy = 0`, else `0x00415F50` (keyed) | `0x00416010` | `0x00416010` | 5,319,977 opaque |

"General case" for types 2, 10, 13 means the last two rows of the table. For uncompressed images
the source pointer is advanced first by `(width × skipped rows + skipped columns) × 2` bytes.
Type 30 ignores the mirror. **verified** (`0x005ACF50`, every branch read in the decompilation).

The native fast path (`sprite::dispatch`) handles, with no overlay: compressed type 0, `mode ≠ 1`,
no alpha → `0x004161B0`, `0x004163B0`, `0x004165D0`, or mirrored `0x00415490`, `0x004158E0`,
`0x00415680`; uncompressed type 0 not mirrored → `0x00415F50` or `0x00416010`; other types
(not 12, 20, nor 2, 10, 13 clipped sideways), not mirrored → `0x00413DC0`, `0x00415F50` or
`0x00416010`, the source advanced as above; 58-pixel diamonds → `0x00435070`, `0x004377B0` or
`0x00438A90`. Measured before the last three extensions: With it, 7,938,575 of the
8,215,286 calls stay in native code from entry to exit except 276,711 that fall back; the clip
functions still run 493,617 times in emulated code (fallbacks and `0x005ADF00`). **verified**
(two runs above).

## 6. Drawing state (globals shared by all routines)

The dispatcher and the clip functions write these; the pixel routines only read them.
**verified** (`0x00509C20`, `0x00509CD0`, `0x005ACF50`, all routines in 5.3).

| Address | Width | Meaning |
|---|---|---|
| `0x00F139E0`, `0x00F139DC`, `0x00F139D8` | 32 | arguments: id, x, y |
| `0x00F13A18` | 32 | address of the pixels (advanced for uncompressed clipped images) |
| `0x00F13A1E`, `0x00F13A1C` | 16 | width, height of the image (height forced to 30 for 58-pixel diamonds) |
| `0x00F13A14` | 8 | type |
| `0x00F13A40`, `0x00F13A3C` | 32 | first pixel written (x, y) after clipping |
| `0x00F13991`, `0x00F13990` | 8 | horizontal, vertical clip state: 0 whole, 1 left, 2 right, 3 top, 4 bottom, 5 invisible |
| `0x00F139AC` | 32 | pixels hidden on the left |
| `0x00F139A8` | 32 | end of the visible part, in image columns |
| `0x00F139A4` | 32 | visible pixels per row |
| `0x00F139A0` | 32 | source bytes to skip after a row: (width − visible) × 2 |
| `0x00F1399C` | 32 | target bytes to skip after a row: (row pixels − visible) × 2 |
| `0x00F13994` | 32 | image rows up to the last visible one |
| `0x00F13998` | 32 | rows hidden at the top |
| `0x00F13A34` | 32 | rows to draw |
| `0x00F139BC`, `0x00F139B8`, `0x00F139B4`, `0x00F139B0` | 32 | clip rectangle: left, right (exclusive), top, bottom (exclusive) |
| `0x00F139D4`, `0x00F139D0`, `0x00F139CC`, `0x00F139C8` | 32 | window copied in one block (section 5.1, step 5) |
| `0x00F1397C` | 16 | colour of type 20 masks (set by the text code, `0x00508B50` family) |
| `0x0078C570` | 16 | overlay colour (section 9), 0 = none |

The clip rectangle is the full screen outside the city view; `0x0044A830` sets it to the map
view for the duration of the city drawing and restores (0, width, 0, height) afterwards.
**verified**

## 7. Clipping

`0x00509C20` (horizontal) and `0x00509CD0` (vertical), reimplemented in
`src/win32/sprite_clip.h` and tested on their own. **verified, native.**

- Horizontal: width < 1 → invisible. If `x < left`: invisible if `x + width ≤ left`, else state
  1, `skip = left − x`, `x = left`. Else if `x > right − width`: invisible if `x ≥ right`, else
  state 2, `end = right − x`. Visible = end − skip. Skips are computed even when invisible
  (with visible = 0).
- Vertical: height < 1 → invisible. If `y < top`: invisible if `y + height ≤ top`, else state
  3, `skipped = top − y`, `y = top`, rows limited to `bottom − top + skipped`. Else if
  `y > bottom − height`: state 4 with `rows = bottom − y` if `y < bottom`, else invisible.
  Rows to draw = rows − skipped.
- The dispatcher makes the vertical state 5 when the horizontal one is 5.

## 8. Pixel routines

All write 16-bit pixels at `[0x00F9C100] + y × [0x00F2A698] + x × 2` (section 6 for x, y).
**verified** for each routine named; the variants were compared by structure, not
instruction by instruction, where marked *structure*.

- **Opaque copy** `0x00413DC0(src)`: `rows × visible` pixels, skipping the two row strides.
  *verified, native.*
- **Keyed copy** `0x00415F50` (whole width), `0x00416010` (clipped): `0xF81F` in the source is
  not written. The loops of `0x00416010` run at least once even for a count ≤ 0. *verified,
  native for `0x00416010`.* `0x00415F50` has no source stride (whole rows) and copies
  max(width, 1) pixels per row when the width is not a multiple of 4, max(width, 4) when it is
  (four pixels unrolled, then one by one), at least one row. *verified (listing), native.*
  Mirrored versions `0x00415E30`/`0x00415EC0` write each row from right to left. *verified*
- **Run-length images** `0x004161B0(src, shadow)`: first skip the hidden rows (`0x004170B0`,
  counting the image width per row: `0xFF n` = n transparent pixels, `c` = c pixels follow;
  only the low byte of `EAX` is reloaded for each segment and `EAX` is cleared only at the start
  of a row, so after a run of c ≥ 128 pixels the next count read on the same row carries an
  extra 256, and so does the skip: `0x004170CD`–`0x004170E8`, *verified*, kept by the native
  `skip_rows`);
  then for each row, segments until the width is consumed. With `shadow`, a source pixel
  `0x7C00` darkens the destination to `(dst >> 1) & 0x3DEF`; other pixels are copied. Runs cross
  row ends in the data, but drawing stops at the width (the original quirks are in
  `draw_runs_from`). *verified, native.* Clipped on the left, `0x004163B0`: each row is read from
  its start, written from `target − [0x00F139A0]` and the first `[0x00F139A0] / 2` pixels are
  not written; the row stops at the image width, even in the middle of a run. Clipped on the
  right, `0x004165D0`: only the first `[0x00F139A4]` pixels of a row are written, but every run
  is read to its end and the row stops at the first run that reaches the width; a data run of
  0 pixels only advances one byte. Both write their argument slots (the last run byte, a row
  counter), which their callers never read back. *verified (listing), native.* Mirrored,
  each row written from right to left from the right edge of the image: `0x00415490` (whole
  width) stops a row like `0x004161B0`, and starts the next one from where the pointer ended,
  `+ [0x00F1399C] + 2 × width + 2`, which is the next row only if the row consumed exactly the
  width; `0x004158E0` (clipped on the left) writes the first `width − [0x00F139A0] / 2` pixels
  it meets, `0x00415680` (on the right) skips the first `width − [0x00F139A4]`; both read every
  run to its end. *verified (listing), native.*
- **Blended images** `0x00413E30(src, alpha)`: a second run stream with one byte (0–31) per
  pixel, parallel to the first (`0x00417040` skips hidden rows in both). Per 5-bit component:
  `(a × src + (31 − a) × dst) >> 5`; `a = 0` leaves the pixel. *verified, native* (555 only).
  Clipped and mirrored versions: `0x00414CE0`, `0x00414540`, `0x00414190`, `0x004150B0`,
  `0x00414910`. *structure*. `0x0041DF90` uses the same formula on whole images (`formats/sg3.md`).
- **Ground diamond** `0x00435070(src, cy)`: 15 rows widening by 4 pixels (2, 6, …, 58),
  centred, then 15 rows narrowing; stored back to back (450 words). Clipped at the top (state
  3) only the lower half is drawn; at the bottom (4) only the upper half. *verified, native
  (`draw_ground`).* Sideways clipped versions `0x004377B0`, `0x00438A90` (2.2 kB each, 210
  copies written one by one): they do not clip to the pixel but draw a fixed half, columns 30–57
  written from the target x (as if exactly 30 pixels were hidden) on the left, columns 0–27 in
  place on the right; with an overlay they call `0x00438050` / `0x00439340`. Two copies of the
  left one break the rule: on row 15 columns 38–39 receive pixels 36–37 (`+0x3CC` read twice,
  `0x00437C48`), on row 25 column 37 receives pixel 35 (the high half of `EAX` left over after
  `MOV AX`, `0x00437FFE`). *verified (listing, each copy checked by a script against the rule),
  native.*
- **Isometric bases** of n × n tiles (`0x0043FF10` for n = 2 … `0x00441E50` for n = 6): the base
  is drawn as separate 58×30 diamonds, each clipped and drawn by the three diamond routines,
  1,800 bytes (`0x708`) apart in the data, plus the value at `0x00F29F74` (role not established).
  The diamonds follow the row layout of `formats/sg3.md`: row r (0 … 2n − 2) holds
  min(r, 2n − 2 − r) + 1 diamonds, centred on x, 60 pixels apart, at y + 15 r. For n = 2: (x, y),
  (x − 30, y + 15), (x + 30, y + 15), (x, y + 30). They are called by the dispatcher before any
  clipping and set width and height to 58 × 30; they return 58. *verified (n = 2 in the listing;
  n = 2 to 5 by the native rewrite, every city drawing of the nine scenarios compared call by
  call; n = 6 is never drawn there), native in the dispatcher.*
- **Building tops** `0x005ADF00(id, x, y)` / mirrored `0x005AE1B0` (`__thiscall`, `this` =
  `0x012AEBE8`, 12 bytes of arguments popped): for an entry with `+0x35` (the mirror link
  `+0x3E` is *not* followed; the image pointer is stored in `0x00F13A18` even when it is ≤ 0 and
  nothing is drawn),
  draw only the compressed part that follows the base (`pixels + [entry + 8]`), placed by base
  width: 58 → (x, y − h + 30), height h − 16; 118 → (x − 30, y − h + 60), h − 31; 178 →
  (x − 60, y − h + 90), h − 46; 238 → (x − 90, y − h + 120), h − 61; 298 → (x − 120,
  y − h + 150), h − 76; 358 → (x − 150, y − h + 180), h − 91 (h = image height). Drawn with the
  run-length routines (shadow flag `+0x3B`) and the overlay. *verified*
- **Glyph mask** (type 20) `0x00416C30`: run-length segments whose pixels are all replaced by
  one colour, `0x00F1397C`. *verified*; that type 20 images are font glyphs: *hypothesis*
  (the colour is set in the text code).
- **Tint** (mode 1) `0x005B86A0(src, colour)`: grey = (r + g + b) / 3 of each source pixel; each
  output component = `(c × grey) >> 5`, rounded up when bit 4 of the product is set, with
  `c` the component of `colour`; handles both 555 and 565. *verified*
- **Fixed squares** `0x00417290` (32 × 32), `0x004171E0` (24 × 24), `0x00417140` (16 × 16):
  opaque copies with a source stride of the full square; clipped left or top by offsetting the
  source. `0x00417100` writes two pixels on one row and one below. *verified*; their use: not
  seen in the run.

## 9. Overlays and colour mixing

| Effect | Mechanism | Where | Status |
|---|---|---|---|
| Transparency | key `0xF81F` (uncompressed), `0xFF n` runs (compressed), `0x03E0` in isometric bases | routines above | verified |
| Shadow | source pixel `0x7C00` → `(dst >> 1) & 0x3DEF` | `0x004161B0` and variants, flag `+0x3B` | verified |
| Alpha | `(a × src + (31 − a) × dst) >> 5` per component, a ∈ 0…31 | `0x00413E30` and variants | verified |
| Tint | grey level × colour / 32 | `0x005B86A0` and variants, `mode = 1` | verified |
| Single-colour mask | every opaque pixel = `[0x00F1397C]` | `0x00416C30` and variants | verified |
| **Overlay** | after the sprite, every pixel it covered: `dst &= [0x0078C570]` | `0x00416070` (uncompressed), `0x00416140` (clipped), `0x00416820` (runs), `0x00416AF0`, `0x00416B90` (runs, clipped); mirrored tops `0x00415B80`, `0x00415C40`, `0x00415D30` | verified |

The overlay colour is set by the city loop just before a building or tile is drawn and cleared
just after: `0xF863` (red: keeps red, crushes green and blue) when byte `+0xBF` of the building
record (`0x0092E727 + 0x118 n`) is set; the value returned by `0x0045DBD0(tile)` for tiles with
flag `0x01000000`; `0x0051EF30` may change it when `0x00665E2C == 1`. **verified** (writes in
`0x00458170`); that `+0xBF` marks a building selected for demolition or out of order is a
**hypothesis**. The overlay routines ran 0 times in this scenario.

## 10. The isometric view

### 10.1 The map grid

- The map is a staggered grid of **`0x1C9` (457) rows × `0xE7` (231) columns** at `0x007D3BEC`,
  one 32-bit tile number per cell; values ≥ `0x0FFF0000` are marks, not tiles (the ground loop
  then uses `value + 0xF0010000` as an image id). One row is `0x39C` bytes. **verified**
- Odd rows are shifted half a tile to the left: the two cells above a cell of an odd row are
  (r − 1, c − 1) and (r − 1, c); of an even row (r − 1, c) and (r − 1, c + 1). **verified**
  (`0x00457900`, offsets `−0x3A0`, `−0x39C`, `−0x398`; `walker_behind`). The parity is counted
  from the first drawn row, not from row 0. **verified**; that the first drawn row is always even
  is a **hypothesis**.
- Per-tile arrays, indexed by the tile number (not the cell): height `0x008E01FA` (s8, 30 pixels
  per step), ground image `0x00D4CFC8`, terrain flags `0x00CCE128`, building `0x00D26E98` (s16),
  bits `0x00D404B8` (`0x40`: anchor tile of what stands on it), slope `0x00D00D68` (low 4 bits:
  size of a sloped piece; `0x10`: replacement ground), first walker `0x00D0D878` (s16), "walkers
  drawn elsewhere" `0x0083AD6C`, `0x00CB4B08` (s8, with flags `0x44` or `0x48`: see 11.2).
  **verified** (all in `src/win32/game_gog.h` except the last). Records: buildings
  `0x0092E668`, `0x118` bytes, `+0` type (s16); walkers `0x00DBD790`, `0x184` bytes, `+0` next
  walker on the same tile, `+2` type, `+0x5D` hidden. **verified**

### 10.2 View parameters (`0x00449E50`)

`0x0051CBA0`/`0x0051CC00` call `0x00449E50(228, 228, x0 = 0, y0 = 30, columns, rows, 58, 30)`
(columns × rows depend on the screen: 10 × 39 at 800×600, 14 × 50 at 1024×768,
`docs/reverse-engineering.md`). It stores: **verified**

| Address | Value |
|---|---|
| `0x007D3BBC`, `0x007D3BB8` | view columns, view rows |
| `0x007D3BE8`, `0x007D3BE4` | x0, y0 of the view on screen |
| `0x007D3BB0`, `0x007D3BB4`, `0x007D3BAC`, `0x007D3BA8` | 58, 29, 87, 116 (tile width and its multiples by ½, 1½, 2) |
| `0x007D3BA0`, `0x007D3BA4`, `0x007D3B9C`, `0x007D3B98` | 30, 15, 45, 60 (tile height and multiples) |
| `0x007D3B94`, `0x007D3B90`, `0x007D3B8C`, `0x007D3B88` | clip rectangle of the view: x0, x0 + 60 × columns − 2, y0, y0 + 15 × (rows − 1) |

### 10.3 Camera and projection

`0x0051CAB0` calls `0x0044A830(column 0x00DA2F64, row 0x00DA2F68, 0x00DA2F80)`, which stores
the camera cell in `0x007D3BD0`/`0x007D3BCC`, sets the clip rectangle to the view, raises the
water-animation flag `0x007D3B1C` when more than 60 units of `0x00F9C0F4` have passed since
`0x007D3B18`, then draws: `0x00458170`, `0x0044AD70`, `0x00451820`, `0x004517F0`, `0x00454420`,
and restores the clip rectangle. **verified** (the argument of `0x00458170` is 0 in view mode
0, 9999 otherwise).

Projection used by the three passes of `0x00458170`: **verified**

- rows `r = camera_row − 8 … camera_row − 8 + view_rows + 29`, columns
  `c = camera_column − 6 … camera_column − 6 + view_columns + 8` (cells outside the grid are
  skipped);
- `x = x0 − 360 − (odd ? 30 : 0) + 60 × (c − camera_column + 6)`;
- `y = y0 − 135 + 15 × (r − camera_row + 8) − 30 × height(tile)`.

A tile is thus a 58 × 30 diamond on a 60 × 15 lattice; the 2-pixel gap is the `+2` added to
`0x007D3BB0` at each column.

## 11. City drawing order, `0x00458170`

8,419 bytes, the largest drawing function and the first game function in the profile (5.4 % of
the corrected time in the run, 51.6 million basic blocks). Before the passes it clears a ring of
5 rows × 231 bytes at `0x0084787C` (flags of walkers deferred to a later row), increments
`0x00847D24` and calls `0x005A7180` (role not established). **verified**

### 11.1 First pass: the ground (native)

For each cell, in row order then column order: compare the height with the cell two rows above
(`compare` = 1 higher behind, −1 lower, else 1 if both have flag `0x200` and the one behind
`0x800200`); skip images that the loop never draws (an interval and a bit set prepared once from
the group numbers listed below, stored at `0x0160C248`, `0x0160C24C`, `0x0160C260`, flag
`0x00847D28`, built from the 40 group numbers at `0x005F8CA8`–`0x005F8D44`); otherwise call **`0x00457900(tile, x, y, odd)`**, the ground of a tile: view-mode
check (method `+0x18` of the view object `0x00641C10[mode]`), animated water (the image advances
one step in its cycle, 6 images, the deep-water cycle by 15), red overlay for a building, sloped
replacement image, walkers standing behind (types `0x01`, `0x04`, `0x0D`, `0x1C`, `0x2A`,
`0x43`, `0x44`, `0x45` on the two cells above force the original path), then the dispatcher.
**verified, native** for the common cases (`native_draw.cpp`, header comment: the three exits
back to the original are listed there).

Counts: `0x00457900` 593,891 calls (1,283 per city frame), 318,084 of which still run in
emulated code with the native functions; diamonds `0x00435070` 163,764 (354 per frame).

### 11.2 Second pass: what stands on each tile

Same traversal, from after `0x0045868C` to `0x00459FF0`; for each cell: **verified** (order of
the calls and conditions as summarised), roles as stated.

1. **Ground drawn again.** The ground is drawn a second time by `0x00457900` when the tile has
   flag `0x40000000` (then cleared) or is higher than one of the tiles behind it (scan of rows
   r − 2, r − 4, …, ending at `0x004589F2`), so that a raised tile covers what was drawn
   behind it; and, for a tile the first pass did not draw, unless a tile two rows in front is
   higher, or four rows in front higher by two steps (`0x00458A19`–`0x00458C2D`). The skip list
   of the first pass applies again.
2. **Ambient counters.** For cells within ±3 columns and rows of the view centre, counters at
   `0x010E41F4`–`0x010E4218` are incremented by terrain kind or by building class
   (`0x005A7200`). Nothing is drawn. *Hypothesis: they drive the ambient sounds.*
3. **Water with a boat or similar** (flags & `0x44` = `0x44` and `0x00CB4B08` set, or `0x48` and
   building type 199): mark the tile in `0x0083AD6C` and draw it with `0x0041D150`
   (0 calls in the run).
4. **The object on the tile**, when the tile is an anchor (bits `0x40`), in view mode 0:
   overlay red if `+0xBF`; flat tiles → building top `0x005ADF00` (or mirrored `0x005AE1B0` when
   `0x0045DC10` says so: `0x00DA2F88` is 2 or 6 and `0x0045DC00`), unless a walker stands behind
   (`0x0045DAF0` on the two cells above); flags `0x4020` → `0x0045A690`/`0x0045A790` with
   size 0; sloped pieces → `0x0045A690(size, image, x, y)`, which offsets the top by
   (29 + 1, −15), (58 + 2, −30), (87 + 3, −45), (116 + 4, −60) or (145 + 5, −75) for sizes 1–5.
   Then extras: `0x0053E5F0` for sanctuaries and monuments, the two towers (images `0x809`,
   `0x80F` at (x − 2, y − 43) and (x + 29, y − 43)) of building types `0x81`, `0x91`, `0x92`.
   Other view modes ask the view object (method `+0x18`), or draw `0x0044BCD0` in mode `0x11`.

Facts found while rewriting the extras (**verified**, listing; counts: `working-city`):

- `0x0053E5F0(this = 0x00FE5100, building, x, y)` (sole caller `0x00459375`, for types
  `0x54`–`0x62`, `0x75` (`0x005344A0`) and `0x64`–`0x72` (`0x0045DBB0`)) calls `0x0055EE60`
  (`RET 0xC`), then `0x00540770`, `0x005406B0` and `0x005406E0`, which take the object of the
  part's class (`0x0053C200`, which writes the building at `+4` of that static object) and call
  its methods `+0x48 (x, y, 0)`, `+0xF8 (x, y)` and, when the sanctuary table `0x00EF5DF0` has an
  entry for the type (`0x00546DB0`) or a monument's first part has a zero byte `+0xCA`,
  `+0xFC (x, y)`. All 171 classes share `+0x48` = `0x005407A0` (3,348 bytes: a deferred part
  `0x00FE6148`, icons when `0x00FE615C` is set, then images of the part; *hypothesis*: statues
  and animations) and its
  helpers `+0x0C` = `0x00535600` (is the part drawn: always for a sanctuary, else the height of its
  tile against the part's height), `+0x1C` = `0x0053E250`; `+0xF8` is `0x0044EE80` (170 classes)
  or `0x005367B0`; `+0xFC` is `0x0055E510` (`RET 8`, 160 classes) or `0x0055B6E0`.
- In `working-city` 52,319 of the 53,150 calls draw nothing: `+0x48` stops after
  `0x00534500` returns 0 and `+0xC4` is false, `+0xF8` returns early (methods `+0xB0`, `+0xB4`,
  `+0xB8` false), `+0xFC` is never reached with anything but `0x0055E510`. The other 831 calls are
  parts of the three classes whose `+0xB0` is true: `0x0044EE80` draws an animated image
  (`0x005AF8C0`, which resets the counter at `+0xDC` of the record when it passes the last
  image).
- The towers are drawn only on the sides where the neighbouring tile of the row above
  (`0x0045A640`) also carries a type `0x81`, `0x91` or `0x92`; `0x0041FD40` is the dispatcher with
  `this` = `0x012AEBE8` and `0x0040FD10` turns the group numbers `0x809`, `0x80F` into image
  ids. A turned view draws them mirrored with `0x005AE9F0`, the two images swapped.
5. **Building animations and special buildings**: a `switch` on the building type calls one
   drawing function per kind (`0x0044C940` + `0x0044C4A0` for types `0x0B`–`0x0E`, `0x0045B500`
   for `0x12`, `0x0045AB70`, `0x0045ADE0`, `0x0045C170`, `0x0044BE90`, `0x0044C0B0`,
   `0x0044BFA0`, `0x0044C2F0`, `0x0045B690`, `0x0045B820`, `0x0045B9B0`, `0x0045BB40`,
   `0x0045BCD0`, `0x0045BE60`/`0x0045BFE0`, `0x0044CD10`, `0x0044CB80`, `0x0045C580`,
   `0x0045B060`, `0x0044E100`, `0x0045CDE0`, `0x0045C710`); otherwise the animation of the image
   (`0x00423180`, `0x00413380`) is drawn by `0x0045D140`. Then `0x0044F320` for every anchor
   (433,862 calls, as many as building tops; *hypothesis*: labels or fire and damage effects).
6. **View-mode overlay** (modes other than 0): methods `+0x0C`, `+0x20` and `+0x00` of the view
   object draw on the building (columns of the risk and service views: *hypothesis*).
7. **Walkers.** After the object of cell (r, c), in this order:
   - if the ring `0x0084787C` (5 rows of 231 bytes, row index modulo 5) has bit 2 for row
     r − 4, the walkers of cell (r − 4, c) (`0x007D2D7C + c × 4 + r × 0x39C`) are sorted
     (`0x00449B30`) and drawn with `0x0044FEF0(walker, x, y − 30 × (height + 2), arg, 1)`, and
     the bit is cleared;
   - the walkers of cell (r − 2, c) (`0x007D34B4 + c × 4 + r × 0x39C`) are sorted and drawn with
     `0x0044FEF0(walker, x, y − 30 × (height + 1), arg, 2)` — last argument 3 when the ring has
     bit 1 for row r − 2 (then cleared); on a tile with flags `0x44` and `0x00CB4B08` set, only
     the walkers for which `0x004D7210` is false;
   - the byte `0x0083AD6C` of the current tile is ORed into the ring for row r;
   - `0x0045A5A0(tile, x, y, arg, byte ≠ 0, 1)` draws the walkers of the current tile (all of
     them, or only those for which `0x004D7210` is true when the byte is set);
   - `0x0041D3D0(tile, x, y, 0)` (620,704 calls).

   Walkers hidden (`+0x5D`) are never drawn; lists stop at a number ≤ 0 or ≥ 2,000.
   **verified** (calls and arguments). *Hypothesis*: the last argument of `0x0044FEF0` selects
   which part of a walker is drawn, so that a walker is drawn in pieces at its own tile and two
   rows later, after the buildings in front of it; `0x0041D3D0` draws animals and boats on
   water.

`0x0044FEF0` (5,977 bytes, the walker sprite) is called 27,178 times (59 per city frame).

Facts found while rewriting it (**verified**, listing):

- The view object of mode 0 (`0x00FE68FC`, method table `0x005E4424` set by `0x0055E9B0`):
  `+0x00` does nothing (`0x0055EE50`), `+0x08`, `+0x10`, `+0x18` return true (`0x0055F3D0`),
  `+0x0C` and `+0x20` return false (`0x00565750`, `0x00565930`). Step 6 therefore never draws in
  the city view.
- Steps 4 and 5 read the type of building 0 when the tile has no building (record 0 of
  `0x0092E668`).
- A tile that is not an anchor draws nothing in step 5 whatever its building type, except type
  `0x39` (`0x004574C0`); but the `switch` conditions call functions, and `0x004FAA00` (type
  `0x7D`) writes the game state (it clears a figure number that no longer matches).
- `0x0044F320` draws only for building types `0x25`, `0x29`, `0x2A`, `0x2E`, `0x30`–`0x33`,
  `0x35`–`0x37`, `0x7D`, `0x84`, `0x85`, `0x87`, `0x88`, `0xD3`–`0xD6`, `0xD8` (table
  `0x0044FD6C` / `0x0044FDC4`); for the others it reads the record and returns, `EAX` = type −
  `0x25`. Each drawn extra goes through `0x0044F1A0(building, image, x, y, dx, dy)`: the
  dispatcher at (x + dx, y + dy), or `0x005AE9F0` when `0x0045DC10` mirrors. In the nine
  scenarios it never draws: 28,144,965 calls, all for other types (`--count 0x0044FD61`).
- The animation of an anchor (`0x0045984B`, image entry `+0x1E` ≠ 0) is two functions with one
  caller each. `0x00413380(image, tile)` (`0x00459C00`) returns the current frame and keeps it in
  the tile's byte at `0x00CB4B08` (the same array as the water life of step 3): 0 without a
  building, or for types that need occupants (`+0x30` > 0: `0x39`, `0x3A`, `0x3E`, `0x4A`,
  `0x4B`, table `0x0041377C`) when there are none, or when `0x00433D50(type, 5)` is non-zero
  (unless byte `+0xCA` is set) and there are none; types `0x0F`–`0x18`, `0x7F`, `0x94`–`0x98`
  (table `0x00413824`) count frames in `+0xDC` of the record instead. The frame advances only
  when the byte of `0x006668EC` for the entry's speed (`+0x3A`) is set, by one up to `+0x1E` then
  back to 1, or back and forth with bit 7 as the direction when `+0x30` is set.
  `0x0045D140(building, image, n, x, y)` (`0x00459C77`) draws image + min(n, `+0x1E`) at
  (x + `+0x22`, y + `+0x24` − `+0x16` + 30 (k + 1) − the height of a k-tile slope piece, k the
  tile's slope `0x00D00D68` & 15, nothing for k > 5), leaving half the width in `0x00847D20`
  and the mirror flag (`0x0045DC10`) in `0x00847D1C`. A switch on the type (`0x0045DA24`)
  chooses: case 0 draws so; case 1 first asks `0x0044EAB0`, which can only return true for types
  `0x2A`, `0x30`, `0x32`, `0x33`, `0x35`–`0x37`, `0x85` and `0x87` (tables `0x0044ED44`,
  `0x0044EE20`; then `0x005AF8C0` and `0x0044DDE0` draw instead); `0x75` and `0x7C` have their own drawing, `0x86`
  none, and every other type draws image (`0x00847D14` + building) mod 6 of group `0x0137C02C`
  (*hypothesis*: smoke). Sanctuaries and monuments (`0x005344A0`, `0x0045DBB0`) take the type
  from `0x00547980`. In the nine scenarios 29,594 calls: case 1 with `0x0044EAB0` false and a
  slope of 1 for all but the 463 sanctuary calls of `working-city` and its 113 of type `0x39`;
  frames never go back and forth and never use `+0xDC`.
- The walkers of row r − 2 are read from `0x007D34B4 + c × 4 + r × 0x39C` without checking that
  r ≥ 2. In the run below the ring bit of row r − 4 is set at 193,102 cells, while the walkers
  of that row are drawn only 2,447 times: almost always the list is empty and the original only
  clears the bit. Likewise 91,903 tiles have a non-zero `0x0083AD6C`, which is only ORed into
  the ring.
- `0x00449B30`, the sort, writes the game state (the order of the list of a tile, `0x00D0D878`
  and the `+0` links of `0x00DBD790`) as soon as a tile holds two walkers: drawing reorders the
  walkers.

**Native rewrite** (`native_draw.cpp`, `objects_row`). A host jump at `0x00458792` (a cell inside
the grid) handles cells one after the other and hands back to the original, with the registers
and variables it would have, at the first step it does not handle; each step decides before it
writes anything. Resume points: the start of the cell (bytes displaced by the jump), `0x00458D7A`
(after the ambient counters), `0x004594FA` (after the object, `EBP` = y), `0x00459CE2` (before
`0x0044F320`: `EBX` building, `EDI` x, `EBP` y, `[ESP+0x28]` the tile bits), `0x00459D7A` (the
walkers: `EDI` x, `EBP` y), and `0x0045A022` at the end of a row (`EDX` the ring row, `ESI` 15,
`EDI` the row). Handled: view mode 0 with the method `+0x0C` above; ground drawn again through the
native `0x00457900`; the counters; building tops `0x005ADF00` and sloped tops `0x0045A690` (not
red, not turned), with `0x0053E5F0` when it draws nothing and the towers when the dispatcher
draws both images (`native_sanctuaries.cpp`); step 5 when nothing is drawn (no animation, types
outside the `switch`, or tiles that are not anchors); `0x0044F320` for types it does not draw;
step 7 when the three lists are empty. Scenario `working-city`, 300 s deterministic, 606,993
cells: **490,265 (81 %) were handled entirely in native code** when this pass was first rewritten;
the original resumed 35,158 times at the start of a cell (mostly bases of 2 to 6 tiles and
`0x00546530`), 52,802 times after the counters (sanctuaries and monuments, `0x0053E5F0`), 8,123
after the object (animations) and 20,645 at the walkers. With the bases and the clipped diamonds
native: **509,717 (84 %)**; the original resumes 5,924 times at the start of a cell (all
`0x00546530`), 53,708 after the counters, 16,220 after the object and 21,424 at the walkers.
With `0x0053E5F0` and the towers native: **556,943 (92 %)**; 5,924 at the start of a cell,
none after the counters, 16,220 after the object and 27,906 at the walkers. `0x0053E5F0` is
still reached from the cells restarted by the original (5,924 calls): its fast path takes 5,093
of them, and the 831 that draw run in the original (`--count 0x00540770`). The 6,482 tower
cells are all native (`--count 0x004593D6`: 0).

Animations (`native_animations.cpp`): an anchor with an animated image stays native when
`0x00413380` and `0x0045D140` are cases that file handles (no counter `+0xDC`, no back and
forth, no sanctuary, case 0 or case 1 with `0x0044EAB0` always false, not red, not mirrored,
type not `0x39`, `0x3A`, `0x3D`, `0x3E`); the two functions and `0x0044F320` (types it does not
draw) also have fast paths, so `--verify` compares them call by call. In `working-city` the
original now resumes 5,924 times at the start of a cell, 53,708 after the counters, 12,308 after
the object, never before `0x0044F320` and 21,704 at the walkers: **513,349 cells (84.6 %)** stay
native; the original still calls `0x0045D140` and `0x00413380` 576 times (cells it resumed
earlier, through the fast paths) and `0x0044F320` 64,856 times, all through its fast path.

### 11.3 One walker, `0x0044FEF0`

`0x0044FEF0(walker, x, y, wanted, layers)` (cdecl). Record fields are given relative to the
record of walker 0 (`0x00DBD788` + n × `0x184`); all **verified** by the listing and by
`zeus-run --verify 0x0044FEF0` (section 13).

1. **Layer.** Nothing happens unless `layers` has bit 1 (byte `0x00DBD7A5` of the record is 0)
   or bit 2 (it is not). In `working-city`, 13,711 of the 27,178 calls go on (`--count 0x0044FF29`).
2. **Offset in the tile** (`0x0044FF29`–`0x00450069`). Start at (2, 10). For types 1, `0x0B`–`0x0D`,
   `0x28`–`0x2A`, `0x37` and `0x3B`, y gains −5 or 7 when bit 6 of the terrain of the tile at
   `0x00DBD8A4` matches byte `0x00DBD7C9`, otherwise an entry of a 16-value table indexed by the
   step `0x00DBD7C1` (reversed when the bit is set); *hypothesis*: boats and their crews at a
   quay. Types `0x3A`
   and 6 (kinds `0x12`, `0x0D`) ask the C++ object of their building (`0x00534A70`). A tile with
   terrain bit `0x20000` lowers the sprite by 8 (byte `0x00C1C648` = 0) or 4 (= 100).
3. **Direction in the view**, written to `0x00FA05E4`: direction `0x00DBD799` (or `0x00DBD79A`
   when ≥ 8, or 0) minus the view orientation, modulo 8. If `0x00B1E8C4` = 1 the function stops
   here.
4. **Position.** With byte `0x00DBD794` = 0 the walker is placed by its view direction and step
   (tables `0x005F0658` for x, `0x005F06D8` for y, 16 steps per direction) minus byte
   `0x00DBD800`; otherwise by its fine position (`0x00DBD7D2`, `0x00DBD7D4`, each modulo 15) in
   one of two 16 × 16 tables per axis (`0x005F0258`/`0x005F0358` for orientations 0 and 4, signs
   reversed in 4; `0x005F0458`/`0x005F0558` for 2 and 6), minus byte `0x00DBD7A4`. Type `0x2D`
   linked to a `0x2E` and type 6 have further corrections (`0x004501A1`, `0x0045021E`). Each
   sprite is then placed by the anchor (+`0x22`, +`0x24`) of its image entry.
5. **Wanted walker.** `wanted` = 0 draws; 9999 asks the view object (`0x0044FEA0`); any other value
   draws only that walker and stores its position in `0x007D3B38` (y) and `0x007D3B3C` (x).
6. **Sprites**, through the dispatcher, with mode 1 (tint) when the colour `0x00DBD8F0` is not 0.
   Body `0x00DBD78C`; second sprite `0x00DBD908` at the offset (`0x00DBD7F4`, `0x00DBD7F5`);
   third `0x00DBD8D8`. Without a second sprite: the body (type 6 mirrored by bit 6 of
   `0x00DBD8F6`, or kind `0x0E` by the orientation); type `0x39` adds the third. With one, by
   type: 4, `0x24`, 9, `0x1C` draw the second sprite first unless the view direction is 3 (or out
   of 0–7), `0x43` always after, `0x45` always before; 1, 2, `0x0D`, `0x2A` place it from the
   body, before when its y offset is negative; `0x2D`, `0x34`, `0x36` draw it 10 pixels lower,
   first; `0x39` draws body, second, third; `0x22` writes a text (`0x00508F00`); `0x0E` with a second
   sprite (6,482 of the 13,711 drawn calls of `working-city`) reads the record of `0x00DB7B44` (`0x9C` bytes)
   named by its field `0x00DBD7E8`: nothing at all (not even steps 7 and 8) when its `+0x7E` is
   ≥ 0, otherwise the body 6 pixels higher, the second sprite 9 pixels right and its height
   higher, then by the record's kind (`+0`: 11, 12, 13) a third sprite, the first image of group
   75, 78 or 77 of the table `0x012E2CFC` plus `0x10000` (or of group 7 of `0x015479BC` plus
   `0xD0001`, `0xD0002`, `0xD0000` in a Poseidon game), stacked on the second; all three untinted.
   Every other type draws only the body.
7. **Water.** Without a second sprite, a walker on water (`0x004D7210`) calls `0x0041D3D0` on its
   tile and seven neighbours (offsets `0x005D9188`); then, for every type but 6, `0x0041D3D0` on
   its tile. `0x0041D3D0` does nothing outside a Poseidon game (`0x004200F0`) or on a tile
   without water life (terrain `0x44`, byte `0x00CB4B08`).
8. **Tiles to draw again.** For 4 neighbours (9 for types 1, 4, `0x0D`, `0x1C`, `0x2A`,
   `0x43`–`0x45`; tables `0x005F8B88` and `0x005F8BD0` by row parity) higher than the walker's
   tile, bit `0x40000000` of the terrain is set, unless the neighbour holds a building of type 2,
   9, `0x7B`, `0x86`, or its ground image is one the loop skips (`0x0160C260`); sanctuaries and
   monuments ask their C++ object (`0x00546530`). Lower or level neighbours among the first four
   lose the bit, for the nine-neighbour types. The third pass draws these tiles again over the
   walker (*hypothesis* on the purpose).

**The sort of a tile's walkers, `0x00449B30(tile)`** (cdecl), called before the walkers of a
tile are drawn (11.2 step 7, three call sites: `0x00459DCD`, `0x00459EE0`, `0x0045A5A9`).
**verified** by the listing and by `zeus-run --verify 0x00449B30` (section 13). It copies the
list of the tile (first walker in `0x00D0D878`, next one in `+0` of each record) into a local
array of (walker, rank) pairs, sorts it with the C runtime `qsort` (`0x005CC22F`) and the
comparator `0x00449B10` (rank, then walker number, both ascending), and writes the list back in
that order. A list of one walker is left as it is, and so is a list of more than 50 (the
function returns as soon as it meets the 51st, without writing anything). The rank is:

- −100 when byte `0x00DBD796` of the record is not 1 or byte `0x00DBD7C0` is 4 (*hypothesis*:
  walkers not standing on the map, or in some special state), so they come first;
- −50 for type 6 of kind `0x0D` (a boat, *hypothesis*);
- otherwise the vertical offset of step 4 of `0x0044FEF0` without its corrections: by direction
  and step (`0x005F06D8`), or by fine position (`0x005F0358` for orientations 0 and 4, reversed
  in 4; `0x005F0558` for 2 and 6, reversed in 2); 0 for any other orientation.

So the walkers of a tile are drawn from the back to the front. Since the comparator never
returns 0 for two distinct walkers, the order does not depend on how `qsort` works: the native
version (`native_walkers.cpp`, fast path) sorts with `std::sort` and gives the same list. It
leaves to the original a list holding a number outside 1–1,999, which the nine scenarios never
meet. In `working-city` (300 s, original alone, `--count`), 30,396 of its 1,411,706 calls find a
walker and 1,680 sort two or more; with native code on, the second pass calls it only 198,260 times.

### 11.4 Third pass

For anchor tiles with flag `0x08` and a building, in modes other than `0x10`, the view object
(methods `+0x0C`, `+0x20`, `+0x00`) draws once more over everything. **verified** (control
flow); what it draws is a **hypothesis** (the mode overlays above buildings).

### 11.5 After the passes

`0x0044AD70` (tiles again: construction preview or selection, *hypothesis*), `0x00451820`,
`0x004517F0`, `0x00454420` (`0x0091EA6C` per tile: *hypothesis*, labels and flying effects), once
per city frame each.

## 12. Real-time rate

`zeus-run --headless --ticks --count …` with the wall clock, same scenario, 150 s, native
functions on: 3,344 simulation ticks in 74.4 s of active play (44.9 per second) and 3,396 city
drawings (`0x0044A830`): **the city is drawn once per simulation tick, about 45 times per
second**, while only 2,523 frames are presented in the whole run (17 per second, section 3.3).
About half of the city drawings are therefore never shown (issue #36 studies skipping them).
Per city drawing in this run: 945 building tops (`0x005ADF00`, 3,208,047 calls), 681 ground
calls (`0x00457900`), 69 walkers (`0x0044FEF0`, 235,448 calls); the dispatcher ran 5,448,448
times in the 150 s. To convert the per-frame column of section 13 into calls per second,
multiply by 45.

## 13. Order of the rewrite

What is left to make the rendering fully native, in the order proposed: each step only needs the
steps before it, and the most frequent come first within a step. Counts: original code alone,
`working-city`, 300 s deterministic, 463 city frames.

| # | Function | Role | Calls (run) | Per city frame | Now |
|---|---|---|---|---|---|
| 1 | `0x004163B0`, `0x004165D0` | run-length image clipped left / right | 5,563 / 5,703 | 12 / 12 | **native** |
| 2 | `0x00415F50` | keyed copy, whole width | 95,842 | — (menus too) | **native** |
| 3 | `0x004377B0`, `0x00438A90` | ground diamond clipped left / right | 8,682 / 8,691 | 19 / 19 | **native** (overlay: original) |
| 4 | `0x00415490`, `0x004158E0`, `0x00415680` | mirrored run-length images | 1,020 / 57 / 2 | 2 / 0.1 / 0 | **native** |
| 5 | `0x00416C30`, `0x00416F00`, `0x00416FA0` | glyph masks | 934 / 0 / 0 | — | original |
| 6 | overlay `0x00416070`, `0x00416140`, `0x00416820`, `0x00416AF0`, `0x00416B90`, `0x00415B80`, `0x00415C40`, `0x00415D30` | `dst &= colour` | 0 | 0 | original |
| 7 | alpha variants `0x00414CE0`, `0x00414540`, `0x00414190`, `0x004150B0`, `0x00414910`; tint `0x005B86A0`, `0x005B8910`, `0x005B8B60`; squares `0x00417290`, `0x004171E0`, `0x00417140`, `0x00417100`; mirrored keyed `0x00415E30`, `0x00415EC0` | rare cases | 0 | 0 | original |
| 8 | `0x005ACF50` | dispatcher: every remaining case, then the whole function | 8,215,286 | — (all screens) | fast path (clipped run-length images included) |
| 9 | `0x005ADF00`, `0x005AE1B0`, `0x0045A690`, `0x0045A790` | building tops | 433,862 / 0 / 45,747 / 0 | 937 / 0 / 99 / 0 | `0x005ADF00` fast path (all but the overlay) |
| 10 | `0x0043FF10`, `0x00440170`, `0x004406B0`, `0x00440FE0`, `0x00441E50` | bases of 2–6 tiles | 23,888 / 2,410 / 3,627 / 1,389 / 0 | 52 / 5 / 8 / 3 / 0 | **native** (in the dispatcher; overlay: original) |
| 11 | `0x00457900` | ground of a tile, remaining cases | 593,891 | 1,283 | fast path (flat, sloped, flags `0x4020`), 5,941 still emulated (42,500 before the bases were native) |
| 12 | `0x00449B30` | sort the walkers of a tile | 1,411,706 | 3,049 | **native** (11.3) |
| 13 | `0x0045A5A0`, `0x0041D3D0`, `0x0041D150` | walkers and water life of a tile | 606,993 / 620,704 / 0 | 1,311 / 1,341 / 0 | `0x0041D3D0` fast path (when it draws nothing: every call outside a Poseidon game); `0x0045A5A0` original, now possible since the sort (12) is native |
| 14 | `0x0044FEF0` | one walker | 27,178 | 59 | fast path (11.3): all but the C++ objects of sanctuaries, the trader's text and tinted walkers whose image the dispatcher refuses; in `working-city` no drawn walker is left to the original (`--count 0x0044FF29`: 0) |
| 15 | `0x0044F320`, `0x0045D140`, `0x0053E5F0`, `0x0045DC10`, building-type functions of 11.2 step 5 | building extras and animations | 433,862 / 4,488 / 53,150 / 553,374 / — | 937 / 10 / 115 / 1,195 / — | `0x0053E5F0` native when it draws nothing (52,319 of 53,150 calls; fast path), towers native; `0x0044F320` fast path for the types it does not draw (all calls in the nine scenarios); `0x0045D140` and `0x00413380` fast paths and native in the second pass for their common cases (11.2); the rest original |
| 16 | `0x00458170` | second and third passes | 463 | 1 | first pass native, second pass native for @PCT@ of the cells (11.2) |
| 17 | `0x0044AD70`, `0x00451820`, `0x004517F0`, `0x00454420` | after the passes | 463 each | 1 | original |
| 18 | `0x0044A830`, `0x0051CAB0`, `0x00449E50` | view set-up | 463 / 463 / rare | 1 | original |
| 19 | `0x00514020`, `0x0050C1B0` and converters `0x004138B0`, `0x00413930`, `0x004139B0`, `0x00413A30` | frame and presentation | 8,398 / 8,412 | — | original (the host replaces the surfaces) |
| 20 | `0x0050A860`, `0x0050A000`, `0x00509F90`, `0x005AE770`, `0x005AE730` | external images and backdrops | 0 | 0 | original |

Already native: `0x00413DC0` (5,319,977 calls), `0x004161B0` (2,384,169), `0x004163B0` and
`0x004165D0` (5,563 and 5,703), `0x00415490`, `0x004158E0`, `0x00415680`, `0x00415F50`
(95,842), `0x00416010` (467), `0x00413E30` (4,317), `0x0050B410` (1,459), `0x00435070` (163,764),
`0x004377B0` and `0x00438A90` (8,682 and 8,691), the clip functions (8,562,614 each) inside the dispatcher and the
building tops, `0x005ADF00` except with an overlay, the first pass of `0x00458170`, `0x0044FEF0` for its
common walkers, `0x0041D3D0` when it draws nothing and the walker sort `0x00449B30`.

Where the list stands: steps 1 to 4 are native; 5 to 7 are never called in the nine scenarios,
so no call-by-call proof is possible yet and they wait for a scenario that draws glyph masks,
overlays (a building marked red) and the rare cases; the second pass (16) is native for its
common cells; the bases of 2 to 6 tiles (10) and the walker sort `0x00449B30` (12) are native.
The extras of sanctuaries and monuments (15, `0x0053E5F0`) are native when they draw nothing,
and the animations (15) for their common cases; the parts that draw (`0x0044EE80`, `0x005407A0`
beyond its first tests), the 21 types `0x0044F320` draws and the per-type functions of step 5
are left to the original. Next, in this order: the walker loops that call the sort
(`0x0045A5A0` and the two loops inlined in the second pass, which could then run natively with
`0x0044FEF0`).

Weight of what is left (`zeus-run --profile`, `working-city`, natively, 2026-10-15): the
walkers (`0x0044FEF0` 0.3 %, `0x0045A5A0` 0.2 %, `0x0041D3D0` 0.1 % before this rewrite) and
their sort (0.3 %) weigh little; the text routines (`0x004F57A0` 10.3 %, `0x004F5300` 7.9 %,
`0x00507F20` 3.4 %, `0x004F59B0` 2.8 %, `0x00508B50` 2.3 %) and the building objects
(`0x0053CD80` 3.0 %, `0x0053CC30` 2.1 %) weigh far more.

Outside this page but heavy in the same profile: the text routines (`0x004F57A0`, 2.2 million
calls; `0x00507F20`, 10.0 million; `0x00508B50`, 1.0 million; `0x004F59B0`, 1.1 million;
`0x004F5300`, which calls the dispatcher). They belong to the "interface and texts" subsystem.

## 14. Open questions

- Unit of the presentation accumulator `0x00F9C0F0` (milliseconds?).
- Role of `0x00F29F74` (added to the base pixels of multi-tile diamonds), of `0x005A7180`, and
  of `0x0044F320`, `0x0041D3D0`, `0x0044AD70`, `0x00451820`, `0x00454420`.
- Whether the first drawn row is always even (the parity of `0x00458170` is relative).
- When `0x0045DC00` mirrors buildings (`0x00DA2F88` ∈ {2, 6}).
- Which callers produce the 5.3 million opaque copies `0x00413DC0` (menus and panels, not the
  city: 11,490 per city frame would be far more than the visible tiles).
