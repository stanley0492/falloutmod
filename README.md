# Fallout 4 — Complete Modernization Program

**The goal:** take Fallout 4 (Next-Gen build 1.10.163) from a 2015 engine to a 2026
experience — modern image, 120 Hz smoothness, reactive squad-level AI, a living
world, 2026 combat, full accessibility — built entirely on the public modding
surface (F4SE plugins + data mods + D3D11/12 graphics injection), no engine source.

## Start here

| Document | What it is |
|---|---|
| [`docs/MILESTONE_STATUS.md`](docs/MILESTONE_STATUS.md) | **Where we are.** M0 ✅, M1–M7 logic done + tested, in-game verification pending Windows. |
| [`docs/MODERNIZATION_ROADMAP.md`](docs/MODERNIZATION_ROADMAP.md) | **The plan.** 8 pillars, 30+ concrete work items, milestones M0–M7 with numeric exit criteria, risk table. |
| [`docs/INTEGRATION_CHECKLIST.md`](docs/INTEGRATION_CHECKLIST.md) | **To ship it.** Exact Windows steps: F4SE DLL build, data→`Data/RCAI/`, CK Papyrus, soak, render proxy, gauntlet. |
| [`docs/DATA_INSIGHTS.md`](docs/DATA_INSIGHTS.md) | What the decompiled engine actually does (generated stats). |
| [`docs/DATA_REFERENCE.md`](docs/DATA_REFERENCE.md) | Inventory + data-quality caveats for every dataset. |
| [`docs/ACCESSIBILITY_AUDIT.md`](docs/ACCESSIBILITY_AUDIT.md) | M7 a11y audit — 12/12 rows mitigated by design. |

## The evidence base

`Decompiled Fallout 4.zip` (and its extracted form, `DecompiledMaster/`) is a full
decompilation of the game's engine surface: 4,097 RTTI classes, 3,621 hardcoded
GMSTs, a 1.58 M-symbol address library, all AI packages / combat styles / factions /
weapons / perks / magic, the complete Papyrus native-function & event registry
(1,669 natives, 167 events, 10,307 scripts), and the INI configuration map.

Run the analyzer to regenerate the stats:

```bash
python3 tools/analyze_decompiled.py
```

## The flagship plugin: RCAI

`plugins/rcai/` is **RCAI (Reactive Combat AI)** — the F4SE plugin that modernizes
the AI layer: perception (sight/hearing/smell from the decompiled GMSTs), threat
ranking, a utility Brain over 13 actions, cover solving (hard + grazing
occlusion), squad roles/leader/alerts, anti-cheese, adaptive difficulty, faction
memory (M6) — all executed by a deterministic headless sim that doubles as the
test harness.

**Verified on Linux (this repo):** 29/29 C++ unit tests, 12/12 Python data
tests, PSC lint clean, and the M2 scoreboard gates:

```
G1 combat time in cover   0.320  (gate >= 0.30)
G2 flanking               43 shots / 6 flankers (gate >= 2/2)
G3 first kill             13.8 s (gate <= 30 s)
G4 enemy survival @30 s   0.710  (gate >= 0.40)
```

Reproduce: `g++ -std=c++17 -O2 -I plugins/rcai -I plugins/rcai/src
-o /tmp/t plugins/rcai/tests/*.cpp && /tmp/t` (bench: same with
`plugins/rcai/bench/bench_ai.cpp`).

**In-game wiring (Windows):** the IWorldSampler adapter + DLL build are the
remaining step — see [`docs/INTEGRATION_CHECKLIST.md`](docs/INTEGRATION_CHECKLIST.md).

**Installing (runtime):** put `RCAI.dll` + `RCAI.ini` (see `config/`) into
`<game>/Data/F4SE/Plugins/` with F4SE 0.7.x, copy the `data/` trees into
`Data/RCAI/`, import the three `scripts/RCAI/*.psc` in the CK, and attach them
to a start-on-load quest. In-game console:

```
rcai_status        — version, state, perf, anti-cheese
rcai_toggle        — enable/disable tick processing
rcai_tune          — apply performance INI baseline
rcai_dump_prof <path.csv> — profiler CSV (feeds tools/perf_report.py)
rcai_dump_memory   — faction-ledger dump -> Data/RCAI/memory_dump.json
rcai_sandbox_mode <0..3> / rcai_inject_raid <settlement> — M6 sandbox
```

**Building (Windows):**

1. Grab the latest [F4SE release](https://github.com/afrocat/F4SE/releases), unzip it,
   and place its `include/` and `lib/` folders under `third_party/f4se/`.
   (CI does this automatically — see `build.yml`.)
2. `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`
3. `cmake --build build --config Release` → `build/Release/RCAI.dll`

Tagging `v*` on `main` triggers the release build that uploads `RCAI.dll` +
`RCAI.ini`.

## Repo map

```
DecompiledMaster/            decompiled knowledge base (21 datasets + pipeline scripts)
DecompiledKnowledge/         earlier extraction (superseded, kept for reference)
tools/analyze_decompiled.py  knowledge-base analyzer → data/insights.json, docs/DATA_INSIGHTS.md
docs/                        roadmap + data docs
plugins/rcai/                RCAI F4SE plugin (scaffold)
config/RCAI.ini              RCAI settings
build.yml / CMakeLists.txt   CI + build
```

## Legal

Personal-use research on a legally purchased copy of Fallout 4. The JSON knowledge
base is derived analysis of game files — **do not redistribute the zip or the
extracted datasets**. Public releases of this program ship mod code + data mods only,
within Bethesda's modding terms.
