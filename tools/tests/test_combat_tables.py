#!/usr/bin/env python3
"""Tests for the M5 combat-table pipeline (run: python3 -m tools.tests.test_combat_tables)."""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DATA = ROOT / "data" / "combat"


class TestCombatTables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Regenerate deterministically so the test also covers the tool.
        subprocess.run([sys.executable, str(ROOT / "tools" / "combat_tables.py")],
                       check=True, capture_output=True)
        cls.weapons = json.loads((DATA / "weapons.json").read_text(encoding="utf-8"))
        cls.styles = json.loads((DATA / "combat_styles.json").read_text(encoding="utf-8"))
        cls.ball = json.loads((DATA / "ballistics.json").read_text(encoding="utf-8"))

    def test_all_weapons_classified(self):
        self.assertEqual(self.weapons["count"], 252)
        self.assertEqual(len(self.weapons["weapons"]), 252)
        # One decompiled dummy is genuinely unclassifiable; everything else
        # must carry a class.
        self.assertLessEqual(self.weapons["unclassified_fallback"], 1)

    def test_classes_are_schema_valid(self):
        schema = set(self.ball["classes"].keys())
        for w in self.weapons["weapons"]:
            self.assertIn(w["class"], schema, w["edid"])
            self.assertIn(w["role"], {"assault", "sniper", "heavy", "support",
                                      "suicidal", "generic"}, w["edid"])
            self.assertEqual(w["ballistics"], self.ball["classes"][w["class"]])

    def test_known_weapons(self):
        by_edid = {w["edid"]: w for w in self.weapons["weapons"]}
        self.assertEqual(by_edid["10mm"]["class"], "pistol")
        self.assertEqual(by_edid["CombatShotgun"]["class"], "shotgun")
        self.assertEqual(by_edid["BaseballBat"]["class"], "melee")
        self.assertEqual(by_edid["FatManBomb"]["class"], "explosive")
        self.assertEqual(by_edid["InstituteLaserGun"]["class"], "energy")
        self.assertEqual(by_edid["CombatRifle"]["class"], "rifle")

    def test_ballistics_model_sane(self):
        for cls, c in self.ball["classes"].items():
            self.assertGreater(c["end_m"], c["start_m"], cls)
            self.assertGreater(c["pen"], 0.0, cls)
            self.assertGreater(c["rofs_hertz"], 0.0, cls)

    def test_all_styles_mapped(self):
        self.assertEqual(self.styles["count"], 112)
        self.assertEqual(len(self.styles["styles"]), 112)
        archs = {"assault", "sniper", "heavy", "support", "suicidal", "generic"}
        params = {
            "assault":  {"cover_preference": 0.60, "flank_bias": 0.40, "aggro": 0.80},
            "sniper":   {"cover_preference": 0.85, "flank_bias": 0.20, "aggro": 0.50},
            "heavy":    {"cover_preference": 0.55, "flank_bias": 0.30, "aggro": 0.70},
            "support":  {"cover_preference": 0.50, "flank_bias": 0.35, "aggro": 0.45},
            "suicidal": {"cover_preference": 0.15, "flank_bias": 0.75, "aggro": 0.95},
            "generic":  {"cover_preference": 0.40, "flank_bias": 0.40, "aggro": 0.60},
        }
        for s in self.styles["styles"]:
            self.assertIn(s["archetype"], archs, s["edid"])
            self.assertEqual(s["archetype_params"], params[s["archetype"]])

    def test_style_raw_floats_preserved(self):
        kb = json.loads((ROOT / "DecompiledMaster" / "data" /
                         "csty_combat_styles.json").read_text(encoding="utf-8"))
        by_edid = {s["edid"]: s for s in self.styles["styles"]}
        for src in kb:
            self.assertIn(src["edid"], by_edid)
            self.assertEqual(by_edid[src["edid"]]["raw"], src["general_data_floats"])

    def test_archetype_distribution_not_degenerate(self):
        from collections import Counter
        dist = Counter(s["archetype"] for s in self.styles["styles"])
        # The 112 stock styles must span several archetypes, not collapse
        # into one bucket.
        self.assertGreaterEqual(len(dist), 4, dict(dist))


if __name__ == "__main__":
    unittest.main()
