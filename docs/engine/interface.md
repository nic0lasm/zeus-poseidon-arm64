# Interface and texts

How `Zeus.exe` (GOG edition, SHA-256 `382bcd28…75e90a57`) reads the mouse and the keyboard, lays out its
windows and panels, and draws its texts: the screen switch, the widget tables, the control panel, the
information and message windows, the tooltip, the fonts and the text routines. The goal is a description
precise enough to rewrite the subsystem by hand without reading the machine code again. It follows
[rendering.md](rendering.md), which describes what the draw calls below end up in (the sprite dispatcher
`0x005ACF50` and the clipping inside it).

Every claim carries its evidence (an address, an offset, a measurement) and a status:

- **verified**: read in the decompilation or the listing of the executable (and, where noted, confirmed by
  a call count or by the native code of #35, which kept the eight scenarios identical);
- **hypothesis**: inferred from names, data, a debug string or context, not proven.

Addresses are those of the GOG executable. `FUN_xxxxxxxx` names are Ghidra's. The tables of the game are
described by their layout and their address only; nothing is copied from the executable.

## 1. Measurements used in this page

Three scenarios of the bench (`tests/scenarios/`), each played to the end of its duration with
`zeus-run --headless --deterministic --mute --count ADDRESSES` (calls counted natively and emulated alike):

| Scenario | Duration | Game turns (`0x00514020`) | City frames (`0x0051CAB0`) | What it shows |
|---|---|---|---|---|
| `working-city` | 300 s | 8,398 | 463 | a large city, then the objectives screen for a quarter of the turns |
| `options` | 125 s | 18,513 | 4,873 | every window of the File and Options menus, from the city |
| `tutorial` | 90 s | 9,614 | 1,564 | the first tutorial: panel, messages |

The deterministic clock does not run at wall-clock speed (every clock read adds 1 ms), so the game turns
many more times per simulated second than in real play. The counts are therefore **per run**, to be read as
ratios between functions, not as rates. The #35 measurements (section 11) showed that in real time the
control panel lays its texts out about 4.5 times a second, against 4.9 per city frame here. Measurement
logs are not kept in the repository; the command is

```bash
./build/zeus-run --run original/gog/app/Zeus.exe --headless --write-dir build/essais/NAME --mute \
    --deterministic --seconds S --count 0x00507F20,0x004F57A0,… --script "$(grep -v '^#' tests/scenarios/NAME.txt | head -1)"
```

## 2. The frame, seen from the interface

| Step | Function | What it does | Status |
|---|---|---|---|
| One turn of the main loop | `0x00513580` | in this order: the simulation ticks due (`0x00513A30`), mouse poll `0x0050B470`, background repaint `0x00513B30(0)`, `0x00512970`, then (after the code that handles loading a game) `0x00514020`, `0x00514C10`, `0x0050B450` | verified (8,398 calls of each = 8,398 turns) |
| Poll the mouse | `0x0050B470` | turns the raw mouse state into edges, once per turn (section 3.1) | verified |
| Background repaint | `0x00513B30(0)` | returns at once unless `0x00F2A6CE` is set; otherwise a `switch` on the screen number redraws that screen's background (screens `0x01`, `0x20`, `0x48`, `0x58` → `0x0058E2E0`; `0x04` → `0x00422C60`; `0x05` → `0x004F2830`; `0x06` → `0x0058F620`; …) and clears the flag | verified (structure; not counted) |
| Draw the current screen | `0x00514020` | `switch` on the screen number `0x00F2A6DD`; calls the screen's drawing function(s) | verified |
| Input of the current screen | `0x00514C10` | `switch` on the same byte; hit-tests and clicks, **after** the drawing of the same turn | verified |
| Clear the edges | `0x0050B450` | zeroes `0x00F2A700`, the click flag `0x00F2A706` and `0x00F2A6FA` | verified |
| Redraw flags | `0x00F2A6CD`, `0x00F2A6CE`, `0x00F2A6CC` | see below | verified (accesses), meaning of two: hypothesis |

The byte `0x00F2A6DD` is the **screen number**; there are about 60 values (section 4). Three flags steer what
is repainted. `0x00F2A6CE` is written in 402 places and read in 17 (`0x00513B30` and the drawing functions of
several windows, e.g. `0x004316F0`, `0x004F2D80`, `0x0058F620`): "the background of the screen must be
redrawn" (it is cleared by `0x00513B30` after the redraw) — **verified** for the clearing, the rest: hypothesis.
`0x00F2A6CD` is written in 84 places, read in 3 (`0x004316F0`, `0x0058EA50`, `0x0059D280`) and cleared at
the end of `0x00514020`: *hypothesis*, "redraw all the widgets this turn". `0x00F2A6CC` is read once, in the
presentation `0x0050C1B0`: *hypothesis*, "present the frame". A button whose flash counter is not zero is
drawn even when no flag is set (section 5.3). The frame buffer is never cleared by these functions (it is
cleared by `0x0050B410`, rendering.md section 2): the interface relies on what the previous turn left in it.

## 3. Input

### 3.1 Mouse

The window procedure `0x005C4A90` (reached through tables, so Ghidra does not see it as a function; it is
printed by `Listing.java`, see `docs/reverse-engineering.md`) only **records** state:

| Message | What it writes | Status |
|---|---|---|
| `WM_MOUSEMOVE` (`0x200`), and the end of every button message | x and y as 16-bit values: `0x00F2A70E`, `0x00F2A70C` (`0x005C582C`) | verified |
| `WM_LBUTTONDOWN` (`0x201`) | bit 0 of `0x00F2A70B` set, flag `0x00F2A706` = 1 (`0x005C58C0`) | verified |
| `WM_LBUTTONUP` (`0x202`), `WM_RBUTTONDOWN` (`0x204`), `WM_RBUTTONUP` (`0x205`) | bit 0 cleared, bit 1 set, bit 1 cleared in `0x00F2A70B` | verified |
| `WM_MOUSEWHEEL` (`0x20A`) | adds the delta to `0x0160AC90` (per screen, reset when the screen changes, `0x00661511`); on screens `0x01` and `0x15` the wheel scrolls through `0x00403E20`, on `0x27` through `0x00403EB0`; the delta is divided by 120 | verified (`0x005C593F` to `0x005C59B4`) |

The game then reads the state **once per turn** in `0x0050B470` (called by the turn, 8,398 times = once per
turn; its sibling `0x0050B450` clears the flags and is called as often):

1. It rescales the position from window pixels to frame-buffer pixels: x = `0x00F2A70E` × (buffer width × 100
   / window width) / 100, the same for y, with the window rectangle `0x00F2AB08…0x00F2AB14` and the buffer
   size `0x00F2A6A0`/`0x00F2A69C`. The result is the **mouse position used everywhere else**, x in
   `0x00F2A738`, y in `0x00F2A734`. The previous position is kept in `0x00F2A714`/`0x00F2A710`. **verified**
2. It derives the **edges** from the level in `0x00F2A70B`: `0x00F2A707` left press, `0x00F2A708` left release,
   `0x00F2A6FB` right press, `0x00F2A6FC` right release; `0x00F2A704` is the left level and `0x00F2A6F8` the
   right level; `0x00F2A744` is set when anything changed (position or button); `0x00F2A740` counts the time
   the mouse has not moved (`0x00F9C0F4` is the game clock). A left press also calls `0x005A4C00(0)` (20, 22 and
   6 calls in the three runs: a click sound, *hypothesis*). **verified** (edges), *hypothesis* (sound)
3. The edges last exactly one turn: they are reset to zero at the start of the next poll (`0x0050B450`, called at the
   end of the turn, clears the click flag and two more). A click shorter than
   one turn is lost, which is why scripted input needs the press one turn after the move
   (`docs/reverse-engineering.md`). **verified**

### 3.2 Keyboard

`WM_KEYDOWN` (`0x100`, handled at `0x005C4E04`) first sets `0x00F2A752` = 1, then branches on the key through
two tables. The byte table at **`0x005C5A6C`** is indexed by *virtual key − 8* (112 entries, key `0x08` to
`0x77`) and holds an index into the jump table at **`0x005C59EC`** (31 handlers, `0x1F` = ignore). Read from
the tables:

| Key (virtual-key code) | Handler | What it does | Status |
|---|---|---|---|
| Backspace (`0x08`) | `0x005C5265` | text field: `0x005082B0` moves the cursor left and erases (`0x00508440`) | verified (calls); role: hypothesis |
| Tab (`0x09`) | `0x005C54F6` | sets the panel category `0x00FA0000` to 1 if it was 0, else to 0 (`0x00516D30`) when `0x00665E2C != 1`; `0x0051EEF0` otherwise | verified |
| Enter (`0x0D`) | `0x005C526F` | confirms a text field (`0x005082D0`, `0x005082A0` sets `0x00F13C90`) | verified (calls); role: hypothesis |
| Shift (`0x10`), Ctrl (`0x11`) | `0x005C4E52`, `0x005C4E47` | set `0x00F9C10C`, `0x00F9C108`; cleared by `WM_KEYUP` (`0x005C4DEE`, `0x005C4DF9`) | verified |
| Escape (`0x1B`) | `0x005C5524` | on screen `0x00`: asks to quit (`0x00F1389D` = 1); elsewhere shows a **quit confirmation** (text group 5, ranks 0 and 1, `0x00508060`/`0x00508080`) through `0x005A0980(…, 4)` (a modal loop, section 3.4) and asks to quit when it answers 6 | verified |
| Page up / down (`0x21`, `0x22`) | `0x005C50E3`, `0x005C518D` | with `0x00665E2C == 1`: `0x00408930` on screens `0x01` and `0x58`. Otherwise a byte table (`0x005C5B78`, `0x005C5BD4`) by screen: screens `0x01`, `0x48` → `0x00408930` with `0x00F135F8` = 2 (up) or 1 (down); list screens `0x0D`, `0x10` → 12 steps of `0x00401B10`; `0x2F` → 12 of `0x00586370`; `0x3D` → 15 of `0x0042FA40` | verified (calls). `0x00408930` stores `0x00DA2F88`, which the debug overlay labels "MAP ROTATION": rotates the map, *hypothesis* |
| End, Home (`0x23`, `0x24`) | `0x005C4F6E`, `0x005C4F40` | text cursor to the end / to the start; on screens `0x01`, `0x20`, `0x48`, `0x58` `0x00408930` with `0x00F135F8` = 0 | verified (calls) |
| Left, Right (`0x25`, `0x27`) | `0x005C4F9F`, `0x005C4F78` | text cursor ±1; scroll the map one step west / east (`0x00407600(6 / 2, 1)` or 8 steps with Ctrl) | verified (calls); direction names: hypothesis |
| Up, Down (`0x26`, `0x28`) | `0x005C504C`, `0x005C4FC6` | scroll the map north / south the same way; on the list screens a second byte table (`0x005C5AEC`, `0x005C5B30`) sends them to the list scrollers `0x00401B10`, `0x0042FA40`, `0x00586370` with `0x00F135F8` = 1 / 0 | verified (calls) |
| Insert, Delete (`0x2D`, `0x2E`) | `0x005C5243`, `0x005C524D` | `0x005082E0` toggles insert mode (`0x00F13A93`); Delete calls `0x005BF880` on screen `0x0E` | verified (calls) |
| `1`…`9` (`0x31`…`0x39`) | `0x005C4E5D` | with Ctrl held: `0x00433AB0(n − 1)` on the object at `0x007818B0` | verified (calls); role: hypothesis (control groups) |
| `A` and numpad 1 (`0x41`, `0x61`) | `0x005C5291` | unless `0x00661510` or `0x00777289` is set: with Ctrl on screen `0x01` flips `0x00665DF0` (the "Allow All" text of the top bar), then `0x00402B80`, `0x00403DF0` | verified (stores); role: hypothesis |
| `G`/numpad 7, `H`/numpad 8, `J`/multiply (`0x47`/`0x67`, `0x48`/`0x68`, `0x4A`/`0x6A`) | `0x005C4E7D`, `0x005C4EBE`, `0x005C4EFF` | with Ctrl held, on screen `0x01`, unless `0x00661510` is set: step `0x00665DEC` (0…13), `0x00665DE8` (1…3), `0x00665DE4` (0…4), which `0x0058F130` shows as the event text, its level and its kind: **developer shortcuts left in the release** | verified (stores and the top-bar reads) |
| `R`, numpad 2 (`0x52`, `0x62`) | `0x005C530E`, `0x005C5322` | flips `0x0160AC8C`; steps `0x00F2A79C` down from `0x1B` | verified (stores); role unknown |
| F1 … F8 (`0x70`…`0x77`) | `0x005C534F`, `0x005C5373`, `0x005C538A`, `0x005C53A2`, `0x005C53BA`, `0x005C5417`, `0x005C542D`, `0x005C5443` | F1–F4: `0x005C5C80(n)` (go to the saved view *n*, slots at `0x00FA05C0`) or, with Ctrl, `0x005C5CC0(n)` (save the current map position there); F5: switches full screen and window (`0x00F2A764`); F6, F7: `0x005C4A30(2 / 3)`; F8: places the window | verified (calls); the slots: verified (stores in `0x005C5CC0`) |

`WM_CHAR` (`0x102`, `0x005C560F`) feeds every character to the **text-field filter `0x00508140`**, which keeps
digits, ASCII letters, space, `- , . ? !` (the four punctuation marks only when the field allows them,
`0x00F13B34`) and a list of accented Latin-1/Windows-1250 letters, and appends it to the field buffer
(`0x00508380`). On top of that: `[` and `]` call `0x005C5D30(1 / 0)` (on screen `0x01`: `0x00402430`, a speed
step, *hypothesis*), `P`/`p` toggles the pause flag `0x00F2A768` on the city screens (the banner drawn by
`0x0058F130` when it is set, section 6.5), `M`/`m` toggles `0x00665E3E`, `+` and `-` step the counter `0x008E01F8`
(0 to 16) when `0x00665E2C == 1` (the other view mode, probably the editor) and call `0x005C3670`/`0x005C36C0`
otherwise, space swaps the overlay `0x00FA05F0` with the one saved for the category (`0x00516C60`) and brings screen
`0x20` back to `0x01`, and `0`…`9`, `-` call `0x0051CA20`, which selects a
build-menu category (it sets `0x00F135F8` = index and calls `0x00516D50`). **verified** (conditions and
stores); the roles of the targets are *hypothesis*.

`WM_SYSKEYDOWN` (`0x104`, Alt+key, `0x005C5842`) goes through a third table (`0x005C5C30`, jump table
`0x005C5C1C`): on screens `0x01`, `0x48`, `0x20` Alt+digit calls `0x00433B60(n − 1)` on the object at `0x007818B0`
(the counterpart of Ctrl+digit); other entries toggle `0x0160AC78`, post an Escape key message (`0x005C585D`) or call
`0x00409A20`/`0x004604E0`. Separately, for every key message the window procedure first calls `0x0042C700`
(90 calls in `working-city`): on screen `0x01` with Alt held, key `1` or `2` writes the screenshot `shot1.bmp` or
`shot2.bmp` (`0x0050B030`). **verified** (tables, calls); roles of the Alt entries: hypothesis

Not read: the handlers behind the per-key calls (`0x00407600`, `0x00433AB0`, `0x005BF750`…), and the
`WM_KEYUP` path beyond Shift and Ctrl.

### 3.3 One input pass per turn

`0x00514C10` (6,908 bytes, called once per turn, after the drawing) does the clicks:

1. copies the mouse position to `0x00F2A730`/`0x00F2A72C` and clears the hover index `0x00781844` and the
   other per-turn variables (`0x00781834`, `0x0078183C`…); the pressed widget `0x007818A0` is kept between turns;
2. reads the clock (`timeGetTime`) and keeps in `0x00F9F9A0` whether at least 30 ms have passed since the last
   time (`0x00F9F99C`);
3. `switch` on `0x00F2A6DD`. Each case runs the same three-call pattern on **its own widget table(s)**, with the
   screen's origin offset (section 5.1): hit-test `0x00432300`, hover/layout `0x00513180`, click `0x004321E0`.
   Where the screen is a city (`0x01`, `0x48`, `0x20`) it first tests the panel and the windows, then the map.

**verified** (the structure); which screen each table belongs to is in section 4.

### 3.4 Modal loops

`0x005A0980(a, b, c)` runs a Windows message loop of its own: it sets the screen to `0x54`, pumps messages with
`PeekMessage`/`GetMessage`/`DispatchMessage`, calls `0x005A0900` while the application is active, and returns
when it gets an answer other than −1 (`0x005A0980` has 8 callers: confirmation boxes such as the Escape
one, and the "Allow all" prompts). **verified**. Screen `0x54` is set only there and restored on return, so
the window procedure's own screen test (`0x005C4AD6`) lets only mouse and focus messages through to the
default procedure while it runs. **verified**

## 4. Screens

`0x00514020` and `0x00514C10` switch on the same byte. The list below gives, for each value, the draw
function(s) of `0x00514020`; the input case of `0x00514C10` has the same value. "Role" is a hypothesis unless
a verified use is given.

| `0x00F2A6DD` | Draw function(s) | Role | Status |
|---|---|---|---|
| `0x00` | `0x00420240` | start-up / intro | hypothesis (set to `0x44` at the end of start-up, `0x005C37C0`) |
| `0x01`, `0x20`, `0x48` | `0x0051CAB0` then the interface chain of section 6.1 | the city (`0x01`), the city with another side panel (`0x20`, `0x48`: `0x00525C10` / `0x00525630` draw the panel) | verified (calls); roles: hypothesis |
| `0x05` | `0x0058EA20`, `0x004F4AC0` | **message window**: text of an event or of a help page, over the city (section 6.4) | verified (`0x004F2600` sets it, saves the previous screen in `0x00EF8CB0`) |
| `0x07`, `0x09` | `0x005904F0` + `0x00590920`/…; `0x005724E0` | god / sanctuary windows; **building information window** (`0x09`, section 6.3) | verified for `0x09` (`0x005724E0`); `0x07`: hypothesis |
| `0x14` | `0x00599DA0`, `0x00597E50` | the world map | verified (`world_map_screen`, rendering.md) |
| `0x23` | `0x004F6E40` | **tooltip** (section 6.6) | verified |
| `0x42`, `0x43` | menu objects `0x0052DA30`, `0x0052DA40`; text screen `0x0052C070` | objectives of the scenario (the goal list) | hypothesis (17 goal types, one text builder each) |
| `0x44` | (input only) | the state set at the end of start-up | verified (`0x005C37C0`) |
| `0x54` | none | modal loop of `0x005A0980` | verified |
| `0x56`, `0x58` | `0x0051CAB0`; `0x0051D830` object | two more city-like modes (`0x58` is also the case reached when `0x00665E2C != 0`) | hypothesis |
| `0x02`, `0x03`, `0x04`, `0x06`, `0x08`, `0x0A`…`0x13`, `0x15`…`0x1F`, `0x21`, `0x22`, `0x24`…`0x41`, `0x45`…`0x47`, `0x49`…`0x53`, `0x55`, `0x57` | per-case functions (`0x005BE860`, `0x00431210`, `0x0059AF60`, `0x0059B5E0`, `0x0059B330`, `0x0059BCD0`, `0x005BF8D0`, `0x005BF1E0`, `0x0059C060`, `0x00522620`, `0x0046BA10`, `0x00464C00`, `0x0059C440`, `0x0059BBE0`, `0x004670A0`, `0x00586140`, `0x004FDD50`, `0x00421C70`, `0x0059C810`, `0x004314C0`, …) | menus, load/save, options, victory and defeat, editor, adventure screens | verified (draw call per case); roles: hypothesis |

Several cases only call a menu object with a virtual method (`(*obj)->+0x10` or `+0x14`): screens `0x08`, `0x15`,
`0x1C`, `0x1F`, `0x21`, `0x22`, `0x29`, `0x2B`, `0x2C`, `0x2D`, `0x2E`, `0x35`, `0x3B`, `0x42`, `0x43`, `0x4C`, `0x57`,
`0x58`: their classes
have a vtable and are not read here. **verified** (the dispatch); their content is not described.

Menu screens are drawn **centred** with the offsets `0x00F2A6AC` = (width − 640) / 2 and `0x00F2A6A8` =
(height − 480) / 2 (rendering.md section 3.2) and always run in 800×600 (rendering.md, full-screen
backgrounds).

## 5. Widgets

### 5.1 The hit table (28 bytes per entry)

A menu screen keeps an array of entries of **28 bytes** (`0x1C`) and passes it to the three functions of the
input pass together with its origin `(ox, oy)` and its entry count. Layout read from `0x00432300`,
`0x004321E0`, `0x00431530` and `0x00513130`:

| Offset | Type | Meaning | Status |
|---|---|---|---|
| +0x00, +0x02 | s16, s16 | left and top edge, relative to the origin | verified |
| +0x04, +0x06 | s16, s16 | right and bottom edge (exclusive) | verified |
| +0x04 | u32 | image id, **in the other array use** (`0x00431530` draws a table that has no rectangle: left and top at +0/+2, image at +4) | verified |
| +0x08 | code | "left button released" action | verified (`0x004321E0` calls it) |
| +0x0C | code | "right button" action (press for type 1, release for type 3) | verified; "help for the widget": hypothesis |
| +0x10 | u8 | selects the image variant in `0x00431530` | verified |
| +0x11 | u8 | "pressed" flag: set before an action is called; non-zero asks for a repaint | verified |
| +0x13 | u8 | type: 1 (right press fires +0x0C), 3 (right release fires +0x0C), 4 and 5 (image variants in `0x00431530`) | verified |
| +0x14, +0x18 | u32, u32 | two arguments, copied to `0x00F135F8` and `0x00F135F4` before the action | verified |

- **Hit-test** `0x00432300(ox, oy, table, count)`: the first entry whose rectangle contains the mouse
  (`0x00F2A738`, `0x00F2A734`), left edge inclusive, right edge exclusive; returns its number + 1, or 0; stores
  the mouse position in `0x00F2A730`/`0x00F2A72C`. 12,610 / 15,972 / 12,779 calls. **verified**
- **Hover and layout** `0x00513180(ox, oy, table, count, hit)`: on the first call for a screen it copies the
  layout overrides of section 5.4 into the table (`0x00513130`); then returns `hit`. The caller keeps it in
  `0x00781844` (the hover index, read by the drawing of the buttons). 4,457 / 7,650 / 4,457 calls. **verified**
- **Click** `0x004321E0(ox, oy, table, count)`: for the first entry under the mouse: if the left button was
  pressed this turn (`0x00F2A707`), remembers the entry in `0x007818A0` and does nothing else; on a left
  release (`0x00F2A708`), calls +0x08 if the entry is the remembered one and returns its number + 1; otherwise
  on a right press (`0x00F2A6FB`, type 1) or a right release (`0x00F2A6FC`, type 3) sets +0x11, copies
  +0x14/+0x18 and calls +0x0C. **verified** (decompilation); 4,518 / 7,711 / 4,518 calls.

### 5.2 The draw table (40 bytes per entry)

The control panel and the sub-panels use a second kind of table, drawn by **`0x004316F0(ox, oy, table, count)`**
(6,208 / 24,710 / 12,438 calls), entries of **40 bytes** (`0x28`):

| Offset | Type | Meaning | Status |
|---|---|---|---|
| +0x00, +0x02 | s16, s16 | x and y relative to the origin | verified |
| +0x04 | s16 | height of a stretched widget (see +0x24) | verified |
| +0x0A | u16 | **packed group id** = collection × 512 + group rank + 1 (0 = none) | verified |
| +0x0C | s16 | image number added to the first image of that group | verified |
| +0x18 | u8 | state: 0 disabled (image + 3), 1 enabled, 2 not drawn | verified |
| +0x19 | u8 | selected: non-zero adds 2 to the image (the value `0x14` also sets the repaint flag `0x00F2A6CC`) | verified |
| +0x1A | u8 | hovered: adds 1 when not selected | verified |
| +0x1B | u8 | flash counter: drawn even without a repaint flag; its high 6 bits count down by 1 per call, the low 2 bits pick the image | verified |
| +0x24 | u8 | flag bit 0: draw as a vertical stack of 13-pixel pieces (first, middle +4, last +8 images) over the height at +0x04 | verified |

The image id is built as `bank × 16384 + first image of the group + [+0x0C]`, where the group is the entry
`rank − 1` of the bank's group table (rendering.md section 4); an out-of-range rank takes group 0. The same
decoding is `0x0040FD10(packed id)` (107 callers) and, for the fonts of bank 0, `0x0040FCF0(rank)` (101
callers). Drawing goes through `0x005ACF50` with the position in `0x00F13A40`/`0x00F13A3C` and the image in
`0x00F13A20`. A draw is skipped unless a repaint flag is set or the entry flashes. **verified**

### 5.3 Frames of windows

Windows and panels are drawn from **16 × 16-pixel tiles**: `0x0059EE90(x, y, columns, rows)` (3,888 / 7,999 /
4,003 calls) draws a frame with its title bar, `0x0059F2A0(x, y, columns, rows)` (9,368 / 16,910 / 12,159) a
plain frame and `0x0059F6E0(…)` a highlight (8,436 / 22,364 / 8,781). The tile numbers come from fixed
groups of bank 0 and `0x0040FD10`/`0x0040FCF0` ids (`0x83`, `0x84`, `0x284`, `0x285`: groups of the interface
bank) with the corner, edge and middle tiles chosen by row and column and the edge tiles cycling every 5 or
10 columns. **verified** (loops and ids); the group names are not read.

### 5.4 Layout overrides (`dialog.loc`)

At the first use `0x00512780` clears 16-byte records per screen (`0x00F9DF30…`), then `0x00513490` opens a file
named **`dialog.loc`** (string at `0x0063D850`) and, if it exists, reads for each screen three lists of overrides
that `0x00513130` writes into the hit tables (each override replaces the left, top, right and bottom of the
entry with that number). The GOG installation has no such file, so the lists stay empty and every widget keeps
its built-in position; the many lookups of `0x00512740` (15,454 / 113,287 / 19,123 calls), `0x005128E0`
(10,995 / 105,635 / 14,664) and `0x00512990` (7,797 / 88,458 / 11,466), made by the text drawing helpers
`0x00512BF0`, `0x005132A0`, `0x00512B50`, `0x00512FF0`, find nothing. The variable that would switch these
functions to an interactive layout-editor mode, `0x00F9F970`, is **never written** by the code (six reads, no
write): the editor is dead code. **verified** (accesses, absence of the file in `original/gog`), *hypothesis*
(purpose: localised dialog layouts).

## 6. Windows and panels

### 6.1 The city screen

Per turn on screens `0x01`, `0x20`, `0x48` with `0x00665E2C == 0` (not in the editor), after the map
(`0x0051CAB0`, 463 / 4,873 / 1,564 calls), `0x00514020` calls in this order **(verified, all of them called
once per city frame)**:

| # | Function | What it draws | Status |
|---|---|---|---|
| 1 | `0x0058EA20` | the side panel background: `0x00525630` (screen `0x48`), `0x00525C10` (`0x20`), `0x004F2880` (`0x05`: picks one of them from the previous screen), `0x00517BF0` otherwise | verified |
| 2 | `0x0058EA50(0, …)` | copies the current "map style" `0x00FA05F0` (a debug label of the original) to `0x010DFD80` unless it already equals it (a change trigger); the second argument is unused | verified |
| 3 | `0x0058F130` | texts of the top-left corner (section 6.5) | verified (calls) |
| 4 | `0x004542B0` | a number (`0x00DA23D4`) drawn at a map position (`0x00DA2FAC`/`0x00DA2FB0`, converted by `0x0056BF20`), twice: a shadow at +1,+1 and the colour text, with `0x00508F00` | verified (calls); what the number is: hypothesis |
| 5 | `0x0059C8A0` | debug overlay "Pop is", "tot cap", "docks list", "mouse x"…: drawn only when `0x00FE6A08 == 1` | verified (strings); an internal debug view |
| 6 | `0x0059D040` | debug overlay "Evolve info (F3)": only when `0x0091EC24 == 1` | verified |
| 7 | `0x0059D280` | debug overlay, 4,175 bytes | hypothesis (same family) |
| 8 | `0x0059E5A0` | not read (1,478 bytes) | – |
| 9 | `0x00521D30` | the **message queue**: when screen is a city, takes the next pending message (array at `0x00FD3690`, entries of 60 bytes at `0x00FD3844`), opens the message window with `0x004F2600` and sets a 5-turn delay (`0x00FE2314`) | verified |
| 10 | `0x005C5D60` | debug timers and the F2/F5/F6 keys when `0x00661C3C != 0`; draws the profiling overlay when `0x0160AC9C`/`0x0160ACA0` are set | verified |

### 6.2 The control panel

Position (`0x0058E280`, called when the mode is set): the panel's left edge **`0x010DFF34`** is 478 for a
640-wide screen, 598 for 800 and 838 for 1024, and `0x010DFF30` is 120 more (the right edge, *hypothesis*). For other widths (the wide modes) the function sets nothing; the host sets the same variables
(`docs/reverse-engineering.md`). **verified.**

The panel has **two levels**. `0x00FA0000` is the category (0 or 1; Tab toggles it, `0x005C54F6`), `0x00FA0004`
the selected page (2, 3, 5, 7, 8, 9, 10 have hit tests). Drawing (`0x00517BF0`, 466 / 3,664 / 1,689 calls, once
per city frame plus redraws): it fixes the image number of a few buttons from the game state
(`0x0063DD3A`, `0x0063DE88`, `0x0063E088`), gives the first 11 entries of the draw table at **`0x0063DC40`** (17
entries) their "enabled" byte from the availability bytes `0x00FEAE06 + n` (unless `0x00665DF0`, "Allow All", is set),
clears their "selected" byte, sets the "selected" byte of entry `0x00FA0004` and of entry 15 + `0x00FA0000`
(entries 15 and 16 are the two category tabs), and draws two draw tables with `0x004316F0` at `(panelX, 0x16)`: `0x0063DC40` (17 entries, the
category tabs and the main buttons) and `0x0063DF80` (7 entries, the page buttons). The "sidebar" variant of
screens `0x48` and `0x20` (`0x00525630`, `0x00525C10`) fills the tables at `0x006402A0` (6 entries) and
`0x00640390` (6 entries) with image numbers chosen from the selected building (`0x00DB7B44`, `0x00DB7BD1`, …),
then draws a 10×7 frame (`0x0059F2A0`) at `(panelX + 0xE, 0x168)` and the sub-panels `0x00524CA0`,
`0x005258D0`. **verified** (calls, addresses of the tables); the meaning of the pages: hypothesis.

Clicks: the city's input case calls first `0x00517940`, `0x00517DE0`, `0x00517580`, `0x005178E0` (each once
per city frame, 462 calls), then `0x00431C00` (hit-test of a draw table, 1,848 calls) and `0x004319C0` (its
click, 4,800 calls) on `0x0063DC40`, and `0x00517650` (462 calls), which for each page tests the page's own table
with `0x00432300`/`0x004321E0` or the two helpers `0x00431E10`/`0x00431C80`: page 2 `0x0063DF48` (2 entries),
3 `0x0063DA48` (18), 5 `0x0063DF10`, 7 `0x0063EE08` (15), 9 `0x0063EFB0` (6) or `0x0063F058` (15), 8 `0x0051B470`,
10 `0x005B7680`; the buttons are placed from `panelX` and `0x00F9FFF8`, the scroll of the list (`× 0x14` pixels).
**verified** (structure and addresses); which page is which: hypothesis.

