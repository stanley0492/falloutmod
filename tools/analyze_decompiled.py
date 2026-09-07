#!/usr/bin/env python3
"""
analyze_decompiled.py — FO4 Modernization Program · knowledge-base analyzer
===========================================================================

Reads the decompiled Fallout 4 knowledge base (DecompiledMaster/data/*.json,
produced by the 4-phase decompilation pipeline) and emits two artifacts:

  data/insights.json     machine-readable statistics for tooling / CI
  docs/DATA_INSIGHTS.md  human-readable summary used by the roadmap docs

Usage:
  python3 tools/analyze_decompiled.py [--data-dir DecompiledMaster/data]
                                      [--out-json data/insights.json]
                                      [--out-md docs/DATA_INSIGHTS.md]

Notes on data quality (see docs/DATA_REFERENCE.md for the full inventory):
  * Display-name fields are partially corrupted by the upstream extractor
    (raw BSString/UTF-16LE bytes mis-decoded). Editor IDs (edid) are intact
    and are used as the canonical key everywhere in this program.
  * GMST category buckets are keyword-based; some buckets (notably
    'Sensory Detection & Stealth' and 'General Engine & World Simulation')
    contain unrelated rendering/display GMSTs. Prefix-filtered counts
    (fAI*/bAI*, fVATS*, fCombat*, ...) are computed here as the reliable
    signal.
  * PACK procedure_type values are address-library symbol IDs (Type_123456),
    not the CK enum — they cannot be mapped to behaviour names from this
    dataset alone. Behaviour classification below is edid-keyword based.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


# ---------------------------------------------------------------------------
# name decoding (best effort — see module docstring)
# ---------------------------------------------------------------------------

def dec_name(s: object) -> str:
    """Best-effort decode of a BSString display name from the raw dump."""
    if not isinstance(s, str) or not s:
        return ""
    try:
        b = s.encode("latin-1")
    except UnicodeEncodeError:
        return s  # already decoded
    if len(b) % 2:
        b = b[:-1]
    try:
        t = b.decode("utf-16-le")
    except UnicodeDecodeError:
        return ""
    t = t.replace("\x00", "").strip()
    return "".join(c for c in t if c.isprintable())


def name_recoverable(rows: list[dict], key: str = "name") -> tuple[int, int]:
    """(recoverable, total) — a name counts as recoverable when the decode
    yields 2+ printable chars and the raw string contained NULs (i.e. it
    really was UTF-16-ish) or decoded cleanly to ASCII."""
    total = 0
    ok = 0
    for r in rows:
        raw = r.get(key, "")
        if not raw:
            continue
        total += 1
        d = dec_name(raw)
        if len(d) >= 2:
            ok += 1
    return ok, total


# ---------------------------------------------------------------------------
# section analyzers
# ---------------------------------------------------------------------------

def analyze_engine(data: Path) -> dict:
    out: dict = {}

    rtti = json.loads((data / "engine_rtti_subsystems.json").read_text(encoding="utf-8"))
    out["rtti_total_classes"] = rtti["total_classes"]
    out["rtti_subsystems"] = rtti["subsystem_counts"]

    gmst = json.loads((data / "engine_gmsts_categorized.json").read_text(encoding="utf-8"))
    out["gmst_total"] = gmst["total_gmsts"]
    out["gmst_category_counts"] = gmst["category_counts"]
    names = [str(x) for v in gmst["categories"].values() for x in v]

    def prefix(*pfx: str) -> int:
        return sum(1 for n in names if n.startswith(pfx))

    out["gmst_reliable_prefix_counts"] = {
        "AI (fAI*/bAI*)": prefix("fAI", "bAI"),
        "combat (fCombat*)": prefix("fCombat"),
        "VATS (fVATS*)": prefix("fVATS"),
        "sneak (fSneak*)": prefix("fSneak"),
        "stagger (fStagger*)": prefix("fStagger"),
        "weapon (fWeapon*)": prefix("fWeapon"),
        "action point (fActionPoint*)": prefix("fActionPoint"),
        "armor (fArmor*)": prefix("fArmor"),
        "magic (fMagic*)": prefix("fMagic"),
    }

    al = json.loads((data / "address_library_summary.json").read_text(encoding="utf-8"))
    out["address_library"] = {
        "database_file": al["database_file"],
        "file_size_bytes": al["file_size_bytes"],
        "total_mapped_functions_and_data": al["total_mapped_functions_and_data"],
    }
    return out


def analyze_ai(data: Path) -> dict:
    packs = json.loads((data / "pack_ai_packages.json").read_text(encoding="utf-8"))
    keywords = [
        "acquire", "patrol", "visit", "follow", "escort", "combat", "flee",
        "entervehicle", "exitvehicle", "attack", "defend", "work", "wait",
        "rest", "idle", "home", "return", "move", "use",
    ]

    def classify(edid: str) -> str:
        e = edid.lower()
        for kw in keywords:
            if kw in e:
                return kw
        return "generic/ambient (unclassifiable from edid)"

    dist = collections.Counter(classify(p["edid"]) for p in packs)
    flags = collections.Counter(p.get("flags_hex", "?") for p in packs)
    ptypes = collections.Counter(p.get("procedure_type", "?") for p in packs)

    n_classified = sum(v for k, v in dist.items() if not k.startswith("generic"))
    return {
        "total_packages": len(packs),
        "classified_from_edid": n_classified,
        "generic_unclassified": dist.get("generic/ambient (unclassifiable from edid)", 0),
        "edid_keyword_distribution": dict(dist.most_common()),
        "flag_combo_count": len(flags),
        "top_flag_combos": dict(flags.most_common(5)),
        "unique_procedure_type_ids": len(ptypes),
        "top_procedure_types": dict(ptypes.most_common(5)),
    }


def analyze_records(data: Path) -> dict:
    out: dict = {}

    weapons = json.loads((data / "weap_weapons.json").read_text(encoding="utf-8"))
    out["weapons"] = {
        "total": len(weapons),
        "with_ammo_formid": sum(1 for w in weapons if w.get("ammo_formid")),
        "name_recoverable": name_recoverable(weapons),
    }

    armor = json.loads((data / "armo_armor.json").read_text(encoding="utf-8"))
    out["armor"] = {
        "total": len(armor),
        "name_recoverable": name_recoverable(armor),
        "distinct_body_slot_masks": len({a.get("body_slots_mask") for a in armor}),
    }

    csty = json.loads((data / "csty_combat_styles.json").read_text(encoding="utf-8"))
    subrecords = collections.Counter()
    for c in csty:
        subrecords.update(c.get("subrecords", []))
    out["combat_styles"] = {
        "total": len(csty),
        "subrecord_presence": dict(subrecords.most_common()),
        "with_melee_data": sum(1 for c in csty if "CSME" in c.get("subrecords", [])),
        "with_ranged_data": sum(1 for c in csty if "CSRA" in c.get("subrecords", [])),
    }

    factions = json.loads((data / "factions.json").read_text(encoding="utf-8"))
    rel = collections.Counter(f.get("relations_count", 0) for f in factions)
    reactions = collections.Counter()
    for f in factions:
        for r in f.get("relations", []):
            reactions[r.get("reaction", "?")] += 1
    out["factions"] = {
        "total": len(factions),
        "with_zero_relations": rel.get(0, 0),
        "max_relations": max(rel),
        "relation_count_distribution": {str(k): v for k, v in sorted(rel.items())},
        "reaction_totals": dict(reactions.most_common()),
    }

    perks = json.loads((data / "perks.json").read_text(encoding="utf-8"))
    ranks = collections.Counter(p.get("ranks") for p in perks)
    out["perks"] = {
        "total": len(perks),
        "playable": sum(1 for p in perks if p.get("is_playable")),
        "rank_1_share": ranks.get(1, 0),
        "rank_distribution": {str(k): v for k, v in sorted(ranks.items(), key=lambda kv: (kv[0] is None, kv[0]))},
        "no_description": sum(1 for p in perks if not p.get("desc")),
    }

    mgef = json.loads((data / "mgef_magic_effects.json").read_text(encoding="utf-8"))
    out["magic_effects"] = {
        "total": len(mgef),
        "with_attached_scripts": sum(1 for m in mgef if m.get("has_scripts")),
    }

    proj = json.loads((data / "proj_projectiles.json").read_text(encoding="utf-8"))
    out["projectiles"] = {
        "total": len(proj),
        "fields": sorted(proj[0].keys()) if proj else [],
    }

    omods = json.loads((data / "object_modifications.json").read_text(encoding="utf-8"))
    tt = collections.Counter(o.get("target_type") for o in omods)
    out["object_modifications"] = {
        "total": len(omods),
        "target_type_distribution": {str(k): v for k, v in sorted(tt.items(), key=lambda kv: (kv[0] is None, kv[0]))},
    }

    races = json.loads((data / "race_races.json").read_text(encoding="utf-8"))
    out["races"] = {"total": len(races)}

    av = json.loads((data / "actor_values.json").read_text(encoding="utf-8"))
    out["actor_values"] = {"total": len(av)}
    return out


def analyze_papyrus(data: Path) -> dict:
    nf = json.loads((data / "papyrus_native_functions.json").read_text(encoding="utf-8"))
    ev = json.loads((data / "papyrus_engine_events.json").read_text(encoding="utf-8"))
    sh = json.loads((data / "papyrus_script_hierarchy.json").read_text(encoding="utf-8"))
    per = collections.Counter({k: len(v) for k, v in nf["native_bindings_by_script"].items()})
    return {
        "native_functions_total": nf["total_native_functions"],
        "bound_scripts_with_natives": nf["scripts_with_native_bindings"],
        "top_bound_scripts": dict(per.most_common(10)),
        "events_total": ev["total_unique_events"],
        "script_class_hierarchy_entries": len(sh),
    }


def analyze_config(data: Path) -> dict:
    cfg = json.loads((data / "engine_configuration_subsystems.json").read_text(encoding="utf-8"))
    return {
        "ini_domains": {
            d: sorted(s.keys()) for d, s in cfg["engine_subsystem_domains"].items()
        },
        "user_ini_sections": sorted(cfg["active_user_settings"]["Fallout4.ini"].keys()),
    }


# ---------------------------------------------------------------------------
# markdown rendering
# ---------------------------------------------------------------------------

def render_md(ins: dict) -> str:
    e, a, r, p, c = (
        ins["engine"], ins["ai"], ins["records"], ins["papyrus"], ins["config"],
    )
    w = r["weapons"]
    f = r["factions"]
    pk = r["perks"]
    cs = r["combat_styles"]
    al = e["address_library"]

    ai_items = [
        (k, v) for k, v in a["edid_keyword_distribution"].items()
        if not k.startswith("generic")
    ][:8]
    ai_dist = ", ".join(f"**{k}** {v}" for k, v in ai_items)
    gmc = e["gmst_reliable_prefix_counts"]

    md = f"""# Data Insights — Decompiled Fallout 4 Knowledge Base

