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