### 6.3 The building information window

Screen `0x09`. `0x005715A0` (called when the window opens; 0 calls in these runs) sets its position and size in
the variables `0x00FFA17C` (left), `0x00FFA178` (top), `0x00FFA174` (width in 16-pixel cells, 0x1D by default)
and `0x00FFA170` (height in cells, chosen from a class of the window returned by `0x00571EE0`: 0x10, 0x12, 0x13, 0x0E,
0x18, otherwise 0x1F or 0x18). In 800×600 the top is `(0x25 − height) × 16` when the mouse is above y = 313, otherwise
`0x20`; in 1024×768 it is `0x2F8 − height × 16` when the mouse is above y = 397 (`docs/reverse-engineering.md`,
"above 768 lines"); in 640×480 it is `0x20`. The left edge is `0x44`, `0x80` or `0xF8` for heights 480, 600 and 768
when `0x010DFF60 == 1`, and 60 less otherwise. Some building kinds (`0x39`, `0x3A`, `0x3D`, `0x3E`, `0x7D`, `0x75`) override the
size. Drawing: `0x005724E0` (screen `0x09`), clicks: `0x005726E0` (2,131 bytes, 9 hit-tests). The building is
`0x00FFA158`, its kind the 16-bit value at `0x0092E668 + 0x118 × index`; the window kind `0x00FFA13C` is 1
or 2 (the branch of `0x005724E0` for kind 2 reads the building; kind 1: *hypothesis*, a figure or an event).
**verified** (positions, sizes, dispatch); the contents of each kind are not read.

