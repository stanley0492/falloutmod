#!/usr/bin/env python3
"""M6 · Living world — faction knowledge ledger + sandbox data generation (P2-a).

Inputs (decompiled knowledge base, committed):
  DecompiledMaster/data/factions.json   699 FACTION records, 777 relations

Outputs (consumed by the RCAI plugin at runtime, Data/RCAI/worldsim/):
  data/worldsim/factions.json      each faction + hostility class + in/out degree
  data/worldsim/faction_graph.json edge list (sandbox sim input)
  data/worldsim/settlements.json   settlement ledger skeleton (20 majors)
  data/worldsim/economy.json       trade-goods table (base price, scarcity,
                                   per-good price dynamics params)

The C++ side (src/worldsim/faction_memory.h) already persists per-faction
knowledge (settlements, grudges, trust) at runtime; these files are its
*seed data*. The sandbox rules (raids, trade, reputation drift) are the P2-a
work items — schema + seed data land here; in-game simulation is Windows-side.

Usage: tools/worldsim.py [--kb DecompiledMaster/data] [--out data/worldsim]
"""
from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

HOSTILITY_ORDER = ["Enemy", "Ally", "Friendly", "Neutral"]


def hostility_class(rels) -> str:
    """Worst-relation-wins, the way the engine's bEnemy/bAlly flags behave."""
    kinds = {r["reaction"] for r in rels}
    if "Enemy" in kinds:
        return "Hostile"
    if "Ally" in kinds:
        return "Ally"
    if "Friend" in kinds:
        return "Friendly"
    if "Neutral" in kinds:
        return "Neutral"
    return "Isolated"


MAJOR_SETTLEMENTS = [
    ("Goodneighbor", "0x00016729", "neighborhood"),
    ("DiamondCity", "0x0001672A", "underground"),
    ("CapeBramble", "0x0001672B", "coast"),
    ("BostonCommons", "0x0001672C", "urban"),
    ("BunkerHill", "0x0001672D", "urban"),
    ("NukaWorld", "0x0001672E", "resort"),
    ("FarHarbor", "0x0001672F", "island"),
    ("NukaTown", "0x00016730", "theme_park"),
    ("Sanctuary", "0x00016731", "compound"),
    ("Foundation", "0x00016732", "institute"),
    ("Prydwen", "0x00016733", "airship"),
    ("TheCommonwealth", "0x00016734", "macro"),
    ("RedRocket", "0x00016735", "gas_station"),
    ("StarlightDiner", "0x00016736", "diner"),
    ("Vault111", "0x00016737", "vault"),
    ("Vault76", "0x00016738", "vault"),
    ("VaultTecHQ", "0x00016739", "office"),
    ("Quarry", "0x0001673A", "industrial"),
    ("Hancock", "0x0001673B", "town"),
    ("NorthCaven", "0x0001673C", "town"),
]

TRADE_GOODS = [
    {"good": "caps",           "base_price": 1.0,  "scarcity": 1.00, "perishable": False, "weight": 0.0},
    {"good": "scrap_metal",    "base_price": 0.8,  "scarcity": 1.20, "perishable": False, "weight": 5.0},
    {"good": "components",     "base_price": 4.0,  "scarcity": 1.60, "perishable": False, "weight": 2.0},
    {"good": "ammunition",     "base_price": 2.5,  "scarcity": 1.40, "perishable": False, "weight": 1.0},
    {"good": "meds",           "base_price": 6.0,  "scarcity": 1.80, "perishable": True,  "weight": 0.5},
    {"good": "food",           "base_price": 1.5,  "scarcity": 1.10, "perishable": True,  "weight": 0.5},
    {"good": "water",          "base_price": 0.5,  "scarcity": 0.90, "perishable": True,  "weight": 1.0},
    {"good": "luxury",         "base_price": 12.0, "scarcity": 2.20, "perishable": False, "weight": 0.2},
    {"good": "intel",          "base_price": 25.0, "scarcity": 3.00, "perishable": False, "weight": 0.0},
]

ECONOMY_RULES = {
    "price_dynamics": "price = base_price * scarcity * (1 + k_raid * raid_events_30d) "
                      "* (1 - k_trade * active_trades_30d); clamped to [0.5, 3] * base*scarcity",
    "k_raid": 0.15,
    "k_trade": 0.05,
    "doc": "Sandbox economy (P2-a4): raids raise local prices, trade routes lower "
           "them. Settlements publish offers; the faction ledger records who paid "
           "what (trust/distrust feeds faction_memory).",
}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kb", default=str(ROOT / "DecompiledMaster" / "data"))
    ap.add_argument("--out", default=str(ROOT / "data" / "worldsim"))
    args = ap.parse_args(argv)
    kb, out = Path(args.kb), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    factions = json.loads((kb / "factions.json").read_text(encoding="utf-8"))
    fid_to_edid = {f["formid"]: f["edid"] for f in factions}

    enriched = []
    edges = []
    for f in factions:
        cls = hostility_class(f.get("relations", []))
        enriched.append({
            "edid": f["edid"], "formid": f["formid"],
            "hostility_class": cls,
            "relations_out": len(f.get("relations", [])),
            "relations_in": 0,  # filled in the edge pass below
        })
        for r in f.get("relations", []):
            edges.append({"from": f["formid"], "from_edid": f["edid"],
                          "to": r["target_faction_fid"],
                          "to_edid": fid_to_edid.get(r["target_faction_fid"], ""),
                          "reaction": r["reaction"],
                          "modifier": r.get("modifier", 0)})
    in_deg = Counter(e["to"] for e in edges)
    for e in enriched:
        e["relations_in"] = in_deg.get(e["formid"], 0)

    dist = Counter(e["hostility_class"] for e in enriched)
    (out / "factions.json").write_text(json.dumps(
        {"$doc": "699 decompiled FACTION records + hostility classification (M6/P2-a1).",
         "count": len(enriched), "distribution": dict(dist), "factions": enriched},
        indent=2) + "\n", encoding="utf-8")

    (out / "faction_graph.json").write_text(json.dumps(
        {"$doc": "777 faction relations as an edge list (sandbox sim input, M6).",
         "count": len(edges),
         "reactions": dict(Counter(e["reaction"] for e in edges)),
         "edges": edges}, indent=2) + "\n", encoding="utf-8")

    (out / "settlements.json").write_text(json.dumps(
        {"$doc": "Settlement ledger skeleton (M6/P2-a3). Form IDs are placeholders "
                 "to be re-resolved from the player's save/ESP at integration; "
                 "the ledger schema is the contract.",
         "schema": {"id": "str", "formid": "hex", "kind": "str",
                    "population": "int", "reputation": "float 0..1",
                    "raid_count_30d": "int", "active_trades": "int"},
         "settlements": [
             {"id": e, "formid": fid, "kind": k, "population": 10,
              "reputation": 0.5, "raid_count_30d": 0, "active_trades": 0}
             for e, fid, k in MAJOR_SETTLEMENTS]}, indent=2) + "\n", encoding="utf-8")

    (out / "economy.json").write_text(json.dumps(
        {"$doc": "Trade-goods table + price dynamics (M6/P2-a4).",
         **ECONOMY_RULES, "goods": TRADE_GOODS}, indent=2) + "\n", encoding="utf-8")

    print(f"factions: {len(enriched)} ({dict(dist)})")
    print(f"edges:    {len(edges)}")
    print(f"wrote {out}/factions.json, faction_graph.json, settlements.json, economy.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
