# 09 · Testing And Benchmarks: NetCostLab

**Rule:** every check is recorded with its date, its exact command and its result, **including failures**. The full functional and benchmark sections are filled in during Phases 5–8. Results from earlier phases are recorded as they happen.

**How these checks ran:** one queue job (`tools\ue_queue.py`, job `20261121_014448_2ec3e9`, 442 s, exit 0), started by `Tools\foundation_job.py`, ran the Phase 2 probe and every Phase 4 check in dependency order (`05` B3). The probe ran after the editor build because the `.uproject` already listed the C++ module. Manifests in `Benchmarks\Raw\`: `2026-11-21_022445_foundation_manifest.json` (all steps), `2026-11-21_022542_probe_manifest.json` (P2-1), `2026-11-21_023102_f4_manifest.json` (F4). The probe manifest was passed through `sanitize()` again after the extended version (computer and user names) was written, before any commit.

| Step | Command (from `P05_NetCostLab\`) |
|---|---|
| F1/F2 | `Tools\build.bat NetCostLabEditor Development` |
| P2-1 | `python -I -u Tools\probe_net.py` |
| F1 | `Tools\build.bat NetCostLab Development` |
| F3a | `Tools\package.bat Development` |
| F3b | `Tools\smoke_game.bat packaged` |
| F4 | `python -I -u Tools\probe_net.py --client packaged --setups legacy_push,iris --skip-server-target --label f4` |

Affinity in every networked step, read back from Windows per process: server `0xFFFF` (P-cores), clients `0xFF0000` (E-cores).

---

## Phase 2 · Probe P2-1: what the launcher engine can run (2026-11-21)

| # | Check | Result |
|---|---|---|
| P2-1a | **Build a Server target** (`Build.bat NetCostLabServer Win64 Development`) | ❌ **Refused by design of the engine distribution**, in 1.1 s, exit 6: "Server targets are not currently supported from this engine distribution." This is the line predicted by `UEBuildTarget.cs:1404` (`01b` #1). Nothing was compiled |
| P2-1b | **Editor binary as a dedicated server, legacy + push model** (`-server -UseIrisReplication=0 -dpcvars=Net.IsPushModelEnabled=1`), 2 headless editor-binary clients | ✅ Listening after 40.2 s (cold start); both clients joined and were welcomed by 63.5 s. Server log: "InitBase GameNetDriver … using replication model **Generic**"; "ReplicationDriverClass is null! Not using ReplicationDriver."; "Bringing World … up for play (**max tick rate 30**)" (the dedicated-server cap, `01b` #4); `Net.IsPushModelEnabled = "true" LastSetBy: DeviceProfile` (so `-dpcvars` works, and push model exists in the editor binary, `01b` #2); `net.Iris.PushModelMode = "2"`. Clients: "max tick rate 0" (uncapped). **0 errors** in all three logs |
| P2-1c | **Replication Graph** (`-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ReplicationDriverClassName=/Script/ReplicationGraph.BasicReplicationGraph`) | ✅ Listening after 9.6 s, joined by 20.3 s. "…using replication model **BasicReplicationGraph**"; `Net.RepGraph.PrintGraph` printed `ReplicationGraphNode_GridSpatialization2D_0`. 0 errors |
| P2-1d | **Iris** (`-UseIrisReplication=1 -dpcvars=Net.IsPushModelEnabled=1` on server and clients) | ✅ **Works**, with a finding. Listening after 10.8 s, joined by 23.4 s. "…using replication model **Iris**"; "Iris ReplicationSystem[0] … is created" on server and both clients. `net.Iris.UseIrisReplication` still reads `"0"`: the command line forces the system without changing the cvar (`01b` #5). **❌ Finding:** a handled ensure on server and clients, "Ensure condition failed: GDefaultUseSubObjectReplicationList … Iris requires replicated actors to use registered subobjectslists. Add [SystemSettings] net.SubObjects.DefaultUseSubObjectReplicationList=1 to your DefaultEngine.ini" (`EngineReplicationBridge.cpp:234`), plus per-actor warnings "…does not replicate subobjects using the registered SubObjectsLists, SubObjects will not replicate properly" (58 error lines on the server: one ensure with its call stack). Fixed in `DefaultEngine.ini` after this job (check F-5) |
| P2-1e | **Memory per process** (working set, 15 s after both clients joined) | Editor-binary server ≈ 1.80 GB, editor-binary client ≈ 1.66–1.80 GB in all three setups. **32 editor-binary clients would need ≈ 57 GB on a 32 GB machine**: the editor binary cannot be the client (decided by F4) |
| P2-1f | **Warnings** | 21 per editor-binary process in every setup, **all** `LogEditorDataStorageUI: Unable to register widget factory …` from the editor binary itself (engine-side, none from this project). Iris adds `LogIris: Warning: FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated() called while there are 1 active ReplicationSystems. If polymorphic types are registered we may have corrupt data` (`IrisCoreModule.cpp:141–160`): modules loaded after the replication system started. This project registers no polymorphic net serializers; **S1 checks which modules load late** |

**What P2-1 taught us:** the source reading was right on every point it tested (no Server target; the editor binary is a dedicated server at 30 Hz; push model exists there; command-line switches select Generic / BasicReplicationGraph / Iris), and the run added one fact the reading missed: **Iris on 5.8.4 needs `net.SubObjects.DefaultUseSubObjectReplicationList=1`**.

## Phase 4 · Foundation checks (2026-11-21)

| # | Check | Command | Result |
|---|---|---|---|
| F-1 | Editor target builds (game module + plugin, which uses `SetupIrisSupport`) | `Tools\build.bat` | ✅ Succeeded in 56.3 s; 12 actions; **0 errors, 0 warnings**. Toolchain: MSVC 14.44.35229 (toolset folder 14.44.35207), Windows SDK 10.0.22621.0 |
| F-2 | Game target builds | `Tools\build.bat NetCostLab Development` | ✅ Succeeded in 51.6 s; links `NetCostLab.exe`; **0 errors, 0 warnings** |
| F-3a | Package a **Development** build | `Tools\package.bat` | ✅ BUILD SUCCESSFUL in 98.9 s; cook "Success - **0 error(s), 0 warning(s)**"; 521 packages; ≈ 985 MB staged in `Build\Windows` (git-ignored) |
| F-3b | Packaged build runs headless | `Tools\smoke_game.bat packaged` (the real exe, `-nullrhi`) | ✅ Exit 0 in 3.2 s; mounts `Iris`, `ReplicationGraph` and the project plugin `NetCostLabCore`; does **not** mount `AndroidFileServer`; `Template_Default` loads in 0.034 s; exits through `quit`. **0 errors, 0 warnings** |
| F-3c | No engine-written settings in `Config\` | `diff` of `Config\*.ini` against the files as written | ✅ All three `.ini` files unchanged after every editor, cook and server run; no `AndroidFileServerRuntimeSettings` block (`07` F13) |
| F-4a | **Packaged headless clients join the editor-binary server**, legacy + push | `probe_net.py --client packaged …` | ✅ Server listening after 9.7 s, both packaged clients joined and welcomed by 11.9 s. **Client logs: 0 errors, 0 warnings.** Working set: packaged client ≈ **348 MB**, editor-binary server ≈ 1.53 GB |
| F-4b | The same with **Iris** | `probe_net.py --client packaged …` | ✅ Joined by 14.8 s; Iris replication systems created on both sides. Working set: packaged client ≈ **537 MB**, server ≈ 1.57 GB. The sub-object-list ensure of P2-1d appears again (expected: the setting was not yet in the config) |
| F-5 | **Re-check after the fix:** `net.SubObjects.DefaultUseSubObjectReplicationList=1` added to `DefaultEngine.ini`, the probe's `NetCostLabServer.Target.cs` deleted; both builds, re-package, packaged smoke, F4 again | `foundation_job.py --only F1-F2_build_editor,F1_build_game,F3a_package,F3b_smoke_packaged,F4_packaged_clients --label foundation_fix1` (queue job `20261121_023350_c5d5ec`, 127.4 s, exit 0; batches `2026-11-21_025233_foundation_fix1`, `2026-11-21_025341_f4`) | ✅ Editor and game targets up to date with the final tree (3.9 s, 1.8 s; **0 errors, 0 warnings**; no source changed since F-1/F-2); re-package 59.0 s, 0 errors, 0 warnings; packaged smoke exit 0 in 2.7 s. **Iris: the ensure is gone.** Server **0 error lines** (was 58), each packaged client **0** (was 30); both sides log `Set CVar [[net.SubObjects.DefaultUseSubObjectReplicationList:1]]`. Joined by 10.9 s. Remaining warnings: the 21 engine-side `LogEditorDataStorageUI` lines on the editor-binary server, and the Iris late-module warning (1 on the server, 3 per client; `07` F16, checked in S1). Legacy: 0 errors on all three processes, packaged clients 0 warnings. Working set: packaged client ≈ 348 MB (legacy) and ≈ 360 MB (Iris), server ≈ 1.54–1.55 GB |

**Decision from F4 (recorded in `02` §7 and `01` §7.1):** the benchmark **clients are the packaged Development game** (`Build\Windows\NetCostLab\Binaries\Win64\NetCostLab.exe -nullrhi`). Measured per-process memory makes 32 of them feasible: at most ≈ 32 × 0.54 GB ≈ 17 GB (the largest value seen, Iris before the fix), versus ≈ 57 GB for editor-binary clients. The server stays the editor binary for all five configurations. These are idle start-up numbers with an empty map; S4 measures memory and client frame rate with the real workload at N = 32.

**What Phase 4 taught us:** run the engine before trusting a reading of it. The source predicted every limit correctly, but only a run showed the Iris sub-object-list requirement; finding it here, before any workload code, kept it out of the baselines.

---

## Phase 5 · Step S1: the workload (2026-11-21)

**How it ran:** one queue job (`20261121_031018_539526`, 210.5 s, exit 0) started by `Tools\check_job.py`, which runs the requested steps in order and stops at the first failure:

```
python -I -u Tools\check_job.py --label s1 --build --tests NetCostLab.Workload --package
    --probe "--client packaged --setups legacy,iris --skip-server-target --label s1a --extra=-NCLCountLog=5 --hold 20"
    --probe "--client packaged --setups legacy --skip-server-target --label s1b --extra=-NCLCountLog=5 --hold 20"