### 6.4 The message window

Screen `0x05`. `0x004F2600(message, extra, flag)` is the one entry point (3 callers, including the message
queue `0x00521D30`; 0 / 2 / 0 calls in the three runs): it clears two 200-word tables (`0x00EF860C`, `0x00EF892C`,
not read), stores the record number in `0x00EF8C4C`, resets the scroll position `0x00EF8C58` and the line count
`0x00EF8C60`, prepares the link table with `0x004F52D0`, saves the **previous screen** in `0x00EF8CB0`, sets the
screen to 5 and calls `0x005A0A70`. The text is the body of a record of `Zeus_MM.eng` (`formats/eng.md`; in memory
the 80-byte records start at `0x00F13CB0`, so the body offset is the word at `0x00F13CFC + 0x50 × n`, the text base is
`0x00F29F68`). Drawing of screen 5: `0x004F4AC0` after the side-panel background; background repaint `0x004F2830`;
the page itself is drawn by `0x004F2D80` (5,467 bytes, the help-page code of `formats/eng.md`, which calls
`0x004F5300` five times); `0x004F4380` (1,796 bytes) and `0x004F6BA0`/`0x004F6AF0` are the other functions that set the
fonts for this text (input and pop-up form: hypothesis). The message record's two unread words (+0x38, +0x3C of
`formats/eng.md`) are not zero in use: `0x00521D30` reads the one at +0x3C as the offset of a **sound file name**
(`0x0050A800` tests it; sounds 6 or 7 play when the name is absent, 7 when the flag at +0x38 is 1), so they are the
voice-over name and flag of a message (*hypothesis* for "voice-over"; the stores of the shipped files are zero).
**verified** (entry point, fields, calls); layout of the window: *hypothesis*.

