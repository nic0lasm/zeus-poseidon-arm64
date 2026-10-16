# Simulation

What `Zeus.exe` (GOG edition) does in one simulation tick: the order of its parts, the 51-step
cycle that spreads the slow rules over 51 ticks, what each part and each module costs, and where
the functions the tick reaches belong. It is a map, not a description of the rules: each subsystem
gets its own page when it is rewritten, as [terrain](terrain.md) did. It answers step 5 of the
[roadmap](../roadmap.md): how large the simulation is and in what order to rewrite it.

Statuses are those of the other pages: **verified** (read in the code, or measured) and
**hypothesis** (inferred from the data a function touches, from its callers, or from the shape of
Julius, the open rewrite of Caesar III, an earlier game of the same studio; never proven). Addresses are those
of the GOG executable; our names are in [`analysis/symbols.tsv`](../../analysis/symbols.tsv), with
their evidence in [`analysis/noms.tsv`](../../analysis/noms.tsv). Saved-game ranks refer to
[`formats/sav.md`](../../formats/sav.md).

## 1. Measurements used in this page

- **Call tree of the tick.** `zeus-run --call-graph FILE.tsv` records every call made while the
  game runs (call site, callee, count) and the time of every block of game code, split between
  inside and outside the tick (`0x00513A30`). The nine scenarios of the bench were recorded with
  the original code alone (`--no-native`, deterministic clock), 13,073 ticks, plus `working-city`
  again with the native code on (409 ticks in its large city).
  `tools/simulation_map.py report` sums them up and writes
  [`analysis/simulation.tsv`](../../analysis/simulation.tsv): every function executed during the
  tick, the branches of the tick it descends from, its direct callers during the tick, and the
  share of its executions that happen inside the tick. The recorder is slow (a tick of the large
  city takes 16.5 ms under it, 2.8 ms without it), so it gives shares, not absolute times.
- **Real time.** `zeus-run --time-span START:RETURN,…` times the passes from one address to another
  at the cost of two code hooks, with the real clock. `working-city`'s script was played for
  150 s of real time, native code on, on one core of a cloud machine (x86-64 Linux, Unicorn 2.1);
  78.6 s of it is active play in the large city.

Calls are detected from the emulator's basic blocks: a block entered with the return address of
the previous block on top of the stack is a call. A jump into a function (a tail call) is not a
call, so the tick's last part, `walkers_update`, and the handlers that `month_events_dispatch`
jumps to have no caller in the tree; `simulation_map.py` counts them as branches of their own.

## 2. Order of a tick

`simulation_tick` (`0x00513A30`, 255 bytes) is called by `game_turn` (`0x00513580`) when the
simulation is due (`docs/engine/rendering.md`, section 12). In order: **verified** (listing and
call tree)

| # | What | Address | Share of the tick, large city |
|---|---|---|---:|
| 1 | Random draw: the last 7 bits of the previous draw go into a history of 100 (`0x00F13628`, index `0x00F13624`), then both 31-bit registers (`0x00F137E0`, `0x00F137E4`) step 31 times and their low bits are kept in six variables (`0x00F137C4` … `0x00F137D8`); the same generator as `random_next` (terrain.md §5) | inline | < 0.1 % |
| 2 | `tick_period_counters`: ten period counters, from 4 to 768 ticks (`0x00F1386C` … `0x00F13890`) | `0x00509DC0` | < 0.1 % |
| 3 | `undo_expire`: empties the undo memory once its delay `0x00DA2368` has run out or a building it noted has changed (hypothesis on the role) | `0x004A7A50` | < 0.1 % |
| 4 | `simulation_cycle_step`: one step of the 51-step cycle (section 3), only when `0x00665E2C` is zero | `0x00483840` | 17.2 % |
| 5 | `terrain_events_update`: the 8 terrain events of rank 115 (`0x0078C578`, 28 bytes each); those under way recompute the ground. Skipped when `0x00665E2C` is set, unless the screen `0x00F2A6DD` is `'X'` | `0x00446A70` | 0.1 % |
| 6 | `multi_tile_buildings_update`: the buildings in service whose type passes `building_type_composite` or is 100 to 114 (pyramid pieces) | `0x00534F60` | 1.3 % |
| 7 | `walkers_update`, entered by a jump: the 2,000 figures, each through the handler of its type (table `0x0063969C`), between the timers `0x004CC730` and `0x004CC760` | `0x004CC8C0` | 81.3 % |

