#!/usr/bin/env python3
"""M7 · Release packager — build the Vortex-compatible RCAI mod archive.

Produces dist/RCAI-v<version>.zip with the layout Vortex expects (a Data/
tree at the zip root, extracted into the game directory on install):

    Data/
      RCAI/
        RCAI.ini              plugin config (from config/)
        manifest.json         version + per-file sha256 (integrity)
        combat/               weapons, combat styles, ballistics (M5)
        worldsim/             faction ledger, graph, settlements, economy (M6)
        render/               FO4 Next presets + INI fragments (M4)
        perf/                 CPU budgets (M3)
      F4SE/Plugins/           RCAI.dll goes here (built on Windows — see
                              build/build_dll_windows.ps1)
      Scripts/Source/RCAI/    Papyrus source for the CK (M7)
    README_INSTALL.txt        exact install steps
    docs/                     integration checklist + milestone status
    build/                    Windows build script

Honest scope: this is a *staging* package. Everything except the F4SE DLL and
the compiled .pex scripts is real, generated and committed; those two are
Windows-only (MSVC + F4SE SDK / Creation Kit) and the README documents the
exact remaining steps. Re-running the data generators is the default
(--no-regenerate to skip) — they are deterministic.

Usage: tools/pack_mod.py [--out dist] [--no-regenerate]
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = "0.3.0"  # keep in sync with kPluginVersion in plugins/rcai/src/RCAI.cpp

GENERATORS = [
    [sys.executable, "tools/render_preset.py"],
    [sys.executable, "tools/combat_tables.py"],
    [sys.executable, "tools/worldsim.py"],
]

# (source relative to ROOT, destination relative to the staging root)
FILES = [
    ("config/RCAI.ini", "Data/RCAI/RCAI.ini"),
    ("data/combat/weapons.json", "Data/RCAI/combat/weapons.json"),
    ("data/combat/combat_styles.json", "Data/RCAI/combat/combat_styles.json"),
    ("data/combat/ballistics.json", "Data/RCAI/combat/ballistics.json"),
    ("data/worldsim/factions.json", "Data/RCAI/worldsim/factions.json"),
    ("data/worldsim/faction_graph.json", "Data/RCAI/worldsim/faction_graph.json"),
    ("data/worldsim/settlements.json", "Data/RCAI/worldsim/settlements.json"),
    ("data/worldsim/economy.json", "Data/RCAI/worldsim/economy.json"),
    ("data/render/manifest.json", "Data/RCAI/render/manifest.json"),
    ("data/render/fo4_next_high.json", "Data/RCAI/render/fo4_next_high.json"),
    ("data/render/fo4_next_mid.json", "Data/RCAI/render/fo4_next_mid.json"),
    ("data/render/fo4_next_low.json", "Data/RCAI/render/fo4_next_low.json"),
    ("data/render/ini/fo4_next_high.ini", "Data/RCAI/render/ini/fo4_next_high.ini"),
    ("data/render/ini/fo4_next_mid.ini", "Data/RCAI/render/ini/fo4_next_mid.ini"),
    ("data/render/ini/fo4_next_low.ini", "Data/RCAI/render/ini/fo4_next_low.ini"),
    ("data/perf/budgets.json", "Data/RCAI/perf/budgets.json"),
    ("scripts/RCAI/RCAI_Accessibility.psc", "Data/Scripts/Source/RCAI/RCAI_Accessibility.psc"),
    ("scripts/RCAI/RCAI_AudioCues.psc", "Data/Scripts/Source/RCAI/RCAI_AudioCues.psc"),
    ("scripts/RCAI/RCAI_Sandbox.psc", "Data/Scripts/Source/RCAI/RCAI_Sandbox.psc"),
    ("docs/INTEGRATION_CHECKLIST.md", "docs/INTEGRATION_CHECKLIST.md"),
    ("docs/MILESTONE_STATUS.md", "docs/MILESTONE_STATUS.md"),
]

PLUGIN_README = """RCAI F4SE plugin goes in THIS folder.

  Data/F4SE/Plugins/RCAI.dll