```

Manifests in `Benchmarks\Raw\`: `2026-11-21_032353_s1_manifest.json` (steps), `2026-11-21_032536_s1a_manifest.json` (C1 and C4), `2026-11-21_032648_s1b_manifest.json` (C1 again). `legacy` in the probe is C1 (push off), `iris` is C4. Every networked process was pinned before launch and its mask read back: server `0xFFFF`, both clients `0xFF0000`, in all three setups.

| # | Check | Result |
|---|---|---|
| S1-a | Editor target builds | ✅ 11.8 s, 16 actions (the game module's sources and generated code; the plugin module was recompiled with it); **0 errors, 0 warnings** |
| S1-b | Game target builds | ✅ 14.3 s; **0 errors, 0 warnings** |
| S1-c | Automation tests `NetCostLab.Workload` (headless editor, `Tools\run_tests.bat`) | ✅ **3 of 3 passed**: `LoopMotionMatchesSpeed`, `PlanIsDeterministic`, `PlanStaysInArena` (`Saved\Logs\Tests.log`: "Test Completed. Result={Success}" for each; "TEST COMPLETE. EXIT CODE: 0") |
| S1-d | Package (Development) | ✅ 59.8 s; cook "Success - 0 error(s), 0 warning(s)"; BUILD SUCCESSFUL |
| S1-e | **Workload on server and clients**, C1 and C4, `-NCLCountLog=5`, 20 s with both clients connected | ✅ Server, every sample: `netmode=1 drones=256 props=768 avatars=2`. Clients (`netmode=3`), 6–7 samples each: the first (0 actors) is taken while the map loads; then the counts **ramp up for about 10–15 s** (e.g. props 707 → 741 → 758 → 764) while the initial state of 1,026 actors arrives at the engine's default 100 kB/s per client (`MaxClientRate=100000`, `BaseEngine.ini:1860`); then they **plateau at drones 256, avatars 2, and props 764 for one client and 768 for the other, in all three setups**. The plateau is relevancy, not loss: the cull distance is 150 m and the arena is 200 m square (diagonal 283 m), so props far from one avatar's loop are correctly not replicated (`07` S1-11; the 03 criterion was corrected). One C4 client's last sample is 762: Iris drops an actor as soon as it leaves the cull radius, while legacy keeps its channel for `RelevantTimeout` = 5 s (`BaseEngine.ini:1864`, `NetDriver.cpp:5773`). In the C1 repeat, one client's last sample shows `avatars=1`: the other client had already disconnected. **Consequence for S2/S4:** the 20 s warm-up must cover this ramp at every N; S4 checks it |
| S1-f | **Workload hash** | ✅ `seed=1 drones=256 props=768 hash=0x680F4C7A` on the server in all three setups (two C1 runs and the C4 run) |
| S1-g | **Errors and warnings** | ✅ **0 error lines** in all nine logs; **0 `LogNetCostLab` warnings or errors**. Server warnings: the 21 engine-side `LogEditorDataStorageUI` lines (P2-1f), plus 1 Iris late-module warning in C4. Clients: 0 warnings in C1; 3 Iris late-module warnings per client in C4. Which modules load late (P2-1f asked): on the server, the six engine tool modules `AutomationWorker`, `AutomationController`, `SequenceRecorder`, `SequenceRecorderSections`, `CollisionAnalyzer` and `PerfCounters` (count 108 → 114, matching six `InternalLoadLibrary` lines), all loaded at the end of engine start-up after the map's net driver exists. `NetCostLab` and `NetCostLabCore` load earlier (before the replication system is created). The packaged client is monolithic and does not log module loads; its count goes 93 → 97 in three steps. None of these modules registers a net serializer of this project |
| S1-h | Join time and memory | Server listening after 8.5–9.6 s; both clients joined by 10.8–12.2 s. Working set: packaged client 353–369 MB, editor-binary server 1.55 GB, now with the 1,026 replicated actors |

**S1-i · Re-check after the review and simplify fixes** (same command, labels `s1r` and `s1r2`):

| Attempt | Result |
|---|---|
| Queue job `20261121_035311_fabc5c` (batch `2026-11-21_041253_s1r`) | Editor target ✅ 14.3 s, 10 actions (every changed `.cpp`, the tests and the generated code); game target ✅ 15.9 s; **0 errors, 0 warnings** on both, with the final S1 source. ❌ **Tests step failed in 0.0 s:** `'D:\UNREAL' is not recognized as an internal or external command`. The new log-path argument made the command line hold two quoted paths with spaces, and `cmd /c` then strips the first and last quote (`07` S1-15). The step failed, as it should, and everything after it was skipped. Fix: the log path is passed relative to `Tools\` |
| Queue job `20261121_041433_e65442` (batches `2026-11-21_050006_s1r2`, `…_050124_s1r2a`, `…_050237_s1r2b`), 186.4 s, exit 0 | Both targets up to date (1.1 s each: no source changed since the attempt above). ✅ **5 of 5 tests passed**, recorded per test in the manifest: `ChangeStreamsArePure`, `LoopMotionMatchesSpeed`, `PlanIsDeterministic`, `PlanStaysInArena`, `ScheduleKeepsPhase`. Package ✅ 58.8 s, 0 errors, 0 warnings. C1, C4 and C1 again: hash `0x680F4C7A` in all three (the plan is unchanged; only the change streams and avatar loops are now drawn per event). Clients, after the map loads (the first one or two samples are 0): drones 253–256, props 733–761, both avatars. The numbers moved because the avatars now follow different loops (their stream changed), and relevancy follows the avatars; C1 and C4 show the same ranges. 0 error lines in all nine logs, 0 `LogNetCostLab` warnings; warnings as before (21 engine-side on the editor-binary server, the Iris late-module warning in C4). Masks: server `0xFFFF`, clients `0xFF0000`, in every setup |

**Audit trail for S1** (after the first job; the fixes were re-checked by S1-i):
- `code-review` (medium) raised 6 findings: 2 important, 4 nits. All fixed:
  - **important:** the settings were read at begin play, but in a standalone or listen-server world the host logs in first (`UnrealEngine.cpp:16646` vs `:16660`), so its avatar ignored `-NCLSeed`. They are now read in `Initialize` (`07` S1-12).
  - **important:** the draw order depended on tick timing. A drone's state and loadout changes shared one stateful stream, and all props shared another. Two changes that fell due in one tick in one run and in two ticks in another drew in a different order, and every later value differed. Every event now has its own stream, `MakeStream(seed, kind, object, event number)`, tested by `ChangeStreamsArePure` (`07` S1-4).
  - **nit:** the comment claimed a hitch never causes a burst. It does: about 30 ticks' worth of objects become due at once. The comment and `07` S1-7 were corrected, and the schedule is tested by `ScheduleKeepsPhase`.
  - **nit:** `Seed * 104729` overflowed `int32`, which is undefined behaviour. It disappeared with the CRC-mixed stream seed.
  - **nit:** avatars never turned, so every view looked along +X, and legacy priority depends on the view direction. Avatars now face their direction of travel (`LoopFacing`, tested; `07` S1-13).
  - **nit:** `check_job.py` split `--probe` strings with `shlex`, which drops backslashes. It now uses `split_args` (`07` S1-14).
- `simplify` (4 parallel read-only reviews: reuse, simplification, efficiency, altitude). Applied:
  - the cull distance is one constant, `NCLNet::CullDistance` (`NCLNet.h`), instead of three literals;
  - removed the unused `HasWorkload`, `GetDrones` and `GetAvatars`, and the `bSpawned` flag (the loops over empty arrays already do nothing);
  - the arena size is a `static constexpr` member instead of a getter plus a namespace copy;
  - `FLoop` has a defaulted `operator==`, so the test compares whole arrays;
  - an avatar loop is computed once per slot;
  - `Algo::CountIf` in `OccupiedSlotAt`, which now runs only for an attack (the fraction is still drawn, so the draws do not change);
  - each actor pointer is read once per tick;
  - a prop's schedule is checked before the prop itself;
  - shared `ENGINE`, `new_batch` and `write_manifest` in `netlab_env.py`, used by `foundation_job.py`, `probe_net.py` and `check_job.py` instead of three copies;
  - `check_job.py` reuses `foundation_job.TOOLS`/`PY` and passes `run_tests.bat` a log path inside the fresh batch folder. This removes the stale-log delete and copy, and the exit-code override, because the editor already exits non-zero when no test matches or one fails (`AutomationCommandline.cpp:346–355, 493–504`);
  - `probe_net.py` builds the shared switches once, and the count pattern is one constant.
- Skipped, with reasons:
  - `COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY` (`PushModel.h:466`): it skips the dirty mark when a draw repeats the old value. That changes what is marked dirty, which H1 measures; the setters stay explicit.
  - The engine's `index.json` report instead of the log lines: the log is per run in the batch folder now, and the report folder is shared.
  - A common base class for the three actor types: one shared constant removes the drift risk; the rest is four lines per constructor.
  - `IndexOfByKey` in `RemovePlayer`: the lambda is `04` §10's example; same cost.
  - Building both targets in one UBT call, or dropping `build_game` when packaging: the separate build steps are the 0-warning gate for each target; the packaging rebuild is an up-to-date check.
  - Engine arguments after `--` in `probe_net.py`: the `--extra=` form works and is documented; changing the interface buys nothing in S1.
  - Computing the angle once for position and facing: at most 32 avatars.

**What S1 taught us:** the first expectation that failed was ours. "Every client sees every prop" ignored the cull distance that 01 §6.1 itself sets; the plateau counts are the relevancy rule giving the same result per client in C1 and C4.

---

## Phase 5 · Step S2: the director, the telemetry and the run script (2026-11-21)

**How it ran:** two queue jobs, each `Tools\check_job.py` with a `--bench` step running `Tools\run_bench.py`:

```
python -I -u Tools\check_job.py --label s2 --build --tests NetCostLab --package
    --bench "--configs C1,C4 --clients 8 --runs 1 --no-prime --label s2b"
