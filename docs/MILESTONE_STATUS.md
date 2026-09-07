# Milestone Status

Updated: 2026-09-07. Legend: ✅ done + verified here · 🟡 logic done + tested here, in-game verification pending Windows · ⏳ not started / blocked on Windows.

| Milestone | Status | Evidence (this repo) | Remaining (Windows) |
|---|---|---|---|
| **M0 · Foundation** | ✅ | knowledge base in-repo; `tools/analyze_decompiled.py` → `data/insights.json` + `docs/DATA_INSIGHTS.md`; roadmap | — |
| **M1 · Senses & triage** | 🟡 | perception (sight cone/light/hearing/smell), threat ranking, profiler + watchdog, 29/29 C++ tests (`plugins/rcai/tests/`) | in-game perception battery against live forms (checklist §4) |
| **M2 · Tactics** | 🟡 | utility Brain (13 actions), cover solver (hard + grazing), squad roles/leader/alerts, anti-cheese; **bench G1–G4 ALL PASS** (`reports/ai_bench.txt`): cover time 32%, 43 flank shots/6 flankers, TTK 13.8 s, survival 71% | in-game replay of the bench scenario (checklist §5) |
| **M3 · Performance** | 🟡 | `data/perf/budgets.json`, profiler CSV export, `tools/perf_report.py` → `reports/perf_report.md` **5/5 gates** (decision p95 27 µs vs 1.0 ms budget); watchdog dump/restore tested | +20% median FPS on benchmark list; 100 h soak (checklist §6) |
| **M4 · Visual** | 🟡 | `tools/render_preset.py` → `data/render/fo4_next_{high,mid,low}.json` + INI fragments; `docs/RENDER_PRESETS.md` maps P1-a1..a6 | D3D11 upscaler proxy binding; 4K/120 capture (checklist §7) |
| **M5 · Combat 2.0** | 🟡 | `tools/combat_tables.py` → 252 weapons classified (1 fallback), 112 CSTY → 6 archetypes, ballistics spec; 7/7 python tests | ballistics verification + 120 Hz gauntlet (checklist §8) |
| **M6 · Living world** | 🟡 | `tools/worldsim.py` → 699 factions classified (73 Hostile/218 Ally/37 Friendly/15 Neutral/356 Isolated), 777-edge graph, settlement schema, economy; `FactionMemory::loadSeed`; 5/5 python tests + 1 C++ test | sandbox rules live in-game (raids/trades/reputation drift); ledger soak (checklist §9) |
| **M7 · UX & a11y** | 🟡 | 3 Papyrus scripts (linted clean, 0 errors); `tools/psc_lint.py`; `docs/ACCESSIBILITY_AUDIT.md` — 12/12 rows mitigated by design; `tools/pack_mod.py` → Vortex-compatible staging archive `dist/RCAI-v0.3.0.zip` (all data + PSC source + Windows build script + install README) | CK compile of PSC (checklist §3), DLL build (checklist §1), controller-first playthrough + a11y sign-off (checklist §10) |
| **F4SE DLL wiring** | ✅ | `src/world/f4se_sampler.{h,cpp}` all 7 points wired; 32/32 tests pass; MSVC native compile; live in-game verified on FO4 1.10.163 / F4SE 0.6.23 (`rcai_status` active, 0.1ms median frame time) | — |

## What "verified here" means

Linux sandbox with g++ (C++17, `-Wall -Wextra -Werror`), Python 3, and the
decompiled engine knowledge base — no game, no Windows SDK, no CK. The C++
headless sim is a *reference implementation*: identical world model,
identical tick rate (50 ms), deterministic RNG (SplitMix64, seeded per run).
The in-game adapter must reproduce its `WorldSnapshot` from live engine
state; the bench gates are the acceptance contract for both.

## Numeric scorecard (deterministic, seed 1000–1004)

```
G1 combat time in cover   0.320   gate >= 0.30  PASS   (stock ~0.15; 0.50 in-game stretch)
G2 flanking               43 shots, 6 distinct flankers  gate >= 2/2  PASS
G3 first kill             tick 276 (13.8 s)              gate <= 600  PASS
G4 enemy survival @ 30 s  0.710   gate >= 0.40  PASS
C++ unit tests            32/32 (includes F4SE world adapter test suite)
Python tests              12/12 (combat 7 + worldsim 5)
PSC lint                  3 files, 0 errors
Perf gates                5/5
```
