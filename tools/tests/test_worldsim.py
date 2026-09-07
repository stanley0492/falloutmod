#!/usr/bin/env python3
"""Tests for the M6 worldsim data pipeline."""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DATA = ROOT / "data" / "worldsim"
KB = ROOT / "DecompiledMaster" / "data"


class TestWorldsim(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, str(ROOT / "tools" / "worldsim.py")],
                       check=True, capture_output=True)
        cls.factions = json.loads((DATA / "factions.json").read_text(encoding="utf-8"))
        cls.graph = json.loads((DATA / "faction_graph.json").read_text(encoding="utf-8"))
        cls.settle = json.loads((DATA / "settlements.json").read_text(encoding="utf-8"))
        cls.econ = json.loads((DATA / "economy.json").read_text(encoding="utf-8"))
        cls.kb = json.loads((KB / "factions.json").read_text(encoding="utf-8"))

    def test_all_factions_present(self):
        self.assertEqual(self.factions["count"], len(self.kb))
        self.assertEqual(self.factions["count"], 699)
        kb_edids = {f["edid"] for f in self.kb}
        self.assertEqual({f["edid"] for f in self.factions["factions"]}, kb_edids)

    def test_hostility_worst_relation_wins(self):
        by_edid = {f["edid"]: f for f in self.kb}
        for f in self.factions["factions"]:
            src = by_edid[f["edid"]]
            kinds = {r["reaction"] for r in src.get("relations", [])}
            expect = ("Hostile" if "Enemy" in kinds else
                      "Ally" if "Ally" in kinds else
                      "Friendly" if "Friend" in kinds else
                      "Neutral" if "Neutral" in kinds else "Isolated")
            self.assertEqual(f["hostility_class"], expect, f["edid"])

    def test_graph_edges_match_source(self):
        n_src = sum(len(f.get("relations", [])) for f in self.kb)
        self.assertEqual(self.graph["count"], n_src)
        self.assertEqual(self.graph["count"], len(self.graph["edges"]))
        self.assertEqual(set(self.graph["reactions"]), {"Enemy", "Friend", "Ally", "Neutral"})
        # In-degree consistency.
        from collections import Counter
        in_deg = Counter(e["to"] for e in self.graph["edges"])
        by_fid = {f["formid"]: f for f in self.factions["factions"]}
        for f in self.factions["factions"]:
            self.assertEqual(f["relations_in"], in_deg.get(f["formid"], 0))

    def test_settlement_ledger_schema(self):
        self.assertEqual(len(self.settle["settlements"]), 20)
        for s in self.settle["settlements"]:
            for k in ("id", "formid", "kind", "population", "reputation",
                      "raid_count_30d", "active_trades"):
                self.assertIn(k, s, k)
            self.assertGreaterEqual(s["reputation"], 0.0)
            self.assertLessEqual(s["reputation"], 1.0)

    def test_economy_table_sane(self):
        self.assertGreaterEqual(len(self.econ["goods"]), 8)
        for g in self.econ["goods"]:
            self.assertGreater(g["base_price"], 0.0)
            self.assertGreater(g["scarcity"], 0.0)
            self.assertIn(g["perishable"], (True, False))
        self.assertGreater(self.econ["k_raid"], 0.0)


if __name__ == "__main__":
    unittest.main()