Shares: original code, `working-city`, 409 ticks, time of each part including its callees,
corrected for the cost of the measure. In the first Poseidon adventure (empty land) the cycle is
30 % of the tick and the walkers 68 %.

## 3. The 51-step cycle

`simulation_cycle_step` runs the step numbered by `0x0091EC14` (rank 37), adds one, and after
step 50 goes back to 0 and calls `month_advance` (`0x00483C10`): month and year counters (ranks 6,
38, 39), `last.sav` when that option is on, and `month_events_dispatch` (`0x0046DFD0`), which
jumps to one of 33 event handlers. 16 cycles make a month. Steps not listed do nothing.
**verified** (listing; every listed step is reached in `working-city`); the subjects are
**hypotheses** unless the function's own row in `noms.tsv` says otherwise.

| Step | Calls | Subject |
|---|---|---|
| 1 | `city_totals_refresh` `0x00408C90` | ten computations on the city record (rank 4) |
| 3, 30 | — | sets `0x010DFFB8` |
| 4 | `treasury_debt_check` `0x00461520` | clears two debt variables when the treasury is positive (verified) |
| 5, 29 | `formations_update` `0x004DDD30` (0, then 1) | groups of figures (rank 31) |
| 6 | `buildings_timer_countdown` `0x004483F0` | a countdown in the houses (verified) |
| 7 | `road_networks_update` `0x00491FE0` | numbers the road networks, keeps the ten largest (verified) |
| 8 | `cycle_storages_pass` `0x0048B570` | storage (rank 81) |
| 9 | `figures_type39_count` `0x004DF3C0` | counts figures of type `0x39` (verified) |
| 10 | `buildings_update_highest` `0x004889F0` | highest building index (ranks 40, 68; verified) |
| 11 | `population_record` `0x00566930` | the population record (verified) |
| 12 | `cycle_buildings_pass_12` `0x00484FE0` | buildings and terrain |
| 13 | `houses_capacity_totals` `0x00567910` (when two flags are clear), `cities_relations_update` `0x00568C20` | houses; the 22 cities (rank 7) |
| 14 | `cycle_formations_pass_14` `0x00567C70` (same flags) | groups of figures |
| 16 | `cycle_storages_pass_16` `0x00488C30` | storage |
| 17 | `cycle_city_buildings_17` `0x0048B0D0` | buildings and city record |
| 20 | `cycle_buildings_pass_20` `0x00485360`, `terrain_regrow` `0x004A7D30` | regrowth: [terrain §8](terrain.md#8-regrowth-0x00c1c648) |
| 21 | `buildings_road_access` `0x004856A0` | access by road |
| 22, 24 | `cycle_houses_pass_22` `0x004860A0`, `cycle_houses_pass_24` `0x004871B0` | the list of houses (rank 73) |
| 23 | `cycle_city_pass_23` `0x00486190` | population |
| 25 | `cycle_labor_25a` `0x00487240`, `…_25b` `0x00487630`, `…_25c` `0x00487BA0` | labour |
| 28 | `houses_water_flags` `0x00484ED0` | water |
| 31 | `buildings_spawn_walkers` `0x0048CFB0` (16 KB) | every building sends out its walkers, like Julius's `building_figure_generate` |
| 32 | `cities_trade_update` `0x005B9EE0`, `cycle_buildings_pass_32` `0x00495860` | trade |
| 33 | `buildings_count_by_type` `0x00491820`, `cycle_totals_33` `0x00491EB0` | counts per type (ranks 51, 52) |
| 35, 36 | `cycle_buildings_pass_35` `0x00484D20`, `cycle_buildings_pass_36` `0x00484DE0` | unknown |
| 37 | `desirability_from_buildings` `0x00493830`, `desirability_from_terrain` `0x00493C80` | [terrain §7](terrain.md#7-desirability) |
| 38 | `buildings_update_desirability` `0x004842A0` | [terrain §7.4](terrain.md#74-desirability-of-a-building-0x004842a0) |
| 39 | `houses_evolve` `0x004FE290` | one function per house type (table `0x0063B91C`; verified) |
| 40 | `cycle_buildings_pass_40` `0x00484030` | unknown |
| 43, 44, 45 | `buildings_random_pass_43` `0x00493140`, `…_44` `0x00492260`, `…_45` `0x005BFDE0`; step 45 also `grid_b1e8f8_decay` `0x004A3140` | random events on buildings (ruins, fires, collapses) |
| 46 | `empty_function` `0x00401000` | nothing (a single `ret`) |
| 47 | `houses_counters_reset` `0x004936A0` | four counters (verified) |
| 48 | `cycle_buildings_pass_48` `0x00488220` | one field of every building |
| 49 | `animals_spawn` `0x004F8E30` | creates animals once in 100 draws (verified) |

## 4. What it costs

**In real time** (section 1, native code on): **4.36 ms per tick**, 43.3 ticks per second of active
play, so the tick takes **19 % of a core** in the large city. In the same run the city drawing
(`city_draw` `0x00458170`) takes 4.31 ms per frame, `game_turn` 5.89 ms per pass and `screen_draw`
`0x00514020` 2.73 ms; the whole process used 26 % of a core over the 150 s. Without the recorder
and under the deterministic clock, a tick of the early `working-city` takes 2.79 ms and a city
drawing 2.82 ms. The only native function the tick reaches is `strcmpi` (`0x005CC640`), which
costs nothing there, so the native code changes nothing in it (16.50 ms against 16.59 ms per
tick under the recorder).

**By module**, share of the tick's own time (each function's blocks, not its callees), original
code, all nine scenarios:

| Module | Functions reached | Share of the tick's time |
|---|---:|---:|
| walkers (`marcheurs`) | 191 | 41.5 % |
| buildings (`bâtiments`) | 143 | 24.0 % |
| terrain | 54 | 15.8 % |
| no module (`inconnu`) | 165 | 8.6 % |
| scenario (`scénario`) | 121 | 4.7 % |
| army (`armée`) | 16 | 3.0 % |
| economy (`économie`) | 31 | 1.9 % |
| interface, rendering, texts, sound, C library, gods | 57 | 0.6 % |

**By function**, in the large city: the helpers of composite buildings (section 5) take
**42 %** of the tick's own time: `composite_bounds` 8.6 %, `composite_road_entry` 7.9 %,
`composite_offset_rotate` 6.9 %, `composite_find_part` 6.1 %, `composite_part_class` 5.6 %,
`composite_part_size` 3.6 %, `building_type_composite` 2.0 %, `composite_parts` 0.9 %. Then come
walker handlers not yet named (`0x0058A8C0` 4.6 %, `0x004B2F70` 2.9 %, `0x004B5EB0` 2.8 %,
`0x00587FB0` 2.1 %), `walkers_update` itself (3.5 %) and `route_search` (2.2 %). In the cycle, the
costliest step is 31 (`buildings_spawn_walkers`, 4.1 % of the tick's time on average, so about
2 ticks' worth on the tick that runs it), then 12, 25 and 20.

## 5. Shared helpers

Many functions are called from several branches of the tick: by buildings and by walkers, by
the cycle and by the events. Two families take most of the time.

**Composite buildings.** Sanctuaries, monuments and the pyramid pieces are made of parts, each
with a class (`composite_part_class` `0x0053C1B0`: table `0x00640E08`, 164 classes, or
`0x00641098`, 7 classes numbered from 1000). Per type, `composite_parts` (`0x0053C2C0`) and
`composite_extras` (`0x0053C330`) give two lists of parts of `0x48` bytes; `composite_bounds`
(`0x0053CD80`) is the rectangle the parts cover for a position and an orientation;
`composite_offset_rotate` (`0x00546620`) turns a part's offset by the orientation;
`composite_road_entry` (`0x0053F810`) finds the nearest road tile around the building, preferring
the large networks of step 7, and `building_road_entry` (`0x0053FB60`) does it for a numbered
building. They are pure computations on static tables and the building record. In
`working-city` `composite_part_class` and `composite_offset_rotate` are called 3.4 and 3.3 million
times in 409 ticks, `composite_bounds` and `composite_road_entry` 53,000 times each. **verified**
(roles read in the code; costs measured).

**Routes.** `route_search` (`0x00502EE0`) is a breadth-first search on the distance grid
`0x01079460` (queue `0x010AC0A0`) with a cache of the last route; `route_search_area`
(`0x0058A4D0`), `route_distance` (`0x00588920`, mask `0x11D` on the route grid `0x0105FE40`) and
`route_distance_buildings` (`0x00588D30`, mask `0x13D`) are variants; `route_search_ship`
(`0x00503A60`) works on a second grid (water: hypothesis). **verified** (structure).

Other helpers named: storage (`storage_find` `0x00488BC0`, `storage_goods_take`/`_give`,
`storage_find_goods`, `storage_select`), `figure_type_value` (`0x004E1B20`, 61 callers),
`figure_wander` (`0x004D6C90`), `resource_tile_search` (`0x004A86E0`), `tile_step_to_anchor`
(`0x00418B00`), `tile_ground_image_test` (`0x00565760`) and the month tables of `month_advance`
(`0x00432810` …). Their evidence is in `noms.tsv`.

## 6. How large the simulation is

In the nine scenarios the tick reaches **778 functions, 255,027 bytes: 17.9 % of the code of all
functions.**

- **601** run mostly inside the tick (at least half of their executed blocks): 215,911 bytes. They
  are the simulation proper.
- **177** run mostly outside it: the C library, text and image helpers, map geometry used by the
  interface too (39,116 bytes). The tick borrows them.

Before this map, 700 of these 778 functions had no module. After it, **537 of the
601** have one (93 % of their bytes), 129 of the 778 by a name given by hand. The 64 left (14,443
bytes) are shared between branches of different modules: mostly between the walkers and the
gods (`0x00569…`, `0x0056A…`, `0x0052F…`), the walkers and the economy (`0x004D7580`), or every
branch (`0x0053ECA0`, `0x00433D80`). Their roots and callers are in `analysis/simulation.tsv`.

So the simulation that the scenarios exercise is about a fifth of the executable, and a quarter
of the code that no module claimed before. The rest of Phase C is not reached: wars, gods and
monsters, trade by sea, most events, disasters, saving and loading, the city's other screens.
It will be sized when a scenario reaches it.

## 7. Placing functions in modules

`tools/build_symbols.py` (step 4) uses the tick in this order:

1. The **roots** of the tick (the parts of section 2, the steps of section 3, the event handlers)
   are placed by hand in `noms.tsv`, each with its evidence.
2. A function that runs mostly inside the tick takes the module of the roots it descends from
   when they all agree, or when three quarters of them do.
3. Otherwise, the module of its direct callers during the tick when three quarters of them agree,
   repeated until nothing changes.
4. A function that runs mostly outside the tick is left to the other rules (strings, imports, the
   vote of callers and callees): where a utility is called from says nothing of its module.

**Accuracy.** The rule was checked by hiding the 52 names given by hand to non-root functions of
the tick and running it again: 9 landed in the right module, 2 in a wrong one (the two walker
timers, which `noms.tsv` puts in `scénario`), 40 were left without a module. Two variants were
tried and dropped: taking the most frequent module among the callers of a shared function (5
right, 10 wrong: the composite helpers went to terrain), and the data a function cites (44 right,
28 wrong on the named functions). Without the filter of rule 4, the rule placed 17 right and 15
wrong. So a module placed by the tick is a fair guess for a function that serves one branch, and
no guess at all for a shared helper: those are named by hand or left without a module.

## 8. Order of the rewrite

By measured cost, and by what can be checked one call at a time (`--verify`,
[equivalence.md](../equivalence.md)):

1. **The composite helpers** (section 5): 8 small functions, 1,900 bytes, 42 % of the tick's own
   time in the large city. They read static tables and write nothing but their results, so the
   call-by-call check fits them well. The best speed for the least code in the simulation.
   **Done** (`src/win32/native_buildings.cpp`, [equivalence](../equivalence.md#results-for-todays-replacements)):
   the tick of `working-city` falls from 5.3–5.8 ms to 3.1–3.3 ms under the deterministic clock.
2. **Routes**: `route_search` and its variants. Larger, with their own grids and cache, also
   checkable call by call. **Done** for the 15 that the scenarios call (`src/win32/native_routes.cpp`,
   [equivalence](../equivalence.md#results-for-todays-replacements)): every one clears the whole
   distance grid (52,000 entries) and walks the map, so a call costs hundreds of thousands of
   emulated instructions. Eleven of them are one function copied with a different mask on the
   route grid (`0x00588920` and seven copies, `0x0058A4D0`, `0x0058A8C0`); the others are
   `0x00587FB0` (bare land), `0x00588B20` (towards a listed tile), `0x00588D30` (buildings of three
   types passable), `0x00503A60` (water: hypothesis) and `route_search` itself (a cache of the last
   route, then a search weighted by the kind of each tile, or a plain one where only the owner may
   cross its building). Three copies no scenario calls (`0x005898F0`, `0x00589AE0`,
   `0x00589CE0`) and `0x005034E0`, `0x00588FD0`, `0x00503C40` stay with the original. The tick of
   `working-city` falls from 2.68–2.74 ms to 2.38–2.52 ms under the deterministic clock, about
   10 % less. The share measured under the recorder (about a quarter of the tick's own time) was
   inflated: it sees the clearing instruction (`rep stos`) as one block per entry, 6.65 million of
   them for the 128 calls of `0x0058A8C0`, each charged more than the correction removes.
3. **Terrain** (step 6 of the plan, [terrain §10](terrain.md#10-order-of-the-rewrite)): 15.8 % of
   the tick's time, already documented; its first steps are where the state is written, the first
   test of the tick-by-tick comparison.
4. **The walkers**: 41.5 % of the time but 191 functions, most not named; the handler table of
   `walkers_update` splits them by figure type, which is the natural unit of work. Measured again
   with composites and routes native (`working-city`, real time, deterministic clock):
   `walkers_update` takes 1.77 ms of a 2.38 ms tick, three quarters of it, and its own loop was
   the costliest function of the tick, 11 % of the code bytes executed, skipping the free records
   among the 2,000 (308 are in use per tick), plus two calls of the host (`GetTickCount`) per walker
   in its timers `0x004CC730` and `0x004CC760`. In order:
   1. **The loop. Done** (`src/win32/native_figures.cpp`): two host jumps do everything between
      two handlers (free records, the link `+0x7C`, the count `0x00EF57E8`, both timers and their
      table `0x00DB6EE0`); the removal of a record (`+6` = 2) and `0x004CC160` stay with the
      original. The tick of `working-city` falls from 2.49–2.73 ms to 1.85–2.12 ms (three runs of
      each, one after the other, on one cloud machine).
   2. **Soldiers**: types `0x0B`, `0x0C`, `0x0D` (hoplite, horseman, standard bearer in the name
      table `0x0063969C` + 36: hypothesis), handlers `0x004B31F0`, `0x004B26B0`, `0x004B1840`, with
      their shared `0x004B1630` (place in the formation, rank 31), `0x004B17F0` and
      `building_road_entry` `0x0053FB60`: about 0.57 ms per tick, a quarter of the tick
      (`--time-span` from each handler to its return, `0x004CC987`).
   3. **Growers** (type 4, `0x004AE160`): 0.19 ms per tick.
   4. `figure_type_value` (`0x004E1B20`, 300 calls per tick from most handlers): only inside a
      native caller, since a hook costs about as much as its emulated code.
   5. The other handlers, each under 3 % of the tick: warehousemen (`0x004AF930`), peddlers
      (`0x004B5F10`), noblemen (`0x004B8D90`), goats (`0x004BC380`)…
5. **The cycle**: 17 % of the tick, step by step; 31 (`buildings_spawn_walkers`, 16 KB) is the
   largest.

## 9. Open questions

- Steps 3 and 30 set `0x010DFFB8`; what reads it?
- Why the cycle is skipped while `0x00665E2C` is set, and terrain events kept only on screen `'X'`.
- What the 64 shared functions of section 6 belong to; the walkers–gods cluster may be the
  divine figures.
- Whether the composite helpers are as costly in the game's other cities (sanctuaries and
  monuments make up a large part of `working-city`).
- The real-time figures come from one cloud machine; they should be measured on an M1.

## 10. Reproduce

```bash
cmake --build build
python3 tools/simulation_map.py record            # build/simulation/*.tsv
python3 tools/simulation_map.py report --table analysis/simulation.tsv
python3 tools/build_symbols.py build/carte.tsv analysis/symbols.tsv   # carte: ExportCodeMap.java

./build/zeus-run --run original/gog/app/Zeus.exe --headless --mute --write-dir build/essais/temps \
    --seconds 150 --ticks --time-span 0x00513A30:0x005135C8,0x00458170:0x0044A8CE \
    --script "$(grep -v '^#' tests/scenarios/working-city.txt | tr '\n' ';' | sed 's/;$//')"
```

The return addresses of `--time-span` are those of the call sites (`0x005135C8` follows the call
of the tick in `game_turn`, `0x0044A8CE` the call of `city_draw`). Recordings are not kept in the
repository.
