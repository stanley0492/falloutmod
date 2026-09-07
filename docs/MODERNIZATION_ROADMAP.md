# Fallout 4 — Complete Modernization Roadmap

**Status:** program plan · v1.0 (2026-09-07)
**Evidence base:** the decompiled knowledge base in this repo (`DecompiledMaster/`, game build
**1.10.163.0** — the Next-Gen engine), quantified in `data/insights.json`
(regenerate with `python3 tools/analyze_decompiled.py`).

---

## 1. Executive summary

Fallout 4's engine (NetImmerse lineage, ~2014 design, patched through 2019/2021) has five
structural limits that no amount of content tuning can remove:

1. **AI is a state machine, not a mind** — 3,828 hand-authored PACK packages,
   of which only **~28%** carry a recognisable tactical verb in their editor ID;
   112 fixed-float combat styles; no squad coordination, no cover exploitation
   beyond the single `bPartialCover` GMST, no reaction to player strategy.
2. **The Papyrus VM is the FPS bottleneck** — a single-threaded interpreter with a
   hard per-frame update budget (`fUpdateBudgetMS` in the Papyrus INI section) that
   10,307 shipped scripts compete for; 1,669 native functions form the bridge.
3. **The renderer is a DX11-era fixed pipeline** — precomputed light volumes,
   baked shadows, no global illumination, 250 NetImmerse renderer classes and
   263 Scaleform UI classes doing double duty as the "modern UI".
4. **The world is a snapshot** — 699 factions with a sparse, static hostility
   graph (356 factions — 51% — have zero faction relations); the sandbox respawns
   and forgets.
5. **Combat physics is 2014 physics** — 5 weapon GMSTs of tuning, 5 stagger GMSTs,
   turn-based VATS (56 fVATS GMSTs) as the primary "assisted" aim model.

The modernization strategy is a **three-layer stack** built entirely on the public
modding surface — no engine source is required:

| Layer | Vehicle | What it can change |
|---|---|---|
| **L1 · Data** | Creation-Kit mod (ESM/ESL records + GMST overrides) | balance, content, faction graph, combat-style parameter sets, perk trees |
| **L2 · Engine hooks** | **F4SE plugins** (0.7.x supports the 1.10.163 Next-Gen build) | new C++ subsystems (AI, profilers, streaming, Papyrus extensions), runtime behaviour |
| **L3 · Graphics injection** | D3D11 proxy / F4SE render hooks (GamePlug-class technique, proven in FO4 in 2025) | upscaling (FSR2/3, DLSS, XeSS), frame generation, ray-traced reflections, volumetrics, HDR |

The flagship L2 plugin is **RCAI (Reactive Combat AI)** — scaffolded in this repo
(`plugins/rcai/`, CI in `build.yml`) — because AI is the single most-felt gap between
the 2015 game and a 2026 expectations bar.

**North star (what "modern" means, 2026):**
* 120 Hz+ smooth on mid-range hardware (frame gen + real CPU fixes, not just tricks)
* dynamic, reactive, squad-level AI that you can out-think *and* be surprised by
* a living world: factions that remember, economies that move, events that happen
* modern image: GI-quality lighting, volumetrics, RT reflections, 4K/120 via upscaling
* full accessibility and controller quality
* crash-safe under heavy mod loads, with tooling that measures all of the above

---

## 2. Baseline — what the decompiled engine actually does

From `data/insights.json` (see `docs/DATA_INSIGHTS.md`):