### 6.5 The top bar

`0x0058F130` (1,244 bytes) draws, in the top-left corner of the city: while `0x00FEAF28` is non-zero, the string
(group 6, rank 5) at (8, 0x2F) in the first font (group 0) and again at (7, 0x2E) in
`0x7F9A` (`0xFF3A` for 565): a shadow and the text; one or two lines of figures built with `0x005132A0`
(`0x005E8FD0`, a number format) when `0x0052E230` or `0x0052E240` say so; the literals "Allow All"
(`0x00665DF0 == 1`), "Happiness" (`0x010DFF58`) and the event line "Event / Major Good / Minor Good / Major Bad /
Minor Bad / Level" (`0x00665DE4`, `0x00665DE8`, `0x00665DEC`) — **English literals of the executable, not file
strings: the developers' debug displays**; and, when the pause flag `0x00F2A768` is set, a 3-cell-high frame
(`0x00512FF0`) at y = 0x28 and a text centred in 448 pixels at y = 0x3A (group 13, rank 2). **verified**
(addresses, order, literals); what the lookups say: *hypothesis*.

### 6.6 The tooltip

Screen `0x23`, drawn by `0x004F6E40` (461 bytes; 3 callers), after the city when a text pointer `0x00EF7F70` is
non-empty. It lays the text out **twice** with `0x004F5300`: first with mode 1 (count the lines in
`0x00EF8C60`) at a width of 190 pixels (`0xBE`), and a second time at 270 (`0x10E`) when more than two lines
came out; the box height is `lines × 16 + 10`. The box is placed 20 pixels right of the mouse when the mouse is left of `width + 20`, and left of it
otherwise, and 50 pixels below it (72 above when the mouse is at y ≥ 200), drawn as
two nested rectangles (`0x00509870`, `0x00509B20`, colours `0x84`/`0x9B2` or `0x104`/`0x1372` for 565), then the
text with mode 0. It uses the panel font group at +0x68 of the group table (rank 13) if the table has at least 13
groups. **verified**