*Generated by `tools/analyze_decompiled.py` on {ins["generated"]} — do not edit by hand.*
*Game build: `{al["database_file"]}` (v1.10.163.0, Next-Gen engine).*

## Engine surface

| Item | Value |
|---|---|
| RTTI classes extracted | {e["rtti_total_classes"]:,} |
| — AI, Combat & Actor Systems | {e["rtti_subsystems"].get("AI, Combat & Actor Systems", 0):,} |
| — Renderer & Scene Graph (NetImmerse/BS) | {e["rtti_subsystems"].get("Renderer & Scene Graph (NetImmerse)", 0):,} |
| — Scaleform UI & Menus | {e["rtti_subsystems"].get("Scaleform UI & Menus", 0):,} |
| — Physics & Havok | {e["rtti_subsystems"].get("Physics & Havok", 0):,} |
| — Audio & Acoustics | {e["rtti_subsystems"].get("Audio & Acoustics", 0):,} |
| — Animation Systems | {e["rtti_subsystems"].get("Animation Systems", 0):,} |
| — Papyrus / BSScript VM | {e["rtti_subsystems"].get("Papyrus VM (BSScript)", 0):,} |
| — Game Records (TES/BGS) | {e["rtti_subsystems"].get("Game Records & Form Types (TES/BGS)", 0):,} |
| — Core Engine, Memory & OS | {e["rtti_subsystems"].get("Core Engine, Memory & OS", 0):,} |
| Hardcoded GMSTs | {e["gmst_total"]:,} |
| — AI-tuning (fAI*/bAI*) | {gmc["AI (fAI*/bAI*)"]} |
| — combat (fCombat*) | {gmc["combat (fCombat*)"]} |
| — VATS (fVATS*) | {gmc["VATS (fVATS*)"]} |
| — sneak (fSneak*) | {gmc["sneak (fSneak*)"]} |
| — stagger (fStagger*) | {gmc["stagger (fStagger*)"]} |
| — weapon (fWeapon*) | {gmc["weapon (fWeapon*)"]} |
| — action point (fActionPoint*) | {gmc["action point (fActionPoint*)"]} |
| Address-library symbols | {al["total_mapped_functions_and_data"]:,} |