| Subsystem | Measured surface | Implication |
|---|---|---|
| Engine classes | 4,097 RTTI (AI/Combat 708, Core 2,208, Renderer 250, Scaleform UI 263, Havok 105, Animation 90, Audio 55, Papyrus 38) | every subsystem is hookable via the 1.58 M-symbol address library |
| GMSTs | 3,621 hardcoded; reliable prefix counts: **1,538** combat, **112** AI, **92** magic, **56** VATS, **37** weapon, **26** sneak, **6** action-point, **5** stagger | balance is GMST-driven → a data-layer retune reaches most combat feel with zero code |
| AI packages | 3,828 PACK; top verbs: use 148, combat 130, wait 116, patrol 115, follow 105, work 92, escort 74, flee 30, defend 32 | ~72% of all behaviour is ambient/idle-derivative; tactical behaviour is thin |
| Combat styles | 112 CSTY, all with CSGD+CSME+CSRA+CSCR+CSLR+CSCV+CSFL fixed float sets | one static parameter set per archetype; no runtime adaptation |
| Factions | 699 FACT; relations: Ally 343 / Friend 262 / Enemy 94 / Neutral 78; **356 factions (51%) have zero relations** | the social world is a frozen graph |
| Content | 252 weapons, 682 armor pieces (73 slot masks), 139 projectiles, 2,408 OMODs, 638 MGEF (156 scripted), 538 perks (71% single-rank), 45 races, 359 actor values | the weapon-mod (OMOD) and actor-value (limb damage) systems are the natural substrate for combat modernization |
| Scripting | 10,307 .psc → 1,669 native functions over 60 bound scripts (top: ObjectReference 419, Actor 384, Game 207), 167 events | the Papyrus API surface is wide but the VM is the bottleneck |
| Configuration | INI domains: Display/Graphics, Physics/Havok, Papyrus VM, Cell Streaming, Archive/Loose files; Papyrus exposes `fUpdateBudgetMS`, `fExtraTaskletBudgetMS`, memory page sizes; streaming exposes `uGridsToLoad`, cell buffers, `bUseThreadedAI` | every tuning knob is already documented in `engine_configuration_subsystems.json` |

**Key insight:** the decompiled knowledge base converts "the game is closed-source" into
"a documented C++ system with a 1.58 M-entry symbol map". Every pillar below cites the
exact artifact that makes the work tractable.

---

## 3. Pillar P0-a · Performance & stability (do this first — everything else multiplies onto it)

**Why first:** frame-gen and AI overhauls amplify CPU load; a modern FO4 that stutters
is a worse experience than the stock game.

Work items:

1. **P0-a1 · Frame profiler inside F4SE** (M, 2–3 wks)
   Hook the per-frame message (`f4se::update`) + subsystem timing via the RTTI class
   map (AI/Combat 708, Papyrus 38, Havok 105 classes as markers). Emit
   per-subsystem ms/frame to CSV + on-screen overlay. *Acceptance: a standard
   Goodneighbor/Institute benchmark cell produces a profile that reproduces known
   hotspots (Papyrus tasklets, cell streaming) within 10% of an external profiler.*
2. **P0-a2 · Papyrus VM scheduler** (L, 4–6 wks)
   The VM admits a fixed update budget (`fUpdateBudgetMS`). Build an F4SE layer that:
   pre-compiles all loaded `.pex` at load, measures per-script tasklet cost, and
   dynamically demotes low-impact scripts (ambient dialogue, unused quest markers)
   below the budget floor — the "script priority scheduler".
   *Acceptance: +20% median FPS on a 150-mod list at unchanged frame pacing;
   zero quest regressions in the save-regression suite.*
3. **P0-a3 · Cell-streaming & memory manager** (M)
   Wrap cell load/unload messages: async precombine/mesh/text prefetch for
   neighbouring grids, hard VRAM budget with LRU texture eviction reporting, and a
   streaming stall counter. Tune against `uGridsToLoad`/cell-buffer INIs.
   *Acceptance: 50% fewer 50 ms+ streaming stalls in a 20-min exterior traversal benchmark.*
4. **P0-a4 · Physics governor** (S)
   High-FPS Havok clamping (the `fMaxTime`/`fMaxTimeComplex` INI pair) as a shipped
   default + distance-based ragdoll/physics culling for non-essential actors.
   *Acceptance: 144 Hz frame pacing with no simulation divergence at 60 Hz.*