```

Each bench step ran one C1 and one C4 run at N = 8, Scenario A (1 MB/s per client), 20 s warm-up, 1,800 captured frames, 5 s settle. There was no priming run: these are tool checks, not measurements. Masks were read back for every process: server `0xFFFF`, all 8 clients `0xFF0000`. The numbers below show that the tools work. **They are not results**, and nothing is concluded from them about the configurations.

| # | Check | Result |
|---|---|---|
| S2-a | **First job** `20261121_050455_082254` (batches `2026-11-21_053124_s2`, `…_053310_s2b`) | Both targets ✅ (16.4 s, 17.1 s), **0 errors, 0 warnings**; 6 of 6 tests passed (then including `NetCostLab.Bench.SaturationRule`, removed later). Package ✅ 55.7 s. Both runs valid by the rules then in force: exactly 1,800 frames; `BenchStart`, `BenchEnd` and one `GC` event in the CSV; metadata `NCLRunTag`, `NCLConfig`, `NCLClients=8`, `NCLWorkloadHash=0x680F4C7A`; every process exited 0 by itself. ❌ **Finding: the clients were not capped.** They ran at 2,036–2,217 fps although `-dpcvars=t.MaxFPS=30` was set and logged (`07` S2-8, fixed in the director) |
| S2-b | `code-review` (medium) and `simplify` on the S2 code | 10 review findings and the four simplify reviews (audit trail below). The largest change: saturation now comes from the engine's own per-connection count (`IsNetReady() == false`, `NetConnection.cpp:5110`), which is the metric `01` §6.4 defines, instead of a self-made 90%-of-allowance rule (`07` S2-4) |
| S2-c | **Re-check** `20261121_055446_7732f1` (batches `2026-11-21_065838_s2r`, `…_070023_s2rb`), 0 errors in the job | Both targets ✅ (12.7 s, 17.0 s), **0 errors, 0 warnings**, with the final S2 source. 5 of 5 tests passed (S2 adds none, `05` D3). Package ✅ 58.8 s, 0 errors, 0 warnings. `csv_stats.py --selftest` ✅ |
| S2-d | **Exactly 1,800 captured frames** | ✅ C1 and C4: 1,800 data rows each, finalised; `window_frames` in `_server.json` = 1,800, so the byte window and the CSV cover the same frames (`07` S2-2). Window 60.0 s; 0 long frames |
| S2-e | **The CSV has the columns we need** | ✅ `Exclusive/GameThread/ServerReplicateActors`, `…/NetworkOutgoing`, `…/NetworkIncoming`, `FrameTime` and our `NetCostLab/BusyMsPrevFrame`, `/OutBytes`, `/SatConnections`, `/Clients`, in legacy (C1) **and** Iris (C4). `NetFlushMs` derived for all 1,800 frames |
| S2-f | **Every process exits by itself** | ✅ Server exit 0 in both runs, logging `NCL bench: done (all clients left)`: every client wrote its report and disconnected before the server quit. All 16 client exits 0 |
| S2-g | **Client frame cap** | ✅ Every client 29.98–29.99 fps in both runs (was 2,000+ in S2-a) |
| S2-h | **Bytes: server and client agree** | ✅ Median bytes per second per client 67,603 (server, `OutTotalBytes`) vs 67,610 (clients, `InTotalBytes`) in C1, and 43,514 vs 43,502 in C4. 0 saturated connection-frames at 1 MB/s (SC2's condition holds at N = 8) |
| S2-i | **Environment record** | ✅ CPU, RAM, OS build, power plan, `on_ac_power: True` (from `PowerOnline`), toolchain of the last build `MSVC 14.44.35229, Windows SDK 10.0.22621.0` (from the UnrealBuildTool log), masks |

**A side effect worth knowing, measured in S2-a and S2-c with the same workload and binaries:** the uncapped clients made the server's net flush more expensive. C1's median `NetFlushMs` was 6.27 ms with clients at about 2,100 fps and 3.88 ms with clients at 30 fps; C4's was 1.13 ms and 0.88 ms. The ack traffic of a client scales with its frame rate. That is why `run_bench.py` now marks a run invalid when any client runs outside 27.3–33 fps.

**Audit trail for S2:**
- `code-review` (medium) raised 10 findings: 4 important, 6 nits. All fixed except the one about the commit (docs were staged with this commit):
  - **important:** an unreadable report crashed the whole batch and lost the manifest. Reports are now read by `read_json`, which makes the run invalid; the manifest is also rewritten after every run;
  - **important:** `on_ac_power` was derived from `Win32_Battery.BatteryStatus == 2`, which is false on a fully charged laptop on AC. It now reads `root/wmi BatteryStatus.PowerOnline`;
  - **important:** the server quit a fixed 3 s after `Dump`. It now waits until every client has left (each leaves after writing its report), with a 30 s timeout;
  - **important:** validity checked only that clients were not above the cap. It now also requires ≥ 27.3 fps and every client exit code 0;
  - **nit:** `csv_stats.py` failed on a partial capture that has an end header but no metadata row. It now drops such rows and unreadable cells;
  - **nit:** the tick length was a 1/30 constant. It is now read from `NetServerMaxTickRate` when the window opens;
  - **nit:** PIE client worlds would overwrite each other's reports. The director now runs in game worlds only;
  - **nit:** the `-NCLConfig` comment was out of date;
  - **nit:** the compiler string was hard-coded. It is now read from the last UnrealBuildTool log.
- `simplify` (4 parallel read-only reviews: reuse, simplification, efficiency, altitude). Applied:
  - saturation from `GetSaturationAnalytics()` (two reviews independently), which removed `IsSaturated`, its test and the allowance clamp;
  - the total of saturated frames derived from the per-connection counts;
  - the client keeps its last-seen phase in `Phase`, and its "report written" flag is replaced by `bDirecting = false`;
  - `ViewedAvatar`, `WorkloadHashText`, and `SaveJson` that serialises;
  - `FrameSeconds` reused for the busy time;
  - `FreezeChanges()` instead of `SetChangesFrozen(bool)`, and the props loop skipped as a whole when frozen;
  - in `csv_stats.py`, the EVENTS index looked up once per capture, and the dead one-value branch removed;
  - the run constants (`MAP`, `PACKAGED_CLIENT`, `COMMON_ARGS`, `IPNETDRIVER`, `REPGRAPH_INI`) shared through `netlab_env.py`;
  - `csv_stats.summarize` for the per-client rates;
  - the server-log path built once;
  - one loop for `--probe` and `--bench` in `check_job.py`.
- Skipped, with reasons:
  - `-ini:GameUserSettings:…FrameRateLimit=30` instead of `SetMaxFPS`: the user-settings file is loaded, version-checked and saved by every client process, 32 of them sharing one packaged build's `Saved\` folder. The in-game cap is set after all of that and is checked by every run;
  - `BeginCapture(N)` with the CSV profiler's first-frame and end delegates instead of our frame count: our count is explicit and S2-d shows it equals the CSV's;
  - deriving "joined" from the workload's avatars: the director measures connections, so it counts from the connection side;
  - dropping the `NetCostLab/Clients` column in favour of `Replication/NumConnections`: several `Replication` CSV stats are legacy-only (`BaseEngine.ini:3805–3825`);
  - a shared "run a session" helper for `probe_net.py` and `run_bench.py`: their life cycles differ (the probe holds and samples memory, the bench waits for the server to finish);
  - one PowerShell call instead of five: once per batch;
  - logging the window-closed line one frame later: one line in 1,800 frames.

**What S2 taught us:** a setting that is logged as applied is not necessarily in force. The clients' cap was overwritten during engine start-up, and only the measured frame rate in the client reports showed it. Every run now checks the clients' frame rate itself.