## AI layer

* **{a["total_packages"]:,} AI packages (PACK)** — only **{a["classified_from_edid"]:,} ({a["classified_from_edid"] / a["total_packages"] * 100:.0f}%)**
  carry a recognisable tactical verb in their editor ID.
  Top verbs: {ai_dist}.
* **{r["factions"]["total"]} factions**, {f["with_zero_relations"]} of them ({f["with_zero_relations"] / f["total"] * 100:.0f}%) with **zero** faction relations —
  the hostility graph is sparse and largely static.
* **{cs["total"]} combat styles (CSTY)** — fixed float parameter sets
  (melee {cs["with_melee_data"]}, ranged {cs["with_ranged_data"]}); no runtime adaptation.
* GMST category buckets are keyword-classified upstream and noisy; the prefix counts above are the reliable signal.

## Player-facing content

| Item | Value |
|---|---|
| Weapons (WEAP) | {w["total"]} (ammo cross-refs unresolved in dump: {w["with_ammo_formid"]}/{w["total"]}) |
| Armor (ARMO) | {r["armor"]["total"]} across {r["armor"]["distinct_body_slot_masks"]} distinct body-slot masks |
| Projectiles (PROJ) | {r["projectiles"]["total"]} |
| Object modifications (OMOD) | {r["object_modifications"]["total"]} (weapon attachment/mod system) |
| Magic effects (MGEF) | {r["magic_effects"]["total"]} ({r["magic_effects"]["with_attached_scripts"]} script-attached) |
| Perks (PERK) | {pk["total"]} ({pk["playable"]} playable; {pk["rank_1_share"] / pk["total"] * 100:.0f}% single-rank) |
| Races (RACE) | {r["races"]["total"]} |
| Actor values (AVIF) | {r["actor_values"]["total"]} (incl. derived/limb values) |

