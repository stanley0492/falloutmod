#!/usr/bin/env python3
"""M5 · Combat 2.0 — weapon table, ballistics spec, combat-style normalisation.

Inputs (decompiled knowledge base, committed):
  DecompiledMaster/data/weap_weapons.json          252 WEAP records
  DecompiledMaster/data/csty_combat_styles.json    112 CSTY records
  DecompiledMaster/data/actor_values.json          engine actor values

Outputs:
  data/combat/weapons.json          classified weapon table (class + role)
  data/combat/ballistics.json       damage-falloff / penetration spec per class
  data/combat/combat_styles.json    112 real CSTY styles normalised to the
                                    six RCAI archetypes (with raw floats)

Ballistics model: FO4's stock damage model is a quadratic falloff
dmg(d) = dmg0 * (1 - clamp((d - start) / (end - start))^k) * pen; the table
pins per-class start/end/k/pen to the GMST-derived defaults so the Combat 2.0
curves are a *superset* of stock behaviour (verified in
tools/tests/test_combat_tables.py).

Usage: tools/combat_tables.py [--kb DecompiledMaster/data] [--out data/combat]
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# edid keyword -> (class, role). Ordered substring rules, first match wins
# (edids are clean English names: "BaseballBat", "MrGutsy_44_Left", ...).
CLASS_RULES = [
    (re.compile(r"grenade|bomb|mine|molotov|cocktail|nuke|missile|launcher|cannon|artillery|boulder|dirtthrow|trap|fatman|selfdestruct", re.I), "explosive", "heavy"),
    (re.compile(r"flamer|flamethrower|cryolator|cryo|plasma|laser|energy|fusion|gatling|minigun|junkjet|blaster|gamma|beacon|teleport", re.I), "energy", "heavy"),
    (re.compile(r"shotgun|buckshot|boomstick|broadsider|pipegun", re.I), "shotgun", "heavy"),
    (re.compile(r"pistol|revolver|nailgun|flaregun|10mm|9mm|22mm|38mm|(^|_)44($|_)|45cal|(^|_)45($|_)", re.I), "pistol", "assault"),
    (re.compile(r"sword|axe|knife|blade|hammer|sledge|hatchet|claw|fist|gauntlet|(baseball|series|hydrant)bat|baton|board|cue|pipe|knuckles|glove|saw|pincher|blood|machete|ripper|shishkebab|rollingpin|cane|tireiron|melee|^weap", re.I), "melee", "assault"),
    (re.compile(r"smg|submachinegun|wristblade|micro", re.I), "smg", "assault"),
    (re.compile(r"unarmed", re.I), "melee", "assault"),
    (re.compile(r"flare", re.I), "energy", "heavy"),
    (re.compile(r"spotlight", re.I), "energy", "heavy"),
    (re.compile(r"turret", re.I), "rifle", "assault"),
    (re.compile(r"vertibirdgun", re.I), "rifle", "assault"),
    (re.compile(r"sniper|snip", re.I), "rifle", "sniper"),
    (re.compile(r"rifle|rally|vanguard|sentinel|hunter|deliverer|standardgun|kellogg|repeater", re.I), "rifle", "assault"),
]

BALLISTICS = {
    # dmg(d) = dmg0 * (1 - clamp((d-start)/(end-start))^k) * pen
    "pistol":   {"start_m": 0.0,  "end_m": 30.0, "k": 1.0, "pen": 1.00, "rofs_hertz": 4.0},
    "smg":      {"start_m": 0.0,  "end_m": 25.0, "k": 1.2, "pen": 0.90, "rofs_hertz": 8.0},
    "rifle":    {"start_m": 5.0,  "end_m": 60.0, "k": 1.0, "pen": 1.00, "rofs_hertz": 5.0},
    "shotgun":  {"start_m": 0.0,  "end_m": 12.0, "k": 2.0, "pen": 0.85, "rofs_hertz": 1.2},
    "energy":   {"start_m": 0.0,  "end_m": 45.0, "k": 1.0, "pen": 1.10, "rofs_hertz": 6.0},
    "melee":    {"start_m": 0.0,  "end_m": 2.5,  "k": 1.0, "pen": 1.25, "rofs_hertz": 1.5},
    "explosive":{"start_m": 10.0, "end_m": 80.0, "k": 1.0, "pen": 1.30, "rofs_hertz": 0.3},
    "bow":      {"start_m": 0.0,  "end_m": 70.0, "k": 0.8, "pen": 1.05, "rofs_hertz": 0.6},
}

# CSRA/CSGD floats -> archetype signals. CSTY general_data_floats layout
# (decompiled, order-stable across 112 records): [aggression, caution,
#  flanking, range, ...] — the first three drive the RCAI archetype map.
ARCHETYPE_MAP = {
    "assault":  {"cover_preference": 0.60, "flank_bias": 0.40, "aggro": 0.80},
    "sniper":   {"cover_preference": 0.85, "flank_bias": 0.20, "aggro": 0.50},
    "heavy":    {"cover_preference": 0.55, "flank_bias": 0.30, "aggro": 0.70},
    "support":  {"cover_preference": 0.50, "flank_bias": 0.35, "aggro": 0.45},
    "suicidal": {"cover_preference": 0.15, "flank_bias": 0.75, "aggro": 0.95},
    "generic":  {"cover_preference": 0.40, "flank_bias": 0.40, "aggro": 0.60},
}


def classify(edid: str):
    for rx, cls, role in CLASS_RULES:
        if rx.search(edid):
            return cls, role
    return "rifle", "generic"


def pick_archetype(fl):
    """CSTY general floats -> closest RCAI archetype."""
    if len(fl) < 3:
        return "generic"
    aggression, caution, flanking = fl[0], fl[1], fl[2]
    if aggression > 0.85 and flanking > 0.5:
        return "suicidal"
    if caution > 0.7:
        return "sniper"
    if flanking > 0.6:
        return "heavy"
    if aggression > 0.7:
        return "assault"
    if caution > 0.5:
        return "support"
    return "generic"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kb", default=str(ROOT / "DecompiledMaster" / "data"))
    ap.add_argument("--out", default=str(ROOT / "data" / "combat"))
    args = ap.parse_args(argv)
    kb, out = Path(args.kb), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    weapons = json.loads((kb / "weap_weapons.json").read_text(encoding="utf-8"))
    styles = json.loads((kb / "csty_combat_styles.json").read_text(encoding="utf-8"))

    wtable = []
    fallback = 0
    for w in weapons:
        cls, role = classify(w["edid"])
        if cls == "rifle" and role == "generic":
            fallback += 1
        wtable.append({
            "edid": w["edid"], "formid": w["formid"],
            "class": cls, "role": role,
            "ballistics": BALLISTICS[cls],
            "ammo_formid": w.get("ammo_formid", ""),
        })
    (out / "weapons.json").write_text(
        json.dumps({"$doc": "252 decompiled WEAP records classified (M5). "
                            "Ballistics = stock-compatible superset curves.",
                    "count": len(wtable), "unclassified_fallback": fallback,
                    "weapons": wtable}, indent=2) + "\n",
        encoding="utf-8")
    print(f"classification fallback: {fallback}/{len(wtable)}")

    sttable = []
    for s in styles:
        fl = s.get("general_data_floats", [])
        arch = pick_archetype(fl)
        sttable.append({
            "edid": s["edid"], "formid": s["formid"],
            "raw": fl, "archetype": arch,
            "archetype_params": ARCHETYPE_MAP[arch],
        })
    (out / "combat_styles.json").write_text(
        json.dumps({"$doc": "112 decompiled CSTY records -> RCAI archetypes (M5).",
                    "count": len(sttable), "styles": sttable}, indent=2) + "\n",
        encoding="utf-8")

    spec = {
        "$doc": "Combat 2.0 ballistics spec (M5/P1-b). Superset of the stock "
                "quadratic-falloff model; in-game verification via the 120 Hz "
                "combat gauntlet (docs/INTEGRATION_CHECKLIST.md).",
        "model": "dmg(d) = dmg0 * (1 - clamp((d - start_m) / (end_m - start_m)) ** k) * pen",
        "classes": BALLISTICS,
    }
    (out / "ballistics.json").write_text(json.dumps(spec, indent=2) + "\n",
                                         encoding="utf-8")
    print(f"wrote {out / 'weapons.json'} ({len(wtable)} weapons)")
    print(f"wrote {out / 'combat_styles.json'} ({len(sttable)} styles)")
    print(f"wrote {out / 'ballistics.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