This staging package does not ship the DLL: it is compiled on Windows
(Visual Studio 2022 + the F4SE 0.7.x SDK) — run build/build_dll_windows.ps1,
or wait for the next v* tag of the repo, whose CI uploads the DLL to the
GitHub release. Until the DLL is present, everything else in this mod is
inert but harmless (data files only).

Prereqs (docs/INTEGRATION_CHECKLIST.md, Prerequisites):
  - Fallout 4 build 1.10.163
  - F4SE 0.7.x
  - Address Library (Nexus 47327)
  - High FPS Physics Fix (Nexus 44798) for 120 Hz targets
"""

README_INSTALL = """RCAI — Reactive Combat AI (FO4 Modernization Program) · v{version}
===================================================================

What this is
------------
The AI/data layer of the FO4 Modernization Program: reactive squad AI
(cover, flanking, faction styles, anti-cheese), a living-world faction
ledger, Combat 2.0 ballistics tables and the "FO4 Next" render presets.

This is a STAGING package: every data file in here is real, generated and
checksummed (Data/RCAI/manifest.json). Two binary pieces are produced on
Windows and are the only remaining steps — nothing else is missing.

Install (Vortex)
----------------
1. Vortex -> Add Mod -> "Install from file" -> RCAI-v{version}.zip
   (it extracts the Data/ tree into your game directory).
2. Verify the game itself: Fallout 4 build 1.10.163 + F4SE 0.7.x +
   Address Library (Nexus 47327) + High FPS Physics Fix (Nexus 44798).
3. THE DLL (Windows): run build/build_dll_windows.ps1 in PowerShell
   (needs VS 2022 + the F4SE 0.7.x SDK under third_party/f4se/), or wait
   for the next v* GitHub release which includes RCAI.dll. Place the
   result at Data/F4SE/Plugins/RCAI.dll.
4. THE SCRIPTS (Creation Kit): open the CK on a clean plugin, import the
   three scripts in Data/Scripts/Source/RCAI/ and compile them (they lint
   clean; the CK compile is the one check the sandbox cannot run). Create
   a start-on-load essential quest "RCAI_Quest" and attach
   RCAI_Accessibility, RCAI_AudioCues and RCAI_Sandbox to it. Link the
   four Sound properties in RCAI_AudioCues to your cue assets
   (or leave them unlinked — the script no-ops safely).
5. Launch through FO4SE. Console: `rcai_status` should print the version
   and loaded state. Full verification steps: docs/INTEGRATION_CHECKLIST.md.

What the data files do
----------------------
  Data/RCAI/combat/     252 weapon classifications + ballistics curves +
                        112 decompiled combat styles mapped to 6 archetypes
  Data/RCAI/worldsim/   699-faction hostility ledger, 777-relation graph,
                        settlement schema, trade-goods economy
  Data/RCAI/render/     FO4 Next presets (4K/120, 1440p/144, 1080p/60) +
                        User.ini/System.ini fragments
  Data/RCAI/perf/       per-subsystem CPU budgets the profiler gates on
  Data/RCAI/RCAI.ini    plugin settings (enable, tick rate, budgets)

Status & honesty
----------------
docs/MILESTONE_STATUS.md: M0 done; M1-M7 logic done + tested (29/29 C++,
12/12 Python, PSC lint clean, 4/4 AI scoreboard gates, 5/5 perf gates).
In-game verification (DLL build, CK compile, 100 h soak, 120 Hz gauntlet,
controller playthrough) is Windows-side — every step is listed in
docs/INTEGRATION_CHECKLIST.md.

