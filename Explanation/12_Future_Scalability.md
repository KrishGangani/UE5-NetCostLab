# 12 · Future Scalability: NetCostLab

**Status:** final for this version (2026-11-21). What the prioritiser and the method would need to grow, in order of value.

| Direction | What changes | Why it matters |
|---|---|---|
| **The measurement campaign that was planned and not run** | Scenario A at N = 8/16/32 with ≥ 6 interleaved runs per cell, Scenario B under a calibrated cap and 1/3/5% loss (`01` §6.3, `03` S4–S5). The tools exist (`run_bench.py --first-round`, `divergence.py`) | SC2–SC8 can only be decided there; the prioritiser's benefit shows under a cap, not at 1 MB/s |
| **More than one focus per object** | `FocusIdByIndex` becomes a small fixed set per object (a threat list) | A drone attacking a squad, or an object relevant to several players |
| **Other gameplay relations** | Any game system can call `SetFocusTarget`; the boost could scale with a gameplay weight instead of a constant | Objective carriers, projectiles aimed at a player, scoreboard leaders |
| **Combining with field-of-view** | Subclass `UFieldOfViewNetObjectPrioritizer` instead of the sphere, with the same `ApplyFocusBoost` | Cone-based distance model plus gameplay term |
| **Many connections** | `ApplyFocusBoost` is O(objects in batch × views); at hundreds of connections, precompute per connection the set of boosted indices when targets change | Keeps the per-send cost flat as N grows |
| **Dormancy and push model everywhere** | Props could go dormant between toggles | Removes most of their per-connection cost in every system |
| **A real dedicated server** | A source-built engine allows a Server target and a packaged server (`01b` #1–#3) | Absolute server numbers, not only comparisons |
| **More machines** | Clients on a second PC, a real network instead of loopback and emulation | Removes the shared-CPU and loopback caveats |