5. **P0-a5 · Crash watchdog** (M)
   SEH guard around Papyrus native dispatch (1,669 function boundary is the attack
   surface), structured JSON crash dumps, auto-restore to last known-good save slot,
   and a leak detector on the 2,208 Core/Memory classes.
   *Acceptance: no uncaught crash in a 100-hour modded soak test; every dump machine-parseable.*

**Measurement harness (P0-a0):** three scripted benchmark cells (dense interior,
open exterior, combat gauntlet) run headless with recorded frame times — everything
above is gated by before/after numbers, not vibes.

---

## 4. Pillar P0-b · RCAI — Reactive Combat AI (flagship)

**Why flagship:** AI is the most-felt 2015 artefact, and the decompiled data maps the
entire AI surface: 3,828 PACKs, 112 CSTY float sets, 112 fAI/bAI GMSTs, 1,538 fCombat
GMSTs, 149 sensory-detection GMSTs, and the full Actor Papyrus surface (384 natives).

### Architecture

```
                 ┌────────────────────────────────────────────────┐
                 │                  RCAI (F4SE plugin)            │
                 │                                                │
  player actions │  ┌──────────────┐   goals   ┌───────────────┐  │
 ────────────────┼─▶│  Perception  │──────────▶│  Behaviour    │  │
  engine events  │  │  (senses,    │           │  (utility AI, │──┼──▶ rewritten PACK
  (f4se::update, │  │   threat,    │  roles    │   squad coord,│  │     package ticks,
   messaging)    │  │   memory)    │◀─────────▶│   cover solver)│  │     actor steering,
                 │  └──────────────┘           └───────────────┘  │     Papyrus API export
                 │         ▲                        │             │
                 │         │        ┌───────────────▼──────┐      │
                 │         └────────│  World state cache   │      │
                 │                  │ (cells, LOS, cover,  │      │
                 │                  │  factions, inventory)│      │
                 │                  └──────────────────────┘      │
                 └────────────────────────────────────────────────┘
```

### Work items

1. **P0-b1 · Perception module** (L, 4–6 wks) — replaces the 149-sensory-GMST soup with
   per-actor sensory profiles: sight (cone, distance, **dynamic light/flashlight model**),
   hearing (distance, occlusion, footstep class), smell (chems/poison), plus a **threat
   model** (continuous 0–1 with decay) and a **memory system** (last-seen location,
   player habits, 30-min decay).
   *Acceptance: scripted perception battery — enemies detect a flashlight player at the
   correct modeled distance ±1 m; hiding behind a door stops sight but not hearing.*
2. **P0-b2 · Behaviour layer: utility AI over packages** (L, 6–8 wks) — RCAI does not
   delete the engine's state machine; it **rewrites the combat package tick**. Per actor,
   a utility scorer ranks goals — *take cover / flank / suppress / aid ally / retreat /
   call reinforcements / use vehicle / flee* — each scoring against world state; the
   chosen goal is expressed by re-issuing engine packages + direct steering (the engine
   already supports MoveTo/Combat/EnterVehicle procedures — RCAI sequences them).
   Squad coordinator: within a combat group, one actor becomes **team leader** and
   assigns roles (2 suppressors, 1 flanker, 1 medic/heavy) and redistributes them on
   casualties.
   *Acceptance: AI benchmark gauntlet — cover-usage rate ≥ 60% of shots fired from cover
   (baseline ≈ 15%), flanking events ≥ 2/engagement, zero script-caused TPKs.*
3. **P0-b3 · Cover solver** (M) — the engine's `bPartialCover` proves partial-cover
   awareness exists; extend it: sample candidate cover points from the cell's
   navgrid + collision (105 Havok classes), score by occlusion/line-of-sight/reload
   window/exposure time, drive peek-shoot cycles with per-combat-style cadence.
4. **P0-b4 · Per-faction combat styles** (M) — synthesize CSTY float sets per faction
   archetype (assault / sniper / heavy / support / suicidal) using the 112 decompiled
   CSTY float ranges as priors; ship as a **data mod** (ESM) so it's load-order safe.
   *Acceptance: minigun heavy never takes point-blank melee; snipers keep ≥ 30 m
   engagement distance ≥ 80% of the time.*
