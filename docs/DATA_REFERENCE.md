# Data Reference — Decompiled Fallout 4 Knowledge Base

Game build: **1.10.163.0 (Next-Gen)** — `version-1-10-163-0.bin` address library (25.3 MB,
1,582,975 mapped functions & data).

Two extractions are in the repo:

* `DecompiledMaster/` — the complete set: 21 JSON datasets + the 4 pipeline scripts.
* `DecompiledKnowledge/` — an earlier, overlapping 5-file extraction (kept for reference;
  `DecompiledMaster/data/` supersedes it).

## 1. How the data was produced (4-phase pipeline, `DecompiledMaster/decompile_phase*.py`)

| Phase | Script | Input | Output |
|---|---|---|---|
| 1 | `decompile_phase1_engine.py` | `Fallout4.exe` (binary scan), address library `.bin` | RTTI classes (4,097), GMSTs (3,621), address-library summary |
| 2 | `decompile_phase2_records.py` | `Fallout4.esm` via the `esplib` record parser | all game-record datasets (CSTY, PACK, FACT, AVIF, PERK, WEAP, ARMO, PROJ, OMOD, RACE, MGEF) |
| 3 | `decompile_phase3_papyrus.py` | 10,307 `.psc` sources | native-function registry, event registry, script inheritance tree |
| 4 | `decompile_phase4_configuration.py` | player + default INIs | config-subsystem map (Display, Havok, Papyrus VM, streaming, archive) |

Note: the phase scripts hard-code absolute `G:\` / `C:\` paths from the original
machine; the JSON outputs are the portable artifact. Re-running requires the same
`esplib` helper and a matching game install.

## 2. Dataset inventory

| File | Records | Key fields | Notes |
|---|---|---|---|
| `engine_rtti_subsystems.json` | 4,097 classes | subsystem → class list | keyword-classified; the reliable map of what's hookable |
| `engine_classes.json` | 4,097 classes | subsystem → class list | same data, alternate subsystem labels |
| `engine_gmsts_categorized.json` | 3,621 GMSTs | 11 categories → names | **keyword buckets are noisy** (see caveats); use prefix filtering |
| `address_library_summary.json` | 20 samples | id → offset | full library is the 25.3 MB `.bin`; summary only |
| `pack_ai_packages.json` | 3,828 | edid, procedure_type, flags | procedure_type = address-lib ID (`Type_*`), not the CK enum |
| `csty_combat_styles.json` | 112 | subrecords, general_data_floats | all 112 have CSGD/CSME/CSRA/CSCR/CSLR/CSCV/CSFL/DATA |
| `factions.json` | 699 | relations (target formid, modifier, reaction) | reaction vocabulary: Ally/Friend/Enemy/Neutral |
| `actor_values.json` | 359 | edid, formid, name, abbr | incl. derived values (limb damage conditions, speed scaling) |
| `perks.json` | 538 | ranks, min_level, is_playable, entry_count | 323 playable; 71% single-rank |
| `weap_weapons.json` | 252 | edid, ammo_formid, name, desc | **ammo_formid empty in every record** (unresolved cross-ref) |
| `armo_armor.json` | 682 | body_slots_mask | 73 distinct slot masks |
| `proj_projectiles.json` | 139 | speed, gravity_mult, range | numeric fields present; values partially zeroed in dump |
| `object_modifications.json` | 2,408 | target_type | the weapon-attachment/mod system; target_type 0 dominates (2,033) |
| `mgef_magic_effects.json` | 638 | has_scripts | 156 script-attached |
| `race_races.json` | 45 | body_parts_count | |
| `papyrus_native_functions.json` | 1,669 functions | per-script binding lists | 60 bound scripts; top: ObjectReference 419, Actor 384, Game 207 |
| `papyrus_engine_events.json` | 167 events | signatures | every VM-dispatched event the engine exposes |
| `papyrus_script_hierarchy.json` | 10,260 entries | script → base | the full `extends` tree |
| `engine_configuration_subsystems.json` | — | per-domain INI keys | Papyrus VM budgets, Havok timestep, cell streaming, archive |

## 3. Data-quality caveats (important for anyone consuming these files)

1. **Display names are partially corrupted.** The extractor mis-decoded BSString
   (UTF-16LE) fields: e.g. weapons show `Z2\x02` instead of a readable name, and
   `tools/analyze_decompiled.py` recovers 166/226 weapon names and 317/477 armor names.
   **Always key on `edid` / `formid`, never on `name`.** A phase-2 re-run with a
   corrected `get_str` (BSString-aware, flag `FLAG_LOCALIZED` handled) would fix this.
2. **GMST category buckets are keyword-classified** by the phase-1 script, so
   "Sensory Detection & Stealth" contains lighting GMSTs (`bAmbientLight`, …) and
   "General Engine & World Simulation" is a 1,212-entry catch-all. Prefix-filtered
   counts (fAI*/bAI*, fCombat*, fVATS*, …) are the reliable signal and are what
   `tools/analyze_decompiled.py` reports.
3. **PACK procedure_type values are address-library symbol IDs** (`Type_131090`, …),
   not the Creation-Kit procedure enum. Resolving them to names requires walking the
   full 25.3 MB address library (a tooling task, not a data error).
4. **WEAP → AMMO cross-references are unresolved** (`ammo_formid` is empty for all
   252 weapons) — a known gap in the phase-2 extractor's field mapping.
5. **PROJ numeric fields are partially zeroed** (speed/range/gravity all 0 in the
   dump) — same class of extractor gap.
6. The **`DecompiledKnowledge/`** copy predates `DecompiledMaster/data/`; where they
   overlap, prefer `DecompiledMaster`.

None of the caveats block the roadmap: every pillar's work item cites either an intact
field (edids, formids, relations, flags, GMST names, Papyrus registries) or a re-run of
the pipeline, which is a bounded tooling task.