Legal
-----
Personal-use mod for a legally purchased copy of Fallout 4. The bundled
combat/worldsim data is derived analysis of game files: keep this package
for your own installs — do not publicly redistribute the data files.
""".format(version=VERSION)

BUILD_POWERSHELL = """# build/build_dll_windows.ps1 — one-shot Windows build of RCAI.dll
#
# Prereqs:
#   1. Visual Studio 2022 with the "Desktop development with C++" workload
#   2. CMake 3.25+
#   3. F4SE 0.7.x SDK: put its include/ and lib/ under third_party/f4se/
#      (same layout .github/workflows/build.yml uses)
#
# Output: dist/RCAI-v$VERSION-windows-x64.dll -> copy to Data/F4SE/Plugins/RCAI.dll
$ErrorActionPreference = "Stop"
$VERSION = "0.3.0"
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path "$repo/third_party/f4se/include")) {
    Write-Error "F4SE SDK not found at third_party/f4se/. See docs/INTEGRATION_CHECKLIST.md, Prerequisites."
}

Push-Location $repo
try {
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    New-Item -ItemType Directory -Force -Path dist | Out-Null
    $dll = Get-ChildItem -Path build -Recurse -Filter "RCAI.dll" | Select-Object -First 1
    if (-not $dll) { Write-Error "RCAI.dll not found after build" }
    Copy-Item $dll.FullName "dist/RCAI-v$VERSION-windows-x64.dll"
    Write-Host "Built: dist/RCAI-v$VERSION-windows-x64.dll"
    Write-Host "Install: Data/F4SE/Plugins/RCAI.dll (plus the Data/RCAI tree from the zip)"
} finally {
    Pop-Location
}
"""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--no-regenerate", action="store_true")
    args = ap.parse_args(argv)

    if not args.no_regenerate:
        for cmd in GENERATORS:
            print("regenerating: " + " ".join(cmd[1:]))
            subprocess.run(cmd, cwd=ROOT, check=True)

    stage = Path(args.out) / "RCAI-staging"
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "Data").mkdir(parents=True)

    # copy the real files
    missing = []
    for src, dst in FILES:
        s = ROOT / src
        if not s.exists():
            missing.append(src)
            continue
        d = stage / dst
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(s, d)
    if missing:
        print("ERROR: missing sources:", *missing, sep="\n  ")
        return 1

    # placeholders + generated text
    (stage / "Data" / "F4SE" / "Plugins").mkdir(parents=True)
    (stage / "Data" / "F4SE" / "Plugins" / "RCAI_PLUGIN_README.txt").write_text(
        PLUGIN_README, encoding="utf-8")
    (stage / "build").mkdir(parents=True)
    (stage / "build" / "build_dll_windows.ps1").write_text(BUILD_POWERSHELL,
                                                           encoding="utf-8")
    (stage / "README_INSTALL.txt").write_text(README_INSTALL, encoding="utf-8")

    # integrity manifest (everything under Data/RCAI)
    manifest = {
        "$doc": "RCAI staging package manifest — sha256 per data file.",
        "version": VERSION,
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "pending_windows_builds": ["Data/F4SE/Plugins/RCAI.dll",
                                   "Data/Scripts/*.pex (CK compile of Scripts/Source)"],
        "files": {},
    }
    for p in sorted((stage / "Data" / "RCAI").rglob("*")):
        if p.is_file():
            manifest["files"][str(p.relative_to(stage)).replace("\\", "/")] = sha256(p)
    (stage / "Data" / "RCAI" / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    # zip it (Vortex extracts zip root into the game dir)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"RCAI-v{VERSION}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(stage.rglob("*")):
            if p.is_file():
                zf.write(p, p.relative_to(stage))
    total_kb = zip_path.stat().st_size / 1024
    print(f"\nwrote {zip_path} ({total_kb:.0f} KB, {len(manifest['files'])} data files + "
          f"{zipfile.ZipFile(zip_path).namelist().__len__()} entries total)")
    print("layout:")
    for n in zipfile.ZipFile(zip_path).namelist():
        print("  " + n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