## 7. Texts

### 7.1 The strings

The text file `Zeus_Text.eng` is loaded once at start-up by `0x0050ADA0` (`formats/eng.md`): the group table sits at
`0x00F27F30` (8 bytes per group, offset in the first word, **count not read**), the text area base pointer is
`0x00F29F64`. **`0x00507BF0(group, index)`** (73 callers; 64,286 / 235,689 / 82,712 calls) returns the address of
string *index* of group *group*: it starts at the group's offset and, for each string to skip, walks to a zero byte
that follows a printable byte or another zero, then steps over control bytes. `0x00508060` and `0x00508080` (44 and
11 callers) store the result in `0x00F13A60` / `0x00F13A5C`. A string reaches the screen as
`(group, index)` through one of the helpers of section 7.4. **verified.**

### 7.2 Fonts

All fonts live in `Zeus_Fonts.sg3` (bank 0). It has **17 groups of 134 glyph images** (the first has 134, e.g.
`SPR_FONTS = 201…`, found with `sg3.py info`), in this order: 0 `SPR_FONTS`, 1 `_11C`, 2 `_11C4`, 3 `_11C2`, 4 `_11C3`,
5 `_11CB`, 6 `_22C`, 7 `_22C2`, 8 `_9B`, 9 `_11LTYEL`, 10 `_9LTBK`, 11 `_9LTBU`, 12 `_9LTYEL`, 13 `_9DKBU`, 14 `_9DKYEL`,
15 `_11LTBLUE`, 16 `_DKMESSAGE`. A **font** is passed around as the *image id of the first glyph of its group*, read as
`group table[rank] + 4` (the table `0x012AFBEC`, 8 bytes per group; the last rank is at `0x012BC820`, and many
callers fall back to group 0 when the table is too short). Line heights per group (from `language.cpp`):
10, 16, 16, 16, 16, 16, 29, 29, 12, 16, 12, 11, 11, 11, 11, 16, 10. **verified** (table, order; heights are the
values the language pack code already relies on).

