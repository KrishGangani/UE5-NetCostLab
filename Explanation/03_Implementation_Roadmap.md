# 03 · Implementation Roadmap: NetCostLab

**Written:** end of Phase 3, 2026-11-21 · **Engine:** UE 5.8.4

**Rules for every step:**
- Each step is small enough to read in one sitting: a few hundred lines at most.
- A step is finished only when its code **compiles with 0 warnings** (through the queue), its check **passes**, and its `Explanation/` entries (`05`, `06`, and `07` where relevant) are **written in the same step**.
- After each step that adds C++: run `simplify`, then `code-review`.
- **The baselines (C1–C4) are captured before the prioritiser exists.** Steps S1–S5 build only the workload, the harness and the measurement tools. The prioritiser starts at S6.
- Every multi-process run is **one queue job**: the server and all clients start and stop inside it.

---

**Progress (2026-11-21):** Phases 1–3 ✅. **Phase 4 ✅** (F1–F4, plus the re-check F-5 after the Iris sub-object-list fix; `09`). The Phase 2 probe and all Phase 4 checks ran in one queue job (`Tools\foundation_job.py`). **Phase 5:** S1 ✅ (`09`, step S1). Next: **S2**.

## Phase 4: Foundation (no gameplay code yet)

| Step | What gets built | How it is checked |
|---|---|---|
| **F1** | `NetCostLab.uproject` (EngineAssociation 5.8; plugins Iris, ReplicationGraph, NetCostLabCore enabled; AndroidFileServer disabled), `Source\NetCostLab.Target.cs`, `NetCostLabEditor.Target.cs`, the game module (`NetCostLab.Build.cs` with `Core`/`CoreUObject`/`Engine` only; network modules and Iris support arrive with the S1 code that uses them; module `.cpp`). The Phase 2 probe's `NetCostLabServer.Target.cs` is removed | `Tools\build.bat` for `NetCostLabEditor` and `NetCostLab` (Development) through the queue: **0 errors, 0 warnings** |
| **F2** | The plugin skeleton `Plugins\NetCostLabCore\` (`.uplugin`, `Build.cs` with `SetupIrisSupport`, module `.cpp`) | Builds with F1; the module loads in the smoke logs |
| **F3** | `Config\DefaultEngine.ini` (maps, VSync and smoothing off, 30 Hz server tick restated, Iris's registered sub-object lists; the client frame cap is a run-script argument from S4), `DefaultGame.ini`, `DefaultInput.ini`; `Tools\build.bat`, `package.bat`, `smoke_game.bat` | Package Development through the queue; the packaged game starts with `-nullrhi`, loads the map and exits with code 0 |
| **F4** | No new code: the **mixed-binary check**. An editor-binary dedicated server plus 2 **packaged** headless clients, legacy and Iris | Both clients are welcomed and joined in both setups. **Decides the client binary** (`01` §7.1) |

## Phase 5: Implementation

| Step | What gets built | How it is checked | Explanation files touched |
|---|---|---|---|
| **S1. Workload** | `ANCLGameMode`, `ANCLPlayerController` (server-side view = avatar), `ANCLPlayerAvatar`, `ANCLDrone`, `ANCLProp`, `UNCLWorkloadSubsystem` (a pure seeded plan, spawn, motion as a function of server time, change schedule, `MARK_PROPERTY_DIRTY_FROM_NAME`, workload hash); automation tests `NetCostLab.Workload.*`. (`ANCLGameState` moves to S2, where the run phases first exist) | Tests pass. Editor `-server` + 2 packaged clients, C1 and C4: the server spawns 256 drones and 768 props; every client sees the actors that the 150 m cull distance makes relevant to it (not always all 768 props, because the arena is 200 m square: `07` S1-11) and both avatars; the workload hash is identical across two C1 runs and across C1/C4; 0 errors in our log category | 04, 05, 06, 07 |
| **S2. Director, telemetry, run script** | `UNCLBenchSubsystem` (server phases, `CSV_EVENT(BenchStart)`, per-connection bytes and saturation at `OnWorldTickEnd`, busy ms, CSV category `NetCostLab`); `Tools\run_bench.py` (one queue job per batch, start + pin + wait + kill, interleaving, manifest), `Tools\csv_stats.py` (copied from P01 and adapted) | One C1 run at N = 8: the CSV has `Exclusive/GameThread/ServerReplicateActors`, `Exclusive/GameThread/NetworkOutgoing` and our columns; exactly 1,800 captured frames; every process exits; `csv_stats.py --selftest` passes | 05, 08, 09 |
| **S3. Correctness measurement** | Client arrival recorder (movement and state), server truth log, end-of-run state dump, `Tools\divergence.py` (shared-clock join, error on a 10 ms grid, staleness per change) | **Tool validation before use:** at 0% loss and no cap, median position error is within one server tick of motion; with `-PktLag=100` on the server, staleness rises by 100 ± 10 ms; final mismatches = 0 | 05, 08, 09 |
| **S4. Calibration** (no comparisons yet) | No new code | (a) N = 32 clients on the E-cores keep ≥ 27 fps; (b) Scenario A at N = 32: no saturated frames at 1 MB/s (SC2 feasible); (c) Scenario B: the cap gives ≥ 90% saturated frames for C4. Parameters are then **frozen** and written to `01` | 01, 09 |
| **S5. C6 + baselines captured** | `ANCLDrone::GetNetPriority` with the focus rule behind `ncl.LegacyFocusPriority` (C6, the existing-solution comparison; it is legacy code, not our system, so it is a baseline) | A C6 smoke run: the hook is called (a counter in the CSV). **The baseline capture:** Scenario A: C1, C2, C3, C4, C6 × N = 8/16/32 × 5 interleaved runs + 1 priming run. Scenario B: C2, C4, C6 × loss 0/1/3/5% × 3 interleaved runs + 1 priming run. H1–H4 answered with data **before any prioritiser code** | 05, 06, 09, 11 |
| **S6. The prioritiser (plugin)** | `UNCLGameplayPrioritizerConfig`, `UNCLGameplayPrioritizer` (`Assign`, `SetFocusTarget`, `Prioritize` = `Super` + `ApplyFocusBoost`), the ini registration | Automation tests for `ApplyFocusBoost` with fake arrays: boost only for the matching view target; no boost for an invalid focus; multiple views; zero objects. A C5 smoke run: `AssignFailures` = 0, `BoostedPerFrame` > 0 | 04, 05, 06, 07 |
| **S7. C5 integration** | The workload assigns drones in C5 (retrying until each has a handle) and updates focus targets on retarget | One Scenario B run of C4 and C5 at 0% loss: a first look at SC4/SC5 (a single run is never evidence), and the CPU check for SC6 at N = 32 | 05, 07, 09 |
| **S8. Demonstration client** | A rendering client mode (`-NCLView`): top-down camera, threat drones outlined | Video capture for Phase 10 only; never used for measurements | 05 |

## Phases 6–10 (after the code exists)

| Phase | Work |
|---|---|
| 6 Explanation | Consolidate 04–07 into one consistent line-by-line pass; re-check every API against 5.8.4 |
| 7 Testing | Edge cases: a client joining mid-capture, a client disconnecting, zero drones, every drone attacking one player, all players in one corner; a 30-minute soak at N = 32; Iris/legacy mismatch rejected cleanly |
| 8 Profiling | The full matrix with C5: Scenario A 6 configs × 3 N × 6 runs; Scenario B 4 configs (C2, C4, C5, C6) × 4 loss × 4 runs; boost sweep 1 / 2 / 4; Networking Insights drill-downs. Optimisations only with data (`11`) |
| 9 Code review | `code-review` and `simplify`, plus a written self-review: weaknesses, not praise |
| 10 Portfolio | Charts (`dataviz`), an architecture page, the demo video, README results, interview Q&A |

**Rough matrix cost:** baselines (S5) about 111 runs, the full Phase 8 matrix about 174 runs, each about 3 minutes: about 5.5 h and 9 h of queue time, split into queue jobs of about 30 minutes so other projects can interleave. A run's server and clients always run inside one job.