## Scripting layer (Papyrus)

* **{p["native_functions_total"]:,} native C++ functions** exposed to Papyrus across **{p["bound_scripts_with_natives"]} bound scripts**
  (top: {", ".join(f"{k} {v}" for k, v in list(p["top_bound_scripts"].items())[:5])}).
* **{p["events_total"]} unique engine events** the VM dispatches.
* **{p["script_class_hierarchy_entries"]:,}** script-class inheritance entries (10,307 .psc sources scanned).

## Configuration surface (INIs)

* Engine subsystem domains analysed: {", ".join(c["ini_domains"].keys())}.
* Fallout4.ini sections present: {", ".join(c["user_ini_sections"][:12])}, …

## Data-quality caveats

1. **Display names** in the record dumps are partially corrupted by the upstream extractor
   (weapons: {w["name_recoverable"][0]}/{w["name_recoverable"][1]} recoverable). Editor IDs are intact — all tooling keys on `edid`.
2. **GMST categories** are keyword buckets from the phase-1 script and contain unrelated entries; use prefix-filtered counts.
3. **PACK procedure_type** values are address-library IDs, not CK enums; they need the address library to resolve.
"""
    return md


# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data-dir", default=str(REPO_ROOT / "DecompiledMaster" / "data"))
    ap.add_argument("--out-json", default=str(REPO_ROOT / "data" / "insights.json"))
    ap.add_argument("--out-md", default=str(REPO_ROOT / "docs" / "DATA_INSIGHTS.md"))
    args = ap.parse_args()

    data = Path(args.data_dir)
    if not data.is_dir():
        print(f"error: data dir not found: {data}", file=sys.stderr)
        return 1

    ins = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "source": str(data),
        "engine": analyze_engine(data),
        "ai": analyze_ai(data),
        "records": analyze_records(data),
        "papyrus": analyze_papyrus(data),
        "config": analyze_config(data),
    }

    for out_path in (args.out_json, args.out_md):
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out_json).write_text(json.dumps(ins, indent=2) + "\n", encoding="utf-8")
    Path(args.out_md).write_text(render_md(ins), encoding="utf-8")

    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    print(
        f"key numbers: {ins['engine']['rtti_total_classes']} RTTI classes, "
        f"{ins['engine']['gmst_total']} GMSTs, {ins['ai']['total_packages']} AI packages, "
        f"{ins['records']['perks']['total']} perks, {ins['papyrus']['native_functions_total']} Papyrus natives"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