5. **P0-b5 · Anti-cheese & adaptive difficulty** (M) — detect camping (static player
   position + time), flanking response, and player accuracy/damage-output tracking;
   scale 3–4 fCombat GMSTs (flee threshold, target-LOD priority, ally-distance) within
   safe ranges; full transparency in `rcai_status` console output.
6. **P0-b6 · Papyrus API surface** (M) — expose RCAI state to scripts:
   `GetThreatLevel()`, `SetPerceptionProfile()`, `GetSquadRole()`,
   `OnRCAThreatChanged`-style events (the 167-event dispatch pattern already exists;
   F4SE can register new native functions against the VM's 1,669-function bridge).
7. **P0-b7 · AI benchmark harness** (S, first) — scripted combat scenarios generated
   from real cell data (the 3,828 PACKs tell us where combat happens); measure TTK,
   cover rate, spread, flank rate, ally-assist rate; every RCAI release must improve
   the scoreboard without regressing any line > 5%.

---

## 5. Pillar P1-a · Rendering & visual fidelity

**State of play (verified 2026):** the FO4 Next-Gen build runs a DX12 variant and
F4SE 0.7.x supports it; independent 2025 work proves a **DX12 proxy-chain FSR3
frame-generation** mod works on this exact engine, and a GamePlug D3D11 layer ships
FSR2/3, DLSS and XeSS3 upscaling with an F4SE bridge plugin. So the graphics layer is
no longer blocked — the question is integration quality, not feasibility.

Work items:

1. **P1-a1 · Upscaling & frame generation suite** (M) — integrate the GamePlug-class
   layer as a first-class part of the program (config-driven: FSR2/3, DLSS, XeSS3,
   DLAA, frame-gen on/off), exposed in a native F4SE settings UI rather than a
   separate .conf. *Acceptance: 4K/120 at native 720p render on RX 7800 class;
   no ghosting regression vs. stock TAA in the benchmark cells.*
2. **P1-a2 · Screen-space ray tracing (RTXGI-class)** (L) — real-time global
   illumination via screen-space radiance probes + ray-traced reflections where the
   precomputed-light-volume pipeline (the 1,212 display GMSTs describe its breadth)
   goes stale: dynamic lights, player flashlight, explosions. ENB-independent;
   ships as an F4SE render-hook plugin. *Acceptance: flashlight corners reveal GI
   bounce in ≤ 2 frames; +X% perf budget documented.*
3. **P1-a3 · Volumetrics & atmosphere** (M) — 3D volumetric fog, god rays through
   window geometry, weather-reactive haze; replaces the billboard-fog stack.
   *Acceptance: Commonwealth rain/fog reads as volume at 100 m, not as a gradient.*
4. **P1-a4 · Water, vegetation & surface modernization** (M) — water with
   refraction/transmission + shore foam (the Water INI section is already a first-class
   domain in the config dump); grass with proper culling + wind; foliage occlusion
   culling pass.
5. **P1-a5 · Color pipeline** (S–M) — filmic tonemap, ACES-class grading, HDR10
   output path, modern bloom (no 2015 double-bloom), contact shadows, GTA-class AO.
6. **P1-a6 · "FO4 Next" preset** (S) — ship everything as one opt-in config preset +
   an ENB/ReShade-compatible mode for users who want their own post chain.

**Out of scope (honesty gate):** a clean DX12/Vulkan engine port, true mesh-LOD
streaming rebuild, or replacing NetImmerse — those require engine source. The proxy
technique above achieves 2026 image quality *on top of* the stock renderer, which is
the pragmatic ceiling for a mod.

---

## 6. Pillar P1-b · Combat & damage overhaul ("Combat 2.0")

Substrate: 252 WEAP, 2,408 OMODs (the attachment system), 359 AVIFs (limb values
exist), 5 stagger GMSTs, 56 VATS GMSTs, 37 weapon GMSTs, 139 PROJ.

1. **P1-b1 · Ballistics model** (L) — per-weapon 2D recoil pattern, hip-fire spread
   curve by movement state, ADS sway, dynamic accuracy, muzzle-rise curves; expressed
   as OMOD-compatible data + a small F4SE hook on the fire event. *Acceptance:
   burst-fire accuracy matches the documented curves ±5% in the combat gauntlet.*
2. **P1-b2 · Material & penetration model** (L) — armor class per surface
   (fabric/leather/steel/plate) with deflection probability, armor-piercing as an
   OMOD keyword (the 2,408 existing mods prove the keyword mechanism),
   jacket-deformation rules for ammunition.
3. **P1-b3 · Damage & wound system** (M) — build on the limb actor values:
   directional knockback from impact vector (the 5 stagger GMSTs get 20× the
   resolution), bleeding/infection states, armor degradation, and
   weapon-mass-dependent stagger.
4. **P1-b4 · VATS rework → "Tactical Scan"** (M) — keep VATS (56 GMSTs of fan
   favourite) but reframe it: scan → target → predicted-hit probability with
   uncertainty, time-dilation blending instead of hard freeze, AP from stamina
   (1 action-point GMST cluster), and a "VATS Assist" mode as an accessibility feature.
5. **P1-b5 · Gunsmith 2.0** (M) — procedural mod generation from the decompiled
   base-stat distributions (252 weapons × 2,408 OMOD stats as priors): a balance
   generator that emits valid OMOD sets with guaranteed playtest ranges, shipped as
   a data mod.
6. **P1-b6 · Melee modernization** (S) — directional stagger, block/parry timing
   windows, weapon-length advantage model.

---

## 7. Pillar P2-a · Living world & simulation

Substrate: 699 factions (sparse static graph), 44 crime/bounty GMSTs, 45
dialogue/economy GMSTs, 42 sandbox GMSTs, 1,669 Papyrus natives for world events.

1. **P2-a1 · Faction memory & dynamic diplomacy** (L) — F4SE world-state layer:
   every crime, trade, and raid updates faction memory (per-settlement, with decay);
   the 356 zero-relation factions get synthesized relations from the archetype
   matrix (42-relation max as an upper bound from the real graph); diplomacy
   shifts propagate (if the Railroad and the Brotherhood turn hostile, their
   settlements' patrols change). *Acceptance: killing a settler in a Railroad-run
   settlement degrades Railroad faction attitude at ≥ 2 neighbouring settlements.*
2. **P2-a2 · Dynamic sandbox** (M) — time-of-day activity curves per settlement,
   noise propagation (gunfire attracts within a modeled radius), dynamic encounter
   pacing that reacts to player progress (the 42 sandbox GMSTs + PACK re-issuance —
   same machinery as RCAI).
3. **P2-a3 · Economy simulation** (M) — regional supply/demand per settlement,
   merchant stock rotation driven by world events, barter pressure; a lightweight
   60 Hz economic tick in F4SE with Papyrus export.
4. **P2-a4 · Dynamic events** (M) — caravans, raids, wandering traders,
   settlement attacks generated from a weighted event table; each event writes to
   faction memory (P2-a1) so the world accumulates a history the player can witness.
5. **P2-a5 · Anti-TPK & consequence** (S) — persistent consequences:
   dead NPCs stay dead for a configurable horizon, bounties scale with witnessed
   crimes (the 44 crime GMSTs), settlement reputation decays with neglect.

---

## 8. Pillar P2-b · UI/HUD, input & accessibility

Substrate: 263 Scaleform UI classes, the UI Papyrus bound script, 33
InputEnableLayer natives.

1. **P2-b1 · HUD modernization** (L) — F4SE hooks on the HUD layer: clean
   diegetic HUD, threat compass (from RCAI's threat model), dynamic objective
   tracker; optional "2026 HUD" theme.
2. **P2-b2 · PIP-BOSS overhaul** (M) — faction-heat layer on the map,
   event timeline, settlement management 2.0 (the Workshop INI domain is already
   first-class in the config dump).
3. **P2-b3 · Controller & input modernization** (M) — full remappable input via the
   F4SE input channel (the 0x32-key listener in the scaffold is the pattern),
   contextual button mapping, gyro optional.
4. **P2-b4 · Accessibility suite** (M) — UI scaling, colourblind modes
   (hit markers/VATS), text-to-speech toggle, subtitle enhancements,
   difficulty sliders that map onto the fCombat GMST ranges,
   one-handed gamepad presets. *Acceptance: every core loop completable on
   gamepad alone; colourblind filters pass a contrast audit on hit/VATS markers.*
5. **P2-b5 · QoL pass** (S) — fast-travel overhaul, inventory search/sort,
   container stack merging, auto-map-marker.

---

## 9. Pillar P2-c · Audio

Substrate: 55 Wwise audio classes, the Sound/SoundCategory/MusicType bound scripts.

1. **P2-c1 · Spatial audio upgrade** (M) — reverb-by-environment (cell-class
   driven), occlusion by geometry, engine/weapon noise models.
2. **P2-c2 · Dynamic music director** (M) — layer mixing by threat level
   (RCAI threat model is the input), settlement vs. wild ambience blending.
3. **P2-c3 · Voice & SFX modernization** (S) — crowd density scaling,
   distant-weapon acoustic delay, chem/sound-effect polish pass.

---

## 10. Pillar P0-c · Modding tooling & CI (meta — makes the whole program shippable)

The decompiled knowledge base is itself a tool. Productize it:

1. **P0-c1 · Balance linter** (S, ~1 wk) — CI job that runs `analyze_decompiled.py`
   + lint rules: weapon damage outside distribution, perks with unmet rank
   prerequisites, faction enemy-cycles, OMODs with missing keywords.
   Ships as `tools/lint_balance.py`.
2. **P0-c2 · Record diffing & patch export** (M) — diff any two record sets
   (e.g. stock vs. our balance mod) and emit Creation-Kit-patchable output.
3. **P0-c3 · F4SE dev kit** (M) — address-library-driven offset resolution
   (the `version-1-10-163-0.bin` is exactly the artifact F4SE plugins need),
   debug overlay, script profiler (P0-a1), save-regression runner.
4. **P0-c4 · Wabbajack manifest** (S) — one-click install of the whole program
   (F4SE + plugins + data mods + graphics presets), load-order verified by the linter.
5. **P0-c5 · Docs & onboarding** (S) — README, per-plugin docs, changelog discipline.

---

## 11. Milestones

| Milestone | Contents | Exit criteria |
|---|---|---|
| **M0 · Foundation** *(this repo, done)* | knowledge base extracted into repo; analyzer + insights; RCAI plugin scaffold; CI pipeline; this roadmap | `analyze_decompiled.py` green; RCAI builds under `build.yml`; insights.json committed |
| **M1 · RCAI: senses & triage** | P0-b7 harness, P0-b1 perception, threat model, console debug, P0-a1 profiler | perception battery passes; profiler reproduces hotspots; beta release |
| **M2 · RCAI: tactics** | P0-b2 behaviour layer, P0-b3 cover, P0-b4 faction styles, P0-b5 anti-cheese | AI scoreboard: cover ≥ 60%, flanking ≥ 2/engagement; public beta |
| **M3 · Performance suite** | P0-a2–a5 | +20% median FPS on benchmark list; zero uncaught crashes in 100 h soak |
| **M4 · Visual suite** | P1-a1…a6 | "FO4 Next" preset: 4K/120 target, GI-quality dynamic lighting, volumetrics |
| **M5 · Combat 2.0** | P1-b1…b6 | ballistics curves verified; combat gauntlet parity-or-better vs. stock at 120 Hz |
| **M6 · Living world** | P2-a1…a5 | faction memory + dynamic sandbox + economy in stable release |
| **M7 · UX & accessibility** | P2-b1…b5, P2-c1…c3 | accessibility audit passed; controller-first playthrough complete |

Suggested cadence: M1+M2 form one release ("RCAI"), M3+M4 the next ("FO4 Next:
Performance & Vision"), M5–M7 rolling.

---

## 12. Feasibility, risks, mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| F4SE is pinned to a game version (1.10.163 here) | Med | the address library is per-version and the phase-1 pipeline re-generates it on updates — the knowledge base is a versioned artifact, not a one-time dump |
| Hooking Papyrus/native = crash surface | High | P0-a5 watchdog + JSON crash dumps + 100 h soak gate per release; ship behind a config toggle that hard-disables each subsystem |
| Frame-gen ghosting on a 2015 renderer | Med | FSR3-class motion vectors from the proxy layer (proven by the 2025 DX12-proxy FO4 mod); frame-gen stays opt-in |
| AI that's *too* smart breaks balance/fun | High | adaptive difficulty clamped to decompiled GMST ranges (P0-b5 transparency in `rcai_status`); scoreboard regression gates |
| Scope creep across 8 pillars | High | milestone exit criteria above are numeric; every pillar work item is S/M/L with a named artifact |
| Data gaps in the decompiled set (e.g. WEAP→AMMO cross-refs unresolved, corrupted display names, keyword-noisy GMST buckets) | Low–Med | analyzer flags gaps explicitly (`data/insights.json`); phase-2 extractor re-run fixes are tracked as tooling items, not blockers — edids are intact |
| Legal | Low | personal-use research on a purchased copy; no game files redistributed (the JSON knowledge base is derived analysis, kept in-repo for research only); public releases ship mod code + data mods only, under Bethesda's modding terms |

---

## 13. Ecosystem context (verified September 2026)

* **F4SE 0.7.x** supports the Next-Gen (1.10.163) build; the Address Library mod is
  maintained (Nexus #47327). The 1.58 M-symbol library in this repo's dump is the
  same artifact class — our tooling re-derives it per version.
* **GamePlug D3D11 layer** (Nexus #106119) ships FSR1/2/3, DLSS, DLAA, XeSS3 upscaling
  + FSR3 frame generation for FO4 with an F4SE bridge plugin — proof that P1-a1
  is production-ready technique, not research.
* **DX12 proxy-chain FSR3 frame gen** for FO4 (Nov 2025, open-source) demonstrates
  the DX12 path on this exact engine version.
* **Condition System Framework (F4SE)** (Nexus #105673, active through mid-2026)
  ships a weapon fault/damage system on F4SE — direct precedent for P1-b2-style
  systems and a compatibility target.
* Stability staples (High FPS Physics Fix, Weapon Debris Crash Fix) already target
  F4SE — P0-a4 should ship as a replacement for the standalone physics fix so the
  program composes with, rather than duplicates, the ecosystem.

---

## 14. Repository map

```
falloutmod/
├── Decompiled Fallout 4.zip        # original decompiled knowledge base (as uploaded)
├── DecompiledMaster/               # extracted: data/*.json (21 datasets) + 4-phase pipeline scripts
├── DecompiledKnowledge/            # earlier extraction (5 overlapping datasets, kept for reference)
├── data/insights.json              # generated statistics (tools/analyze_decompiled.py)
├── docs/
│   ├── MODERNIZATION_ROADMAP.md    # ← this document
│   ├── DATA_INSIGHTS.md            # generated narrative of the knowledge base
│   └── DATA_REFERENCE.md           # inventory + data-quality caveats of every dataset
├── tools/
│   └── analyze_decompiled.py       # knowledge-base analyzer (stdlib only)
├── plugins/rcai/                   # RCAI F4SE plugin (M1 target)
│   ├── CMakeLists.txt
│   └── src/RCAI.cpp                # scaffold: config, tick loop, console commands, F3 debug key
├── config/RCAI.ini                 # RCAI settings (shipped with the plugin per CI)
├── CMakeLists.txt                  # root build (build.yml → build/Release/RCAI.dll)
└── build.yml                       # CI: MSVC + CMake, F4SE SDK fetch, release upload
```