The text renderer `0x00508800` derives from the font its **space width** and **line height** (`0x00F13960`):
group 6 → 8 / 0x17, group 7 → 8 / 0x18 (when the table has at least 7 / 8 groups), group 0 → 4 / 9 with one extra
pixel after each glyph, group 16 → 4 / 7, any other group → 6 / 0x0B. `0x00507DA0` (width of a string, used to centre)
applies a **different** rule: space 10 and one extra pixel for groups 6 and 7, space 4 and one extra pixel for
group 0, space 6 and none for every other group (group 16 included), so centring and drawing disagree by a
few pixels for the two large fonts: a rewrite must keep both. **verified.**

### 7.3 The glyph table and the width of a character

**Glyph table `0x0063BE0C`** (256 bytes, indexed by the character code; `0x0063BE2C` is the same table seen from
code `0x20`): the glyph number (1 to 134) of each character, 0 when there is none. Read from the executable: `a`…`z` →
1…26, `A`…`Z` → 27…52, `1`…`9` → 53…61, `0` → 62, the ASCII punctuation → 63…80 (`!` 63, `-` 69, `.` 79; space,
`#`, `$`, `&`, `@` and brackets have 0), and the upper half (`0x80…0xFF`) → 80…134 in the order of the Windows-1250
code page of this edition (the French edition's table differs; the language pack replaces it, `language.cpp`). The
first 32 bytes of the table overlap an unrelated file-name string and are not glyph numbers: every reader tests
for `ch < 0x20` before. **verified.**

**`0x00507F20(character, font)`** (the width at `0x00507F20`): 0 for NUL, **4 for a space**, otherwise the glyph
number `g` from the table (0 → width 0), then the image `font + g − 1` of the sprite banks (rendering.md section 4:
bank = id / 16384, number = id % 16384, an out-of-range number reads entry 0), and the result is its **width field
(+0x14) + 1**. 9,983,090 / 8,729,552 / 8,436,952 calls: the most-called function of the game after the sprite
dispatcher. **verified.**

`0x00507F20` has two callers, `0x004F57A0` and `0x005091B0` (the latter is called by `0x005090A0` and `0x005086E0`);
the width does not depend on the neighbouring characters (no kerning). **verified.**

### 7.4 Rendering a string

| Function | Role | Calls (`working-city` / `options` / `tutorial`) |
|---|---|---|
| `0x00508800(text, x, y, font, colour, flag)` | **the string renderer**, 37 callers: walks the text, `_` is drawn as a space, `#` takes the following characters from the text prepared in `0x00F27630` when `0x00F13A78 == 1` (and is a space otherwise), bytes below `0x20` are skipped; draws each glyph with `0x00508B50`; style variables select an outline or a shadow (`0x00F13970` none, `0x00F13974`, `0x00F13978`) by drawing the glyph first in colour 0, shifted by one pixel, once or twice; keeps the cursor in `0x00F13A70`; counts characters in `0x00F13A7C` for the text cursor | 83,092 / 285,044 / 115,802 |
| `0x00508B50(font, glyph, x, y)` | **one glyph** of a line text: the glyph is lifted by `max(0, height − line height)` (the line height of 7.2) when the character is 0x80 or more (the upper half, accented), by 0 for the value `character − 0x20 = 0xC7`, and by 2 for a list of 18 such values (the Polish letters of the GOG edition, exceptions at `0x00508C7F`); drawn 3 pixels up; returns the glyph width (without the +1) | 1,006,238 / 2,486,184 / 1,278,269 |
| `0x00508670(text, x, y, width, font, colour)` | the same, **centred** in `width` (via `0x00507DA0`) | 63,831 / 121,174 / 83,700 |
| `0x00507DA0(text, font)` | width of a string, up to 10,000 bytes | 76,861 / 138,021 / 96,963 |
| `0x005090A0(text, x, y, width, font, colour)` | **word-wrapped paragraph**: fills a 200-byte line buffer `0x00F27694` word by word with `0x005091B0` until `width` is reached and draws it with `0x00508800`, line by line, with a line step of `5 + line height` | 466 / 3,665 / 1,689 |
| `0x005091B0` | width of the next word of a line (the line breaker's measure) | 11,184 / 87,937 / 36,972 |
| `0x00508D90(group, rank, x, y, font, colour)` | string `(group, rank)` drawn at `(x, y)` | 3,934 / 106,448 / 10,049 |
| `0x00508D40(group, rank, x, y, width, font, colour)` | string `(group, rank)`, centred in `width` | 36,265 / 64,295 / 41,848 |
| `0x00512BF0` / `0x00512B50` | the same two, through the layout override of section 5.4 (47 / 69 callers) | 2,066 / 43k / 3k ; 3,198 / 18k / 3k |
| `0x00508F00(value, sign slot, text, x, y, font, colour)` | a **number** followed by a text (`0x00508540` writes the digits into `0x00F27530`, 2,801 / 44,286 / 11,594; the second argument, always `0x40` in the calls read, overwrites the sign slot) | 1,401 / 36,524 / 5,106 |
| `0x005132A0` | the same through the layout override (27 callers) | 934 / 26,080 / 3,380 |
| `0x00507FC0(text)` | reads a (possibly negative) decimal number from the text with the powers of ten at `0x0063BF1C` | 10,762 / 30,120 / 29,922 |

The glyph drawing ends in `0x005ACF50` (rendering.md section 8) via the clip helpers `0x00509C20`/`0x00509CD0`
(493,617 / 1,969,903 / 718,757 calls, **already native** inside the dispatcher's fast path). **verified** for
every row (decompilation and counts).

### 7.5 The panel texts (`0x004F5300`)

Used for the longer texts: the objectives screen (`0x0052C070`, 2,279 of the 2,280 calls of `working-city`), the
message window (`0x004F2D80`), the tooltip and the pop-up form of the message (the 9 callers of `0x004F5300`
are `0x004F2D80`, `0x004F6E40`, `0x004F6BA0`, `0x0052C070`, `0x0051C800`, `0x004F4380`, `0x004291A0`, `0x00428DA0`,
`0x004248F0`, plus two call sites outside any function Ghidra knows; the roles of the last five are not read). Fonts are set by the caller in `0x00EF8C9C` (normal) and `0x00EF8C98` (link label).

`0x004F5300(text, x, y, width, mode)` (1,169 bytes; 2,280 / 3,906 / 2,395 calls):

- **Layout**: line by line (at most 999 lines, `local_8`), it clears a 200-byte line buffer `0x00F27694`, and adds the next
  word while the running width stays below `width`: the width of the word comes from **`0x004F57A0`** and the
  number of bytes it took from `0x00F13A58`. A word is a run of printable characters; the space that ends it
  is not consumed. Leading spaces of a line are dropped. Control codes (the `@` codes, section 7.6) end a line.
- **Modes**: with `mode == 0` the line is drawn only if its number is inside the window
  [`0x00EF8C58`, `0x00EF8C58` + `0x00EF8C64`) (scroll offset and visible lines), so a whole text is laid out each time and only
  its visible part drawn; with `mode == 1` nothing is drawn and `0x00EF8C60` receives the number of lines (the
  tooltip's measurement pass); other values behave like 1 for drawing but do not count lines. Each line is
  drawn by `0x004F58C0`. Lines are 16 pixels apart (`param_3 += 0x10`).
- **A first, different font for links**: while a link is open (7.6) the characters use `0x00EF8C98` instead of
  `0x00EF8C9C`; when the normal font is the font of group 0 every drawn glyph advances 2 more pixels (`0x004F58C0`) and
  a second pass redraws the line one pixel up and left in colour `0x7F9A` (`0xFF3A` in 565). **verified**
- **`0x004F58C0(line, x, y, mode)`** (34,315 / 37,789 / 36,036 calls): draws a line: the characters below
  `0x20` are skipped, characters with no glyph advance 6, the others are drawn by **`0x004F59B0(font, glyph, x, y, mode)`**
  (1,145,742 / 905,189 / 888,884 calls) which does as `0x00508B50` (7.4) with 11 as the line height (drawn at the lifted position, not 3 pixels higher), draws nothing
  when `mode != 0`, and returns the glyph width. On `@digits` it registers a **link box** with `0x004F5260`
  (up to 50 per text, stored as five integers each from `0x00EF8220`: left − 2, right + 2, top − 1,
  bottom + 13, target record) and switches to the link font for the label. **verified** (the link boxes; the #35
  report called the digits a colour change, the decompilation shows a link, in agreement with
  `formats/eng.md`).
- **`0x004F57A0(text)`** (2,200,200 / 1,776,374 / 1,772,300 calls): the width of the next word. A space before the
  word counts 4 pixels; after the word it ends it. At a `@` before the word: `@P` and `@L` give 0 and consume 2 bytes,
  `@G` consumes `@G` and its digits and gives 0, `@digits` consumes `@` and the digits and measures the label. Inside a
  word the `@` is ordinary. At most 1,999 bytes. **verified.**

Why 2.2 million word measures for 2,280 layouts: each layout measures every word of a text of several hundred
words, even when only a few lines are visible, and the objectives screen lays out about five texts per
turn. **verified** (counts, structure).

### 7.6 The `@` codes

As in `formats/eng.md`, with the decoding that produces them. `@P` and `@L` end the line (`@P` also sets the
indent flag of the next line to 50 pixels, `0x32`). `@G` and digits: an **inline picture**: the digits are read by
`0x00507FC0`; the image id is `0x7FFF + n + first image of a group of bank 2` (group 0 of `0x012C9474`, or the group 9
when the bank's last rank `0x012D60A8` exceeds 8), i.e. the *n*-th image of that group; it is drawn centred
horizontally in a box of `0x00EF8C74` sixteen-pixel cells and reserves `height / 16` lines (the image entry's +0x16). `@digits` alone: a link to the record with that number (the link boxes above). `_` in a
string and `#`: handled by `0x00508800` (7.4) and not by `0x004F5300`. **verified** for `@P`, `@L`, `@G` and
`@digits` in `0x004F5300`/`0x004F58C0`/`0x004F57A0`; the group of the `@G` images: *hypothesis* (arithmetic read in
`0x004F5495`, the bank name not looked up).

## 8. Per-screen drawing helpers

Used by the screens of section 4 and called from many places:

| Function | Role | Status |
|---|---|---|
| `0x00509B20(x, y, w, h, colour)` / `0x00509870` / `0x00509B90` | filled and outlined rectangles (`0x00509B90` draws point by point with `0x00413CD0`, 1 caller: the top-bar band) | verified (calls) |
| `0x0058EB20`, `0x00517CD0` (8,401 calls: once per turn) | the two functions called after every redraw of `0x0058E2E0`: the buttons of the bottom strip and the panel flash update | hypothesis |
| `0x0058E2E0` (3 / 779 / 125 calls) | **redraw of the whole frame**: fills the 9-pixel strip at the bottom (`0x00509B20`), sets `0x00F2A6CD` = 1, calls the panel background (`0x00517B10`, `0x00525280`, `0x004F2850`), `0x0058EB20`, `0x00517CD0` | verified |
| `0x005C4A30(mode)` | changes the display mode (window / full screen) and redoes the layout, except on screens `0x05`, `0x0E`, `0x0F`, `0x21` | verified |
| `0x005233A0` | recomputes the 6 button x positions of the top bar from the screen width ((width − 180) / 2) | verified |

## 9. What the equivalence bench can and cannot see

The text and widget code writes the frame buffer only through `0x005ACF50` and `0x00509B20` family functions, so the
**bench of eight scenarios** (frame logs) sees every difference in a glyph position, a width or a link box that
changes a pixel. It does not see: the link boxes themselves (kept in memory, used by clicks), the text cursor
(`0x00F13A7C`, `0x00F13A70`…), the numeric results of `0x00507F20`/`0x004F57A0` when a text is never drawn, and the
mouse edges. A rewrite must be verified with `--verify ADDRESS` (`docs/equivalence.md`) on top of the frame logs, and
the state fingerprint should include the link table `0x00EF8218…0x00EF8330`, `0x00EF8C60` and `0x00F13A58`/`0x00F13A64`/
`0x00F13A70`/`0x00F13A6C`, which the layout functions leave behind. *Hypothesis*: the globals just named are the whole
state that leaves `0x004F5300` and `0x00508800`; no other write was searched.

## 10. Cost in real time

The #35 branch (section 11) shows that the control panel texts cost under 2 points of one core in real play, because
they are laid out about 4.5 times a second. The same text functions are far heavier in the deterministic runs above,
which turn the main loop about 18 times per simulated second. So: the text pipeline is the biggest *emulated*
hot spot of the interface, but not a big real-time one. Its value for the project is **fidelity and documentation**
(the equivalence bench, the language packs) more than CPU time. The heavier real-time consumer to watch is the
objectives screen, which lays out about five texts per turn (`0x0052C070`) and is on screen for long stretches.

## 11. Starting point: the native attempt of #35 (closed, no gain)

Branch `textes-panneau-natif` (local, not merged; two commits on `3173f7f`) replaced, with fast paths:

| Original | What the native code did | Status of the result |
|---|---|---|
| `0x00507F20`, `0x004F57A0`, `0x00507BF0` | replaced whole | 4 / 4 scenarios identical |
| `0x004F59B0` | draws through the native sprite dispatcher with the language-pack line height; falls back to the original when the dispatcher does | identical |
| `0x004F536C`, `0x004F53CC` inside `0x004F5300` | clears the line (`REP STOSD`, which cost one emulated block per word) and fills a line with the words that fit; `@P`, `@L`, `@G` stay with the original | identical |

Fidelity: the eight scenarios gave identical frame logs; with the French pack, four scenarios identical to the unmodified
build. Measurements: 83.6 M / 70.8 M / 59.6 M / 86.0 M / 35.5 M emulated blocks in the five functions fell to 2.2 M /
7.1 M / 0 / 0 / 0; CPU time of a small city on the fixed clock fell from 69.2 s to 60.1 s; **in real time** the large
city stayed at 66 % of a core (65.9 % after), the Population panel at 30 % → 23 % in one pair. Noise from other jobs
was larger than the gain. The same mistake to avoid: a fixed-clock profile overweights per-frame work against per-tick
work; rank candidates with real-time counts.

The attempt leaves a ready list of **invariants**: the word-copy loop's resume point `0x004F5427`; the `@` codes stay
with the original in the native version (a full rewrite handles them: section 7.6); `0x004F59B0` returns the glyph
width without the +1. It also left one unexplained real-time run (no tick counted); the cause was not found.

## 12. Ordered list of the functions to rewrite

Each step only needs the steps before it; within a step the most frequent function comes first. Counts are those of
section 1 (`working-city` / `options` / `tutorial`). "Native" = already replaced by the host.

| # | Function | Role | Calls | Needs | Status |
|---|---|---|---|---|---|
| 1 | `0x00507F20` | width of a character | 9.98 M / 8.73 M / 8.44 M | glyph table, image entries | original (native attempt closed) |
| 2 | `0x004F57A0` | width of a word | 2.20 M / 1.78 M / 1.77 M | 1 | original |
| 3 | `0x004F59B0`, `0x00508B50` | one glyph (panel text / line text) | 1.15 M + 1.01 M / 0.91 M + 2.49 M / 0.89 M + 1.28 M | the dispatcher `0x005ACF50` (native fast path) | original |
| 4 | `0x005091B0` | width of the next word (line breaker) | 11 k / 88 k / 37 k | 1 | original |
| 5 | `0x004F58C0`, `0x004F5260` | draw a line, register a link box | 34 k / 38 k / 36 k ; 0 / 198 / 0 | 2, 3 | original |
| 6 | `0x004F5300` | layout of a text (modes 0 and 1, `@` codes) | 2,280 / 3,906 / 2,395 | 2, 3, 5 | original |
| 7 | `0x00507BF0` | find string (group, index) | 64 k / 236 k / 83 k | text file loader `0x0050ADA0` | original |
| 8 | `0x00508800` | string renderer (styles, `_`, `#`) | 83 k / 285 k / 116 k | 3 | original |
| 9 | `0x00507DA0`, `0x00508670` | string width; centred string | 77 k + 64 k / 138 k + 121 k / 97 k + 84 k | 1 | original |
| 10 | `0x00508D90`, `0x00508D40`, `0x00508060`, `0x00508080` | string by (group, rank) | 4 k + 36 k / 106 k + 64 k / 10 k + 42 k | 7, 8, 9 | original |
| 11 | `0x00508540`, `0x00507FC0`, `0x00508F00` | numbers: write digits, parse, number + text | 2.8 k + 11 k + 1.4 k / 44 k + 30 k + 37 k / 12 k + 30 k + 5 k | 8 | original |
| 12 | `0x005090A0` | word-wrapped paragraph | 466 / 3,665 / 1,689 | 4, 8 | original |
| 13 | `0x00432300`, `0x004321E0`, `0x00513180` | hit-test, click, hover of the hit table | 12.6 k, 4.5 k, 4.5 k / 16 k, 7.7 k, 7.7 k / 12.8 k, 4.5 k, 4.5 k | mouse state | original |
| 14 | `0x004316F0`, `0x00431530` | draw table, image table | 6.2 k, 0 / 24.7 k, 2.6 k / 12.4 k, 0 | dispatcher | original |
| 15 | `0x0059F2A0`, `0x0059EE90`, `0x0059F6E0` | frames and highlights from 16-pixel tiles | 9.4 k, 3.9 k, 8.4 k / 16.9 k, 8.0 k, 22.4 k / 12.2 k, 4.0 k, 8.8 k | dispatcher | original |
| 16 | `0x00512740`, `0x005128E0`, `0x00512990`, `0x00512BF0`, `0x005132A0`, `0x00512B50`, `0x00512FF0` | layout-override lookups and helpers; dead without `dialog.loc` | 15 k, 11 k, 7.8 k, … / 113 k, 106 k, 88 k, … | 8, 13 | original |
| 17 | `0x0050B470`, `0x0050B450` | per-turn mouse edges | 8,398 / 18,513 / 9,614 | window state | original |
| 18 | `0x005C4A90` and its key tables | recording of mouse and keys | 95 / 122 / 50 | 17 | original |
| 19 | `0x00514C10` | input pass: screen switch, clicks | 8,398 / 18,513 / 9,614 | 13, 14, 17 | original |
| 20 | `0x00517BF0`, `0x00525630`, `0x00525C10`, `0x00517650`, `0x00517940`, `0x00517DE0`, `0x00517580`, `0x005178E0` | control panel: draw and clicks | 466, 462 each | 14, 15 | original |
| 21 | `0x0058F130`, `0x004542B0`, `0x00521D30`, `0x005724E0`, `0x005726E0`, `0x004F2D80`, `0x004F4380`, `0x004F6E40`, `0x004F6BA0`, `0x0052C070` | city top bar, bubble, message queue, windows, tooltip, objectives | 463 each; 0 / 2 / 0; 0 / 0 / 0; 2,279 | 6, 8, 20 | original |
| 22 | `0x00514020`, `0x00513580` | draw and turn dispatcher | 8,398 / 18,513 / 9,614 | all | original |

Remarks. (a) Steps 1 to 6 are the text pipeline; once they are done, the sprite dispatcher is the only function
left that touches pixels. (b) Steps 13 to 17 depend on the *exact* edge semantics of section 3.1 (one turn,
reset at the next poll). (c) Step 16 can be dropped rather than rewritten only if `dialog.loc` support is declared
out of scope (it is absent from the GOG data, but the language packs of other editions are not examined for it).
(d) The debug overlays (`0x0059C8A0`, `0x0059D040`, `0x0059D280`, `0x005C5D60`) run every city frame but test a flag
first and draw nothing in normal play; they need only their flag test.

## 13. Open questions

- The meaning of each screen number not listed with a role in section 4, and the classes behind the menu objects
  (`0x00423250`, `0x00424AF0`, `0x00426B00`, `0x0041E5A0`, `0x0051D830`, `0x0052DA30`, `0x005A8B30`, `0x005BB860`, `0x005C1A50`, `0x005C2A40`).
- The handlers behind the key calls (`0x00407600`, `0x00433AB0`, `0x00402430`, `0x005BF750`, `0x005BF880`, `0x00408930` roles).
- The contents of each building-information window kind and of each panel page; the 16-tile frames' group names.
- The third parameter of `0x00508800` (a flag) and the write of `0x00F13970`, `0x00F13974`, `0x00F13978` (the style variables).
- Whether `dialog.loc` exists in any edition (the three lists it fills, `0x00513490`).
- The `@G` image base (`0x012C9474`, group of bank 2) and the 14-byte `0x00EF8220` link table's reader (the click on a link).
- The text-field machinery (`0x00508380`, `0x005080A0`, `0x00508440`, `0x00F13AE4`/`0x00F13B98` cursor and length per
  field, `0x00F13C88` the active field).
