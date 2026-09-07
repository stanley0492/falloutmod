# Integration Checklist (Windows)

Everything in this repo is built and tested on Linux except what is listed
below. These are the exact in-game steps, in order, to bring each milestone
live. Companion deps verified Sept 2026 (see roadmap §13).

## Prerequisites

1. Fallout 4 build **1.10.163** (Next-Gen compatible), Steam.
2. **F4SE 0.7.x** (supports 1.10.163) → `FO4SE.exe` at game root.
3. **Address Library** (Nexus 47327) — hard dependency of F4SE 0.7.x;
   the 25.3 MB symbol map (1,582,975 symbols) is what lets F4SE survive
   engine patches.
4. **High FPS Physics Fix** (Nexus 44798) for 120 Hz targets (M3/M4).

## 1. Build the DLL

```
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   (MSVC, x64)
cmake --build build --config Release
```
Outputs `RCAI.dll` (F4SE plugin) + copies into
`Data/F4SE/Plugins/RCAI.dll`. CI (`.github/workflows/build.yml`) does this on
`v*` tags and uploads `RCAI.dll` + `RCAI.ini`.

## 2. Data files → Data/RCAI/

| Repo path | Game path | Used by |
|---|---|---|
| `config/RCAI.ini` | `Data/RCAI/RCAI.ini` | plugin (perf budgets, archetypes, binds) |
| `data/worldsim/factions.json` | `Data/RCAI/worldsim/factions.json` | faction ledger seed (M6) |
| `data/worldsim/faction_graph.json` | `Data/RCAI/worldsim/faction_graph.json` | sandbox sim |
| `data/worldsim/settlements.json` | `Data/RCAI/worldsim/settlements.json` | settlement ledger |
| `data/worldsim/economy.json` | `Data/RCAI/worldsim/economy.json` | price dynamics |
| `data/combat/weapons.json` | `Data/RCAI/combat/weapons.json` | ballistics curves (M5) |
| `data/combat/combat_styles.json` | `Data/RCAI/combat/combat_styles.json` | archetype map |
| `data/render/fo4_next_<tier>.json` | `Data/RCAI/render/fo4_next_<tier>.json` | render preset (M4) |

The IWorldSampler adapter (F4SE side) is the only C++ that hasn't been
compiled in this environment: it binds `plugins/rcai/src/world/world.h` to
live engine forms (position, LOS via `IsReferenceVisible`, cover via
`F4SE` occlusion queries, perception via the decompiled sight/hearing GMSTs).
The headless sim in `plugins/rcai/src/ai/sim.h` is the reference
implementation the adapter must match (same `WorldSnapshot`, same tick rate).

## 3. Papyrus scripts

1. Open the CK on a clean plugin, import `scripts/RCAI/*.psc` (3 scripts).
2. Compile all (this is the only Papyrus validation not possible headless).
3. Create quest `RCAI_Quest` (start-on-load, essential), attach
   `RCAI_Accessibility` + `RCAI_AudioCues` + `RCAI_Sandbox`.
4. Link `Sound` properties in `RCAI_AudioCues` to the shipped
   `Data/Sound/RCAI/*.wsb` assets.
5. The plugin raises Papyrus events `RCAICombat(type, detail)` and
   `RCASquadAlert(faction, detail)` via F4SE's Papyrus VM interface — the
   `Event RCAICombat(...)` handlers in `RCAI_Accessibility.psc` must be
   reachable on the quest script instance (send to the global quest reference).

## 4. First run (smoke)

1. Launch via FO4SE with `Data/RCAI/RCAI.ini` present.
2. Console: `RCAIStatus` → expect `v0.2.x · 20 archetypes loaded · ledger 699 factions`.
3. Console: `rcai_dump_prof <path>` (CSV accumulates from load; run the fight, then dump) → CSV; run
   `tools/perf_report.py <csv>` → all gates pass.
4. Start a fight, console `RCAIDumpBrain` → inspect top goals for 3 actors.

## 5. M2 acceptance in game

Replay the bench scenario (20 enemies, 5 archetypes) in a real cell and
compare the scoreboard against `reports/ai_bench.txt` — cover time ≥ 30%,
flanking ≥ 2 shots/engagement, TTK < 30 s, enemy survival ≥ 40% at 30 s.

## 6. M3 soak

- 100 h soak on the benchmark cell list (roadmap §P0-a): record
  `RCAIDumpProf` every 10 min; zero CTD (watchdog dumps must be empty).
- `reports/perf_report.md` regenerated from soak CSVs; median FPS delta vs
  stock baseline must be ≥ +20%.

## 7. M4 render

- Bind the D3D11 upscaler proxy (FSR3 frame-gen → DLSS fallback) to the
  preset's `d3d11_proxy` slot; verify with the FO4SE bridge (windowed mode
  required for the proxy chain — ENB/PureDark-compatible).
- Capture 4K/120 on the high preset; compare against stock 1080p/60 baseline.

## 8. M5 gauntlet

- 120 Hz combat gauntlet: 5 fixed fights (melee ring, cover duel,
  ambush, siege, vehicle) — parity-or-better vs stock at the same 120 Hz
  (TTK, hit registration, damage curves from `data/combat/ballistics.json`).

## 9. M6 sandbox

- `rcai_inject_raid <settlement>` ×5 over 3 in-game days → verify ledger
  attitude drift + price dynamics in `rcai_dump_memory` output.

## 10. M7 UX

- Controller-first playthrough (no keyboard/mouse for 3 h).
- Accessibility passes: announcements on, palette 1 + 2, HUD scale 1.5,
  audio cues on; log against `docs/ACCESSIBILITY_AUDIT.md` rows.
