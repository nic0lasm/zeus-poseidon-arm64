# Terrain

How `Zeus.exe` (GOG edition, SHA-256 `382bcd28…75e90a57`) keeps the map: the per-tile grids
(terrain flags, altitude, water, rocks, trees, regrowth, desirability…), where they live in
memory and in the saved games, the functions that read and write them, and the rules that run
on them: desirability, the regrowth of trees, reeds and meadows, and whether a tile can be built
on. The goal is a description precise enough to rewrite the subsystem by hand without reading
the machine code again.

Every claim carries its evidence (an address, an offset, a measurement) and a status:

- **verified**: read in the code (decompilation, checked against the listing where widths
  matter) and, where noted, confirmed by game data or a measurement;
- **hypothesis**: inferred from names, data or context, not proven.

Addresses are those of the GOG executable. `FUN_xxxxxxxx` names are Ghidra's; our names are in
`analysis/symbols.tsv`. Block ranks refer to [`formats/sav.md`](../../formats/sav.md) (saved
games) and [`formats/map.md`](../../formats/map.md) (maps). Group names such as `SPR_TREES` are
those of `Zeus_Terrain.sg3`, found from the group rank the code passes (`formats/sg3.md`,
`tools/annotate_groups.py`).

## 1. Measurements used in this page

Two scenarios of the bench, original code alone (`--no-native`), `zeus-run --headless
--deterministic`, `--count` on every function named here:

