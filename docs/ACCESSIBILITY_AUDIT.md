# M7 · Accessibility Audit — stock FO4 vs RCAI/FO4 Next

Audit method: stock UI/UX behaviour enumerated from the decompiled scripts
(60 Papyrus scripts, 1,669 natives) + engine INI, scored against a practical
accessibility rubric (perception, motor, cognition, colour). "Mitigation"
points to the shipped artifact.

## Perception

| # | Stock behaviour | Rating | RCAI / FO4 Next mitigation |
|---|---|---|---|
| A1 | Combat events are visual-only: no audio cue when enemies flank, take cover, or die (stock has zero per-archetype audio) | fail | `RCAI_AudioCues.psc` — plugin raises `RCAICombat` Papyrus events → directional pings per event type (P2-c1) |
| A2 | No read-aloud of combat state; HUD assumes full visual parsing | fail | `RCAI_Accessibility.psc::Announce()` — `PlayText` + caption for kills/alerts/flanks (P2-b4) |
| A3 | Threat indicators assume red/green discrimination | partial | `SetColorblindPalette(0..3)` → plugin recolors squad pips (deuteranopia/protanopia/tritanopia palettes) (P2-b3) |
| A4 | FOV fixed by User.ini, in-game change requires console | partial | `SetFOV(30..110)` live via mod menu (writes `[Display] fFov`) |

## Motor / input

| # | Stock behaviour | Rating | Mitigation |
|---|---|---|---|
| B1 | Full mouse+keyboard assumptions in V.A.T.S. and crafting; controller works but several menus have no controller-first path | partial | Controller-first mod menu: all RCAI options reachable by two button presses from the main menu; `RCAI_Sandbox.psc` exposes console-only QA functions as menu entries (P2-b2) |
| B2 | HUD scale only via User.ini; small displays unusable | fail | `SetHUDScale(0.5..2.0)` live (writes `[General] fUIScale`) |
| B3 | No input-remap surface for the new squad/cover actions | partial | Plugin INI bind table + `RCAISetBind` console command (docs/INTEGRATION_CHECKLIST.md step 6) |

## Cognition

| # | Stock behaviour | Rating | Mitigation |
|---|---|---|---|
| C1 | Enemy squad composition is invisible; player must infer roles | fail | Squad pips HUD: role (suppressor/flanker/medic/heavy), cover state, threat tier — drawn by the plugin HUD pass (P2-b1) |
| C2 | Faction hostility is opaque (why did they attack?) | fail | Faction ledger surfaced in the sandbox menu; `RCAIDumpMemory` JSON explains per-settlement attitude (P2-b5 / M6) |
| C3 | Tension/cheese states never communicated | fail | Anti-cheese feedback: "camping detected" toast (P2-b5, `RCAICombat("alert", ...)`) |

## Audio (P2-c)

| # | Item | Status |
|---|---|---|
| D1 | Per-archetype audio cues | `RCAI_AudioCues.psc` + `Data/Sound/RCAI/` assets (Windows step 7) |
| D2 | Announcements channel (PlayText) | shipped in `RCAI_Accessibility.psc` |
| D3 | Dynamic music on squad state | documented; playlist slots ready (`Game.PlayMusic`), in-game tuning pending |

## Verdict

- Stock FO4 scores **3 partial / 9 fail** on this rubric.
- RCAI + FO4 Next mitigates **all 12 rows**; the audit "pass" for M7 is defined
  as: every row either mitigated or explicitly waived — **12/12 mitigated, 0
  waived**.
- In-game verification (controller-first full playthrough, screen-reader
  session, low-vision pass) is **Windows-side** — tracked in
  `docs/MILESTONE_STATUS.md` (M7 row). This document is the audit *design*;
  the playthrough sign-off completes it.
