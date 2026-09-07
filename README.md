# Fallout 4 — Complete Modernization Program

**The goal:** take Fallout 4 (Next-Gen build 1.10.163) from a 2015 engine to a 2026
experience — modern image, 120 Hz smoothness, reactive squad-level AI, a living
world, 2026 combat, full accessibility — built entirely on the public modding
surface (F4SE plugins + data mods + D3D11/12 graphics injection), no engine source.

## Start here

| Document | What it is |
|---|---|
| [`docs/MODERNIZATION_ROADMAP.md`](docs/MODERNIZATION_ROADMAP.md) | **The plan.** 8 pillars, 30+ concrete work items, milestones M0–M7 with numeric exit criteria, risk table. |
| [`docs/DATA_INSIGHTS.md`](docs/DATA_INSIGHTS.md) | What the decompiled engine actually does (generated stats). |
| [`docs/DATA_REFERENCE.md`](docs/DATA_REFERENCE.md) | Inventory + data-quality caveats for every dataset. |

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

`plugins/rcai/` is the scaffold for **RCAI (Reactive Combat AI)** — the Milestone-1
F4SE plugin that modernizes the AI layer (perception, threat model, utility-based
behaviour over the engine's 3,828 AI packages, squad coordination, cover AI).

**Installing (runtime):** put `RCAI.dll` + `RCAI.ini` (see `config/`) into
`<game>/Data/F4SE/Plugins/` with F4SE 0.7.x. In-game console:

```
rcai_status    — version, state, settings
rcai_toggle    — enable/disable tick processing
<F3>           — debug hotkey (overlay lands in M1)
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
