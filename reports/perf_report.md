# RCAI Performance Report

Source: `rcai_profile.csv` — 3947 frames

## Frame pacing

| Metric | Value |
|---|---|
| Median frame time | 0.02 ms |
| P95 frame time | 0.03 ms |
| Max frame time | 0.10 ms |
| 1%-low (worst 1% avg) | 0.02 ms |
| RCAI total CPU/frame | 0.02 ms |

## Subsystem costs

| Subsystem | Mean ms | P95 ms | Max ms | Share |
|---|---|---|---|---|
| decision | 0.021 | 0.027 | 0.09 | 97.3% |
| sim_step | 0.001 | 0.001 | 0.03 | 2.7% |

## Regression gates

- [PASS] RCAI total CPU <= 3.0 ms/frame — measured 0.02 ms
- [PASS] decision <= 1.00 ms (p95) — p95 0.027 ms
- [PASS] sim_step <= 0.50 ms (p95) — p95 0.001 ms
- [PASS] no frame > 25 ms (jank gate) — 0 jank frames
- [PASS] 1%-low frame time <= 16.67 ms (60 Hz parity) — 0.02 ms

**ALL GATES PASSED** (5/5 gates)

## Methodology

- Source: `FrameProfiler` CSV (steady_clock, per-subsystem scopes); budgets: `data/perf/budgets.json`.
- Headless benchmark figures are per 20 actors in the 2D world — a floor cost model.
  In-game figures (3D occlusion, full cell population) come from the Windows soak
  run (`RCAIDumpProf`); see `docs/INTEGRATION_CHECKLIST.md`.