- **`working-city`** (the last tutorial's large city, 629 buildings), 300 virtual seconds:
  **409 simulation ticks** (`0x00513A30`), 8 full turns of the 51-step cycle of section 6;
- **`poseidon`** (first Poseidon adventure: empty land, a road dragged, housing plots placed,
  the build cursor on screen for most of the run), 190 virtual seconds: **1,227 ticks**.

The deterministic clock does not run at wall-clock speed, so counts are given per run and per
tick. In real time the game runs about **45 ticks per second** (`docs/engine/rendering.md`,
section 12). Measurement logs are not kept in the repository; the commands are those of the
scenario files in `tests/scenarios/`, with `--no-native --count ADDRESS,…`.

## 2. Coordinates

| Item | Value | Evidence | Status |
|---|---|---|---|
| Grid | 228 × 228 tiles (51,984), row-major, one row = `0xE4` tiles | every loop below steps rows by `0xE4` | verified |
| Tile index | `[0x00DA2F80] + 228 × y + x` | `0x004A4C60`, `0x004A0DC0`, `0x004A8E10`; `formats/sav.md` (building +0x0C) | verified |
| Offset `0x00DA2F80` | not saved, recomputed at load (10,076 = 44 × 229 in the first tutorial) | `formats/sav.md` | verified |
| Map size | width `0x00DA2F78`, height `0x00DA2F7C`; `0x00DA2F84` = 228 − width (row stride correction) | clamps in `0x004A8E10`, `0x004AA190`; stride `0x00DA2F84 + width − x2 − 1 + x1` | verified |
| Rows of the playable area | for row r < `[0x00DA2F7C]`: first tile index `0x00DA24B0[r]`, columns `0x00DA2BD0[r]` ≤ c < `0x00DA2840[r]` | whole-map loops of `0x004A7D30`, `0x0045ED20`, `0x00493C80` | verified |
| Playable diamond | `0x00496010(x, y)` (absolute coordinates, i.e. plus `0x00DA2F90`/`0x00DA2F94`) is true inside the diamond bounded by `0x00DA2F90`, `0x00DA2F98`, `0x00DA2F9C`, `0x00DA2FA0`, `0x00DA2FA4` (four half-plane tests on x + y and x − y) | `0x00496010` | verified |
| Near the edge | `0x00496080(x, y, m)` is true inside the diamond but less than m tiles from its edge | `0x00496080` | verified |
| Neighbours | N −228, NE −227, E +1, SE +229, S +228, SW +227, W −1, NW −229 | `formats/map.md`; `0x0049CAE0`, `0x004A2510` | verified |
| View orientation | `0x00DA2F88` ∈ {0, 2, 4, 6}, not saved | `0x0045ED20` | verified |

Only half of the grid is used: the playable area is a rectangle on screen, hence a diamond in the
grid (`formats/map.md`).

## 3. The grids

One element per tile unless stated. "Writes" lists the functions found writing the grid directly
in the listing (`MOV`, `OR`, `AND` to the grid) or, when noted, through a block copy; whole-grid
clears (`0x004C7710` area, `0x00511E30`) are not repeated. All addresses are **verified** against
the block tables of the executable (`formats/sav.md`, `formats/map.md`); the "Content" column has
its own status.

| Grid | Address | Type | Save rank | Map rank | Content | Main writers | Status of content |
|---|---|---|---|---|---|---|---|
| terrain flags | `0x00CCE128` | u32 | 17 | 6 | bit field, section 4 | 62 functions; `0x0045ED20`, `0x004A8E10`, `0x0049F950` cited here | verified for the bits marked so |
| ground image | `0x00D4CFC8` | u32 | — | 4 | image id of the tile (`collection × 0x4000 + image`); not saved, rebuilt at load | `0x004A81F0`, `0x0051F450` family, `0x004AA190`, `0x0049F950` | verified |
| draw flags | `0x00D404B8` | u8 | 15 | 5 | `0x40`: anchor tile of what stands on it | `0x0051F450`, `0x0049F950` | verified (`rendering.md` 10.1) |
| piece flags | `0x00D00D68` | u8 | 20 | 7 | low 4 bits: size − 1 of a multi-tile piece; `0x10` replacement ground; `0x20` read by the default ground (`0x0051FBA0`); `0x80` with terrain `0x2`: desirability source (section 7.3) | `0x0049F950`, `0x0051F450`, `0x00493C80` (clears `0x80` when the tile lost terrain `0x2`) | verified (bits named); meaning of `0x20` hypothesis |
| random | `0x00CA7FF8` | u8 | 22 | 8 | random byte per tile: `& 7` picks the variant of a ground image; `& 0xF0` non-zero selects the larger plain-ground pieces (`0x0051FBA0`); copied into buildings (`formats/sav.md`) | `0x005876F0`, `0x00496330` | verified (uses) |
| altitude | `0x008E01FA` | s8 | 111 | 23 (2.1 maps) | height of the tile, 0 to 6, 30 pixels per step | 24 functions, among them the build click `0x00404CD0` (editor tools: hypothesis) | verified (`rendering.md` 10.1, `formats/map.md`) |
| projected altitude | `0x008ECD0A` | s8 | 112 | 24 (2.1 maps) | altitude of what projects onto the tile on screen; derived, depends on the orientation | `0x0045ED20` only (clears and rebuilds it) | verified |
| building | `0x00D26E98` | s16 | 16 | — | number of the building on the tile | building creation | verified (`formats/sav.md`) |
| first walker | `0x00D0D878` | s16 | 19 | — | head of the list of walkers on the tile | walker moves | verified (`formats/sav.md`) |
| desirability | `0x00C9B4E8` | s8 | 23 | — | −100 … 100, recomputed from scratch every cycle (section 7) | `0x00493830` (clear), `0x004A4C60`, `0x00493700` | verified |
| regrowth | `0x00C1C648` | u8 | 102 | — | growth of the resource on the tile, 255 = grown (section 8) | `0x004A7D30`, `0x004F8770`; set to 255 everywhere by `0x00511E30` | verified |
| tree depth | `0x00C29158` | u8 | 109 | — | for tiles with terrain `0x80`: ring distance to the nearest tile outside the forest (section 8.4) | `0x004A8E10` | verified (computation); hypothesis (name) |
| meadow value | `0x00C35C68` | u8 | 99 | 15 | for meadow tiles (terrain `0x800`): value computed by `0x004A4A50(x, y, 4)`, which also picks the `SPR_MEADOW` image; 0 elsewhere | `0x004AA190` | verified (computation); meaning hypothesis (fertility) |
| `grid_cc1618` | `0x00CC1618` | u8 | 18 | — | written by `0x0041B6E0`, `0x00419C90`, `0x0041AFB0`…; zero in every sample save | 11 functions | unknown |
| copy of `grid_cc1618` | `0x00B90C98` | u8 | 26 | — | block copy of `0x00CC1618` (`0x00413AF0`, 51,984 bytes) before dragging walls; never written otherwise | `0x0049AF50` | verified (copy); hypothesis (undo of a drag) |
| `grid_b84188` | `0x00B84188` | u8 | 27 | — | read by `0x004A6CC0` only; no direct write | — | unknown |
| `grid_c8e9d8` | `0x00C8E9D8` | u8 | 24 | 9 | read by `0x00454550`, `0x00496120`, `0x0049CF80`, `0x0049CFE0`; no direct write | — | unknown |
| `grid_c753b8` | `0x00C753B8` | u16 | 25 | — | written by `0x0041B6E0`, `0x00492830`, `0x0049CC10`, `0x004A07C0`, `0x004A09F0` | | unknown |
| `grid_c688a8` | `0x00C688A8` | u8 | 108 | — | written by `0x004A07C0`, `0x004A09F0` | | unknown |
| `grid_cb4b08` | `0x00CB4B08` | s8 | 21 | — | non-zero under some buildings; with tile flags `0x44`/`0x48` it changes how walkers are drawn | 14 functions | partly verified (`rendering.md` 11.2) |
| `grid_b1e8f8` | `0x00B1E8F8` | u32 | 105 | — | sixteen 2-bit fields per tile; `0x004A3100(i, tile)` raises field i to 3 | `0x004A3100` | verified (operation); meaning unknown |
| walkers drawn elsewhere | `0x0083AD6C` | u8 | — | — | drawing state | drawing | `rendering.md` |
| route grid | `0x0105FE40` | u16 | — | — | walkability bits; the search of section 8.3 follows tiles with `& 0x11D` | — | verified (use) |
| tile objects | `0x00666930` | pointer | — | — | per-tile pointer; a tile whose object has byte `+0x18` = 0 cannot be built on (section 9) | — | verified (use) |
| copy of the terrain | `0x00B9D7A8` | u32 | — | — | block copy of the terrain flags made with the copy above | `0x0049AF50` | verified (copy) |

Two small blocks belong to this subsystem too: the regrowth flags `0x00B1E8F0`–`0x00B1E8F5`
(inside save rank 103, `0x00B1E8D8`, 32 bytes; section 8.1) and the cycle step `0x0091EC14`
(save rank 37, section 6).

The altitude pair is located in the GOG executable by the rendering code; for maps, the GOG
block table fills its entries 18 to 32 at run time (`formats/map.md`), and ranks 23 and 24 are
known from the 2.1 maps, where the same two grids sit `0xCB10` bytes apart behind the object
`0x0097C448` exactly as `0x008E01FA` and `0x008ECD0A` do here. That the GOG map ranks are the
same is a **hypothesis** until the run-time table is read.

## 4. Terrain flags (`0x00CCE128`)

The layout is inherited from the studio's earlier games (`formats/map.md`); the names below are
kept only where the GOG code confirms them.

| Bit | Meaning | Evidence | Status |
|---|---|---|---|
| `0x1` | tree | `0x004A81F0` gives it `SPR_TREES` (`SPR_POS_TREES` when `0x00902D0C` = 1), or `SPR_STUMPS` while regrowing; regrowth +4/+5 (`0x004A7D30`) | verified |
| `0x2` | rock (inherited name) | blocks building and roads (masks below); counts as "full" for tree density with `0x100000` and `0x200000` (`0x004A4840`); `formats/map.md` links it with cracks | hypothesis |
| `0x4` | water | `formats/map.md`; cleared of `0x80` and given tree depth 0 (`0x004A8E10`, mask `0x50004`) | verified |
| `0x8` | building | `rendering.md`; `0x0049F950` changes walls only where `0x8` is clear | verified |
| `0x10` | not established; does not block building | absent from the masks of section 9 | hypothesis |
| `0x20` | garden | spreads the desirability of model row 128, `BUILD_GARDENS` (`0x00493C80`) | verified |
| `0x40` | road | `formats/sav.md`; the build tool tests it (`0x0040BF40`) | verified |
| `0x80` | forest area (tiles that get a tree depth) | `0x004A8E10`; cleared on cliffs (`0x0045ED20`) | verified (use); name hypothesis |
| `0x200` | cliff | `formats/map.md`; `0x0045ED20` | verified |
| `0x400` | kept only on cliffs (`0x0045ED20` clears it elsewhere); inherited name: access ramp | blocks roads and walls (`0x1537166F`) but not buildings (`0x1537526F`) | hypothesis |
| `0x800` | meadow | `0x004A81F0` gives `SPR_MEADOW`, `SPR_MEADOW_LIGHT_GRASS`, `SPR_MEADOW_DESERT`, `SPR_MEADOW_GRASSY`; grazed by `0x004F8770`; regrowth +15/+16 | verified |
| `0x1000` | spreads desirability −2 over 2 rings (`0x00493C80`); inherited name: rubble | | verified (desirability); name hypothesis |
| `0x4000` | wall | `0x0049F950` sets or clears it and draws `SPR_STONE_WALLS` (`Zeus_General`) | verified |
| `0x10000` | a building area must lie wholly inside or wholly outside it (`0x004A0DC0`) | | verified (rule); meaning unknown |
| `0x40000` | reeds (marsh) | `0x004A81F0`: `SPR_REED_GROWN` when grown, `SPR_MARSH` while regrowing; regrowth +35/+36 | verified |
| `0x80000` | excludes a tile from the resource images (`0x004A81F0`) and counts as outside the forest (`0x004A8C90`) | | verified (rule); meaning unknown |
| `0x800000` | kept only on cliffs (`0x0045ED20`) | | verified (rule) |
| `0x4000000` | deep water | `formats/map.md` | verified |
| `0x10000000` | never regrows (`0x004A7D30`), stops grazing (`0x004F8770`), always blocks building (`0x004A0DC0` adds it to the caller's mask) | | verified (rules); meaning unknown |

`0x0045ED20` also enforces, on every tile: no cliff (`0x200`) keeps `0x800`, `0x80` or `0x1`;
a tile without `0x200` loses `0x400` and `0x800000`. **verified**

## 5. Functions that read and write the grids

| Function | Role | Calls (`working-city` / `poseidon`) | Status |
|---|---|---|---|
| `0x00511E30` | set-up of a new city from its map: clears the ground images, sets the regrowth grid to 255, runs one regrowth pass, maximum altitude along each edge of the diamond (`0x008F9820`–`0x008F9823`), version upgrades | 0 / 1 | verified (grid parts) |
| `0x00483840` | one step of the 51-step cycle (section 6) | 409 / 1,227 | verified |
| `0x004A7D30` | regrowth pass over the whole map (section 8) | 9 / 25 | verified |
| `0x004A81F0` | ground image of a resource tile: tree, reeds, meadow (section 8.2) | 2,936 / 11,206 | verified |
| `0x004A4840` | is ring r around a tile made only of trees (or `0x300002`, or off the map)? | 3,575 / 33,706 | verified |
| `0x004F8770` | a grazing walker eats the meadow it stands on (section 8.5) | 14,663 / 0 | verified |
| `0x0049F120` | eight flags: which neighbours have regrowth ≤ a threshold | 264 / 0 | verified |
| `0x00588B20` | breadth-first search on the route grid for the nearest tile of a list (section 8.3) | 13 / 0 | verified |
| `0x004A8E10` | tree depth `0x00C29158` and flag `0x80` over a rectangle | 0 / 0 | verified |
| `0x004A8C90` | ring distance (1 … max − 1) to the nearest tile lacking a mask | 0 / 0 | verified |
| `0x004AA190` | meadow values `0x00C35C68` and meadow images over a rectangle, then neighbour updates | not counted | verified (grid part) |
| `0x00493830` | desirability: clear, then every building (section 7.2) | 8 / 24 | verified |
| `0x00493C80` | desirability from the terrain and from avenues and boulevards (section 7.3) | 8 / 24 | verified |
| `0x004A4BF0` | spread around a square footprint, ring by ring | 6,176 / 36 | verified |
| `0x004A4C60` | add a value on one ring around a square footprint | 16,696 / 0 | verified |
| `0x004A4E70` | is tile k of a ring off the map? (offset tables) | 6,608 / 82,174 | verified |
| `0x004937B0`, `0x00493700` | spread around a rectangle / add on its border | 72 / 0, 352 / 0 | verified |
| `0x004842A0` | desirability of each building (section 7.4) | 8 / 24 | verified |
| `0x00433D50` | value k of model row t (`0x00782F70 + 40 t + 4 k`; `formats/model.md`) | 52,775 / 1,729 | verified |
| `0x004A0DC0` | is a w × h rectangle free for building? (section 9) | 908 / 2,311 | verified |
| `0x00496010`, `0x00496080` | inside the diamond / near its edge (section 2) | 927,454 / 28,329,229; 3,538 / 1,641,936 | verified |
| `0x00496B50` | does a rectangle contain one of the map's reserved points? (section 9) | 0 / 9 | verified (test); hypothesis (what the points are) |
| `0x0049CAE0` | number of neighbours having a mask | 39,027 / 51,240 | verified |
| `0x004A2510`, `0x004A2180`, `0x004A2590`, `0x004A2AC0` | neighbourhood pattern tests: all eight neighbours, or the blocks of 2 × 2 / 3 × 3 tiles in each of the eight directions, satisfy `(flags & mask) == value`; results in `0x00DA2470`–`0x00DA248C` | 0 / 0 | verified |
| `0x0045ED20` | projected altitude and the cliff consistency rules (section 4) | 4 / 2 | verified |
| `0x0051F450` | rebuild the ground images of a rectangle: a chain of image choosers (`0x005200B0`, `0x00520CA0`, `0x00520580`, `0x00520920`, `0x00520820`, `0x005214B0`, `0x00520B80`, `0x00521150`, `0x00521200`, `0x00521460`, `0x0051F860`), the plain ground `0x0051FBA0` last; calls `0x0045ED20` | 2 / 1,411 | verified (control flow) |
| `0x0051FBA0` | plain ground: tiles free of mask `0x2CFED66F`, larger pieces where `0x004A0DC0` finds room | 5,061 / 26,416 | verified |
| `0x0049AF50`, `0x0049F950` | drag walls over a line / set or clear the wall of one tile | 0 / 0 | verified |
| `0x0049FF10` | set or clear the road of one tile (setting it clears the garden bit `0x20`), then updates the road tiles among the 3 × 3 around it | not counted / 7,117 | verified |
| `0x00404CD0` | build tool clicked on the map: `switch` on the tool `0x00665DCC` | 16 / 9 | verified (dispatch) |
| `0x00451C70` | build cursor preview (reads the tool `0x00665DCC` and its size in `0x005F6368`) | not counted / 179 | verified (inputs); role hypothesis |

`0x004488C0` is the random number generator of `formats/sav.md` (two 31-bit shift registers
`0x00F137E4`/`0x00F137E0`, 31 steps per draw) with its derived values (`0x00F137D0` = 15 bits,
`0x00F137D4` = 7 bits, `0x00F137D8` = 3 bits…); before each draw the previous 7-bit value is
stored in a ring of 100 at `0x00F13628` (index `0x00F13624`). Most functions of this page copy
the same 31-step loop inline instead of calling it. **verified**

## 6. When the rules run: the 51-step cycle

Each simulation tick `0x00513A30` calls `0x00483840`, which runs one step chosen by
`0x0091EC14` (saved, rank 37), then increments it; after step 50 it wraps to 0 and calls
`0x00483C10` (the month counter of `formats/sav.md`). **verified** (409 ticks, 409 calls,
8 calls of `0x00483C10` in `working-city`).

| Step | Calls | Subject |
|---|---|---|
| 20 (`0x14`) | `0x00485360`, then **`0x004A7D30`** | regrowth (section 8) |
| 37 (`0x25`) | **`0x00493830`**, then **`0x00493C80`** | desirability (section 7) |
| 38 (`0x26`) | **`0x004842A0`** | desirability of the buildings |

So the regrowth and the desirability each run once every 51 ticks, a little over once a second
of real time (45 ticks per second), and 16 cycles make a month (`formats/sav.md`, rank 38).
**verified** (counts), the real-time figure from `rendering.md`.

## 7. Desirability

### 7.1 Spreading

A source is described by four values of its model row (`formats/model.md`; fields 1–4 of
`Zeus_Model_<level>.txt`, read by `0x00433D50(type, 1…4)`): **value**, **step**, **step size**,
**range**. `0x004A4BF0(x, y, size, value, step, step size, range)` does, for ring
r = 1 … min(range, 6): add the current value to every tile of ring r, and after every `step`
rings add `step size` to the value. Nothing happens when size ≤ 0. **verified**

- Ring r around a size × size footprint is the square ring at Chebyshev distance r: 8r + 4(size −
  1) tiles. **verified**: counts at `0x005F079E` (size 1: 8, 16, 24, … 88) and `0x005F07B6`,
  `0x005F07C2`, `0x005F07CE`, `0x005F07DA`, `0x005F07E6` (sizes 2 to 6: 12, 16, 20, 24, 28 for
  r = 1, then +8 per ring); tile offsets in run-time tables `0x0078A40C`–`0x0078AD6C`.
- `0x004A4C60(x, y, size, r, value)`: if the ring stays inside the map it adds the value through
  the offset table; otherwise it tests each tile with `0x004A4E70` (static dx, dy tables
  `0x005F0848`…`0x005F1120`, `0x0078A2BC`…) and skips those off the map. Each addition is clamped:
  above 100 the tile becomes 100, below −100 it becomes −100. **verified**
- Rectangles: `0x004937B0(x1, y1, x2, y2, value, step, step size, range)` does the same with
  `0x00493700`, which adds the value on the border of the rectangle grown by one tile, the
  rectangle growing by one tile on each side per ring; same clamp, no bounds test. **verified**

### 7.2 Buildings (`0x00493830`)

The grid is cleared to 0, then for every building numbered 1 … `[0x0091EBF8]` with state
(`+0x00`) 1 and `+0x38` = 0, with the four values of its type (`+0x10`): **verified**

- by default a square spread around (`+0x06`, `+0x08`) with the size `+0x03`;
- types for which `0x005344A0` is true, and types 100 to 114 (the Poseidon pyramid pieces):
  a rectangle from `0x0053CF30`, only when `+0xDA` is 0 or the type is 117 (`BUILD_PALACE`);
- types for which `0x0040BCF0` is true: a rectangle from `0x0040C9B0`; type 80 (`BUILD_STADIUM`):
  a rectangle from `0x005B02E0`;
- 58 (`BUILD_STOREHOUSE`) and 62 (`BUILD_TRADING_POST`): square of size 3 and 4; 60
  (`BUILD_DOCK`) and 133 (`BUILD_HORSE_RANCH`): a rectangle placed by the orientation byte
  `+0xAC`; 59, 61 and 134 (the bays, dock house and corral, parts of the former) spread nothing.

Type names are the row names of `Zeus_Model_Normal.txt` at those positions (`formats/model.md`:
the file order is the type id). The roles of `0x005344A0`, `0x0040BCF0` and of the three
rectangle functions are **hypotheses** (multi-tile monuments and sanctuaries).

### 7.3 Terrain, avenues and boulevards (`0x00493C80`)

Over the playable rows, tile by tile, each a size-1 source: **verified**

- tile of a building of type 118 (`BUILD_AVENUE`) or 126 (`BUILD_BOULEVARD`): the values of
  that type, from every tile (these are the only buildings spread per tile);
- else, piece flag `0x80` (`0x00D00D68`): if terrain `0x2` is set, the values of model row 2;
  otherwise the piece flag `0x80` is cleared;
- else terrain `0x20` (garden): the values of row 128 (`BUILD_GARDENS`);
- else terrain `0x1000`: value −2, step 1, step size +1, range 2 (so −2 on ring 1, −1 on ring 2).

Row 2 is `BUILD_HOUSE1_HUT` with zero desirability in the shipped model files: in practice the
`0x80`/`0x2` case adds nothing (**verified** in the five `Zeus_Model_<level>.txt` files; what the
case was meant for is unknown).

### 7.4 Desirability of a building (`0x004842A0`)

For every building in state 1: byte `+0xCE` = the highest desirability over its size × size
footprint (offsets `0x005F1560`, rows of 228), plus 10 when `+0xD0` is non-zero (`+0xD0` comes
from `0x004A4500`, `formats/sav.md`). **verified.** Size 1 reads the tile as unsigned, larger
footprints as signed; the stored byte is the same either way.

## 8. Regrowth (`0x00C1C648`)

### 8.1 The pass (`0x004A7D30`)

Over the playable rows; tiles with terrain `0x10000000` are skipped; the first of these that
applies, with g the regrowth value: **verified**

| Terrain | g < 255 | g = 255 | Flag set |
|---|---|---|---|
| `0x1` tree | draw, g += 4 + (bit 0 of the 15-bit value), saturating at 255; image `0x004A81F0` | tile appended to the start of the list `0x00B16BB4` (while `0x00B1E8F0` is 0) | `0x00B1E8F1` |
| `0x40000` reeds | draw, g += 35 + (bit 0 of `0x00F137E4`), saturating; image | appended to the end of the same list | `0x00B1E8F3` |
| `0x800` meadow | draw, g += 15 + (bit 0 of `0x00F137E4`), saturating; image | — | `0x00B1E8F5` |

The flags `0x00B1E8F0`–`0x00B1E8F5` are cleared at the start of the pass: they tell the rest of
the game that the map has trees, reeds or meadows (*hypothesis* for the readers). From g = 0 a
tree needs 51 to 64 passes to grow back, reeds 8, a meadow 16 or 17. With one pass every 51
ticks and 816 ticks a month (section 6), that is 3.2 to 4 months for a tree, half a month for
reeds and about a month for a meadow, or 58 to 73 s, 9 s and 18 s of real time at 45 ticks per
second (arithmetic from the increments; not timed in a game).

### 8.2 Images of resource tiles (`0x004A81F0`)

Only for tiles with no building and none of `0xC064` (water, road, garden, wall, `0x8000`), and
without `0x80000`: **verified**

- tree, g < 255: `SPR_STUMPS` + (random & 7);
- tree, g = 255: first image of `SPR_TREES` (or `SPR_POS_TREES`) + 32 × `min(tree depth, 100)
  / 34` + 8 × r + (random & 7), where r is the largest ring from 3 down to 0 made only of trees
  (`0x004A4840`; ring 0 always qualifies). In `Zeus_Terrain` the 32-image steps land on the
  groups that follow `SPR_TREES` in the file, `SPR_TREES_SOME_GRASS` and `SPR_TREES_LOTS_GRASS`;
- reeds (all eight neighbours reeds, `0x0049CAE0(0x40000, tile)` = 8): `SPR_REED_GROWN` when
  grown, `SPR_MARSH` while regrowing; nothing otherwise;
- meadow: one of `SPR_MEADOW`, `SPR_MEADOW_LIGHT_GRASS`, `SPR_MEADOW_DESERT`, `SPR_MEADOW_GRASSY`
  from two values converted from floating point (not traced) and random & 7.

### 8.3 Timber mills and grown trees

When the list of grown trees and reeds is full (start and end meet, 4,000 entries) and again at
the end of the pass, every building of type 51 (`BUILD_TIMBER_MILL`) in state 1 with `+0x40` ≠ 0
runs `0x00588B20(x = +0x26, y = +0x28, trees)`: a breadth-first search from that tile over the
route grid (`0x0105FE40 & 0x11D`), returning the rank of the first listed tile reached. When one
is found `0x00B1E8F0` = 1 and trees are no longer listed. **verified** (control flow); that
`+0x26`/`+0x28` is the mill's road access tile is a **hypothesis**. The list of reeds is filled
but not searched in this function.

### 8.4 Tree depth (`0x004A8E10`, `0x004A8C90`)

Over a rectangle (clamped to the map): tiles with any of `0x50004` get 0 and lose `0x80`; tiles
with `0x80` get `0x004A8C90(x, y, 0x340082, 6)`, the smallest ring r (1 to 5) holding a tile
inside the map that has none of `0x340082` or has `0x80000`, converted through a floating-point
step not traced (`__ftol`); −1 when none. Only caller: `0x0049DA10`, itself called by `0x00405650`
(not counted in the two runs). **verified** (control flow); the exact scale of the stored value is
**unknown** (it is divided by 34 in 8.2, which suggests a 0–100 scale).

### 8.5 Grazing (`0x004F8770`)

A walker routine (called from `0x004BC588`, `0x004BCCED` and `0x004CB5DF`, three call sites in
code Ghidra did not mark as functions) with its own timer `+0x36` (s16, kept within 0…70): **verified**

1. timer − 1; while it is not 0, return.
2. Standing tile `+0x20`: if it has terrain `0x10000000` or a building, action `+0x38` = 4,
   return.
3. Counter `+0x144`: on a bare meadow (`flags & 0x1000CA60` = `0x800`), with g the regrowth of
   the tile: set to 0 when g ≥ 250, minus 1 when 100 ≤ g < 250, unchanged when 50 ≤ g < 100,
   plus 1 when g < 50. On any other tile: the same rule when another walker (not of type 24)
   shares the tile, plus 1 otherwise.
4. Draw; with probability 7/8, if the tile is still a bare meadow without a building, stop here.
   Otherwise **g = 0** on the tile and `0x004F83E0(walker, 7, 4)`; if that returns non-zero,
   return it.
5. Timer = 20 + (15-bit draw mod 50).

*Hypothesis*: the walker is a sheep or goat of a farm (types 39 `BUILD_GOAT`, 40 `BUILD_SHEEP`
in the model), the counter measures hunger, and `0x004F83E0` moves it on. In `working-city`
the routine runs 14,663 times in 409 ticks, about 36 calls per tick: one per grazing walker and
tick if, as the timer suggests, it is called every tick (*hypothesis*).

No other function writes the regrowth grid: how a timber mill's cutter resets a tree is not
found in the direct writes and remains **open**.

## 9. Can a tile be built on?

`0x004A0DC0(x, y, w, h, mask, image, allow_10000, ignore_walkers, strict)` returns 1 when the
rectangle of w × h tiles from (x, y) is free; the build tools and the plain ground (`0x0051FBA0`,
for its larger pieces) both use it. **verified**, in this order:

1. x ≥ 3, y ≥ 3, x + w − 1 < width − 3, y + h − 1 < height − 3.
2. For each tile (offsets `0x005F1560`): not within 3 tiles of the diamond's edge
   (`0x00496080(…, 3)`);
3. the tile object `0x00666930` is null or has `+0x18` ≠ 0;
4. all tiles inside `0x10000`, or all outside;
5. `flags & (mask | 0x10010000) & M` = 0, with M = `0x3DFFD66F`, or `0x3DFED66F` when
   `allow_10000`, or `0x3DFFDEEF` when `strict` (which also forbids `0x80` and `0x800`);
6. no walker on the tile other than types 24 and 6 (unless `ignore_walkers`);
7. when `image` ≠ 0, the ground image equals `image − 1`.

So, with the full mask, a building may stand on bits `0x10`, `0x80`, `0x100`, `0x800` (meadow),
`0x2000`, `0x2000000`, `0x40000000` and `0x80000000`, and on nothing else. **verified** (bit
arithmetic of M).

Related tests: **verified**

- `0x00496B50(x1, y1, x2, y2)` is true when the rectangle contains one of the points stored at
  `0x00902BB4` (s16 pairs), `0x00902D10` (s32 pairs) or `0x00902C54` (s16 pairs); used by the
  placement code (`0x00498910`, `0x0041BE40`, `0x0049AF50`…). *Hypothesis*: the entry, exit and
  spawn points set in the editor.
- Masks used by other tools: walls (`0x0049AF50`) skip tiles with `0x1537166F`; the footprint
  test `0x0040BF40` forbids `0x1537526F` on "building" cells and requires a road without a
  building on "road" cells; the plain ground needs none of `0x2CFED66F`.

## 10. Order of the rewrite

What to write by hand, in the proposed order: each step only needs the steps before it, and the
most frequent come first within a step. Counts: original code alone, `working-city` 300 s
(409 ticks) / `poseidon` 190 s (1,227 ticks), deterministic.

| # | Function | Role | Calls (wc / pos) | Per tick (wc / pos) |
|---|---|---|---|---|
| 1 | `0x004488C0` and its inline copies | random draw | 97 / 0 (+ inline) | 0.2 / 0 |
| 2 | `0x00496010`, `0x00496080` | inside the diamond, near its edge | 927,454 / 28,329,229; 3,538 / 1,641,936 | 2,268 / 23,088; 9 / 1,338 |
| 3 | `0x0049CAE0`, `0x004A2510`, `0x004A2180`, `0x004A2590`, `0x004A2AC0` | neighbour counts and patterns | 39,027 / 51,240; others 0 | 95 / 42 |
| 4 | `0x004A0DC0`, `0x00496B50` | building area free | 908 / 2,311; 0 / 9 | 2 / 2 |
| 5 | `0x00433D50`, `0x004A4E70`, `0x004A4C60`, `0x004A4BF0`, `0x00493700`, `0x004937B0` | desirability spreading | 52,775 / 1,729; 6,608 / 82,174; 16,696 / 0; 6,176 / 36; 352 / 0; 72 / 0 | 129 / 1.4; 16 / 67; 41 / 0; 15 / 0; 0.9 / 0; 0.2 / 0 |
| 6 | `0x00493830`, `0x00493C80`, `0x004842A0` | desirability passes | 8 / 24 each | 1 in 51 |
| 7 | `0x004A4840`, `0x004A81F0` | tree density, resource images | 3,575 / 33,706; 2,936 / 11,206 | 9 / 27; 7 / 9 |
| 8 | `0x004A7D30`, `0x00588B20`, `0x0049F120` | regrowth pass, mill search | 9 / 25; 13 / 0; 264 / 0 | 1 in 51 |
| 9 | `0x004F8770` | grazing | 14,663 / 0 | 36 / 0 |
| 10 | `0x0045ED20` | projected altitude | 4 / 2 | rare |
| 11 | `0x004A8C90`, `0x004A8E10`, `0x004AA190` | tree depth, meadow values (editor and map changes) | 0 / 0 | — |
| 12 | `0x0051FBA0`, then `0x0051F450` and its choosers | ground images of a rectangle | 5,061 / 26,416; 2 / 1,411 | 12 / 22; 0 / 1 |
| 13 | `0x00483840` | the cycle dispatcher, once every step it calls is native | 409 / 1,227 | 1 |
| 14 | `0x0049FF10`, `0x0049AF50`, `0x0049F950`, `0x00404CD0`, `0x00451C70` | road and wall tools, build click, build cursor (with the interface) | 0 / 7,117; 0 / 0; 0 / 0; 16 / 9; not counted / 179 | — |

The 28 million diamond tests of `poseidon` (23,000 per tick, against 2,268 in the large city
where no tool is used) most likely come from the map tools and the build cursor (*hypothesis*):
the callers of `0x00496010` active in that run are `0x0049FF10`
(7,117 calls), `0x004A3670` (a negated test, 5,047), `0x00452B80` (1,469), `0x0058C3E0` (1,405),
`0x0051F450` (1,411), `0x00451C70` (179) and `0x00448990` (10 calls of a 4,424-byte function
with loops); which of them dominates was not measured per call site.

## 11. Open questions

- Meaning of terrain bits `0x10`, `0x10000`, `0x80000`, `0x800000`, `0x1000000`, `0x10000000`
  and of the grids `0x00CC1618`, `0x00B84188`, `0x00C8E9D8`, `0x00C753B8`, `0x00C688A8`,
  `0x00B1E8F8`.
- The floating-point steps of `0x004A8C90`/`0x004A8E10` and `0x004A4A50` (scale of tree depth and
  meadow value).
- Who resets a tree's regrowth when it is cut (no direct write besides grazing).
- Where the GOG executable fills map ranks 18–32 (altitude and the rest).
- Who reads the regrowth flags `0x00B1E8F0`–`0x00B1E8F5`.
