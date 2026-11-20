# 01b · Research Findings (Phase 2): NetCostLab

**Phase:** 2 (Research), completed 2026-11-21 · **Engine:** UE 5.8.4 (CL 58902052), read from `C:\Program Files\Epic Games\UE_5.8\Engine`

**Method:**
- Four investigations: what the launcher engine can run (targets, push model, dedicated servers), the five replication setups in source, measurement (CSV timers, bandwidth accounting, emulated loss, clocks), and existing work on the web.
- Every engine claim cites a path relative to `Engine/` plus a line number in 5.8.4. Claims that drive design decisions were re-checked by hand and are marked ✔.
- One **probe through the machine queue** (P2-1, `09`) tested what the source predicted: a Server-target build attempt, and the editor binary as a dedicated server with headless clients in three setups.
- **[inferred]** marks a conclusion drawn from following code; **[to measure]** a hypothesis that still needs data.

**Why this phase mattered here.** Phase 1 assumed a dedicated-server target, a packaged build for everything, and a gap that nobody had measured. Phase 2 found that **none of those three holds** on this engine. Without it, configuration C2 would have silently measured nothing, and the write-up would have overclaimed.

---

## 1. What Phase 2 changed

| # | Finding | Consequence for NetCostLab |
|---|---|---|
| 1 | **The launcher engine cannot build a Server target.** `[InstalledPlatforms]` lists only Editor and Game types for Win64 (`Config/BaseEngine.ini:3997–4026`); UnrealBuildTool throws "{0} targets are not currently supported from this engine distribution." right after compiling the rules (`Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:1395–1404`) ✔. **Confirmed by probe P2-1** (`09`) | No dedicated-server executable. The server is the editor binary with `-server` (#3) |
| 2 | **Packaged games on the launcher engine have no push model.** `bWithPushModel` defaults to `Type == TargetType.Editor` and needs a unique build environment (`Rules/TargetRules.cs:1516–1527`) ✔, which an installed engine refuses (`Rules/RulesAssembly.cs:710–712`) ✔. With `WITH_PUSH_MODEL=0` the dirty macros are empty (`Net/Core/Public/Net/Core/PushModel/PushModel.h:480–489`) ✔ and `Net.IsPushModelEnabled` does not exist (`PushModel.cpp:5, 434–439`) ✔. The precompiled game `NetCore` object has no copy of the cvar string while the editor DLL does (byte check, `Phase2_Raw_Notes/A` §A2) ✔ | C2 can only exist in the editor binary. **All five configurations therefore use the same editor-binary server**, so they stay comparable |
| 3 | **Only the editor binary can be a dedicated server.** `IsRunningDedicatedServer()` is always false in a packaged Game binary and true in an editor binary started with `-server` (`Core/Public/Misc/CoreMisc.h:147–169`, `Core/Private/Misc/CoreMisc.cpp:250–262`) ✔. **Probe P2-1 ran it** in three setups with headless clients (`09`) | Server = `UnrealEditor-Cmd.exe <project> <map> -server`. Absolute server numbers include editor overhead; only comparisons are claimed (`01` §8) |
| 4 | **A dedicated server sleeps to its 30 Hz cap; a fixed step never waits.** `GameEngine.cpp:1719–1745` uses `NetServerMaxTickRate` (`BaseEngine.ini:1867`: 30) ✔; the idle sleep and `FApp::SetIdleTime` are at `UnrealEngine.cpp:3094–3096, 3156` ✔; P01 verified that a fixed step skips all waiting (`UnrealEngine.cpp:3015–3023`). Bandwidth refill uses the real frame delta (`NetConnection.cpp:5114–5125`) ✔ | **Real-time runs**, not `-deterministic`. Server busy time = delta − idle, published as our own CSV stat |
| 5 | **How Iris is switched on in 5.8.4:** the `Iris` plugin (loads `IrisCore`, `IrisModule.cpp:9–11`), `bCanUseIris=true` for `GameNetDriver` (already in `BaseEngine.ini:346`), and `net.Iris.UseIrisReplication` (default **0**, `Net/Iris/Private/Iris/IrisConfig.cpp:15–16`) or `-UseIrisReplication=1`, which overrides everything (`IrisConfig.cpp:33–43`; `UnrealEngine.cpp:14865–14875`) ✔. Server and client must agree (`NetDriver.cpp:2725, 2755`). **Found by probe P2-1, not by reading:** Iris also needs `net.SubObjects.DefaultUseSubObjectReplicationList=1`; without it the Iris bridge raises a handled ensure (`Engine/Private/Net/Iris/ReplicationSystem/EngineReplicationBridge.cpp:234`) and warns that sub-objects "will not replicate properly". The cvar defaults to false (`Components/ActorComponent.cpp:115–117`) and each actor copies it in its constructor (`Actor.cpp:292`) ✔. The probe logs confirm the selected system directly: "InitBase GameNetDriver … using replication model Generic / BasicReplicationGraph / Iris" (`09`, P2-1) | Every process of a run gets the same `-UseIrisReplication=` value from the run script. The `.uproject` enables `Iris`. `DefaultEngine.ini` sets the sub-object-list cvar under `[SystemSettings]`, for **all five** configurations, so the workload stays identical |
| 6 | **Iris uses push dirtiness only if push model is compiled in and on.** `net.Iris.PushModelMode` (default 2) needs `Net.IsPushModelEnabled` and `WITH_PUSH_MODEL` (`LegacyPushModel.cpp:13–26`; `LegacyPushModel.h:22, 96`) ✔; otherwise objects are force-polled (`Polling/ObjectPoller.cpp:174–230`) | C4 and C5 run with `Net.IsPushModelEnabled=1` (possible only in the editor binary). This also means **Iris in a packaged launcher game always polls**, worth knowing for any team shipping on this engine |
| 7 | **A project prioritiser cannot see which object it is prioritising**, but `SetPrioritizer` calls `AddObject` synchronously. `AddObject`/`Prioritize` receive only internal indices (`Prioritization/NetObjectPrioritizer.h:46–70, 117–134`); the index↔handle mapping is private or protected (`NetObjectFilter.h:254, 270`; private `ReplicationProtocol.h:41`); custom rep tags are test-only (`ReplicationStateDescriptorBuilder.cpp:2317–2333`). `UReplicationSystem::SetPrioritizer` → `FReplicationPrioritization::SetPrioritizer` → `AddObject` in the same call (`ReplicationSystem.cpp:1389–1407`; `ReplicationPrioritization.cpp:414–456`) ✔ | `UNCLGameplayPrioritizer::Assign` captures the index during `SetPrioritizer` and refuses to boost if the capture ever fails. This is a **verified behaviour, not a documented contract**; it is stated as a limit (`01` §8, `02` §4.1) |
| 8 | **The sphere prioritiser can be subclassed from another module.** `Init`, `Deinit`, `Prioritize` are exported (`SphereNetObjectPrioritizer.h:45–47`), and UHT exports the constructors and destructor of MinimalAPI classes (`Intermediate/Build/Win64/UnrealEditor/Inc/IrisCore/UHT/SphereNetObjectPrioritizer.generated.h:76–83`) ✔; its batch helpers are not exported. Its `Init` requires a `USphereNetObjectPrioritizerConfig` (`SphereNetObjectPrioritizer.cpp:12–18`) ✔. Epic's owner-boost prioritiser is the same pattern: sphere + `OwnerPriorityBoost = 2.0` (`SphereWithOwnerBoostNetObjectPrioritizer.h:15–17`, `.cpp:167–176`) ✔ | Our prioritiser = `Super::Prioritize` + an additive focus boost (default 2.0, the same as Epic's owner boost). C5 differs from C4 by one term. **Linking is confirmed only when S6 compiles it** [inferred until then] |
| 9 | **All four systems share one bandwidth accounting path.** Bytes per packet are added to the connection's `OutTotalBytes` and `QueuedBits` (`NetConnection.cpp:2562–2590`) ✔; `IsNetReady()` is the saturation test (`:2731–2751`) ✔, and Iris's data-stream channel checks it too (`Net/Experimental/Iris/DataStreamChannel.cpp:216–217, 247–248, 452`) ✔. The per-connection rate is `clamp(client's ConfiguredInternetSpeed, 1800, server MaxClientRate)` (`World.cpp:7471`; `:7987–7989` for `MaxInternetClientRate`) ✔; defaults are 100,000 B/s (`BaseEngine.ini:1838–1840, 1860–1861`) | **Bytes/s per client = the server's per-connection `OutTotalBytes` delta over the capture**, identical for every system. Changing the cap means setting all three values; Scenario A raises them to 1,000,000 B/s, Scenario B lowers them |
| 10 | **Two CSV timers cover the net flush of every system.** `UNetDriver::TickFlush` wraps itself in `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(NetworkOutgoing)` and the replication decision in `…(ServerReplicateActors)`, which contains the legacy path, the replication driver *and* `InternalIrisUpdate` (`NetDriver.cpp:1172, 1210–1214, 1373–1378`) ✔. `NetBroadcastTickTime`, used by the community benchmarks, is an Insights/stat scope, not CSV (`LevelTick.cpp:1924–1926`) ✔. Several `Replication` CSV metrics are legacy-only (`BaseEngine.ini:3805–3825`, `EnableForNonIrisOnly`) | **Server ms per net tick = `Exclusive/GameThread/ServerReplicateActors` + `Exclusive/GameThread/NetworkOutgoing`**; legacy-only metrics are never used to compare systems. Our per-frame sampling runs at `FWorldDelegates::OnWorldTickEnd`, after the flush (`LevelTick.cpp:2061` vs `:1929`) |
| 11 | **Relevancy can be made equal across systems.** Iris spatially filters every actor class that is not always-relevant or owner-relevant (`Engine/Private/Net/Iris/ReplicationSystem/EngineReplicationBridge.cpp:242–252`) ✔ with an exact cull distance (`NetObjectGridFilter.h:79–80`) ✔; `UBasicReplicationGraph` takes each class's cull distance and update rate from its CDO (`BasicReplicationGraph.cpp:42–51`) ✔; the actor default is 150 m (`Actor.cpp:312`) ✔ | One 150 m cull distance on the classes, used by all systems. (A blog's claim that Iris filters only pawns by default does not match 5.8.4's source) |
| 12 | **Every system reads the connection's view from `GetPlayerViewPoint`**, which on a server uses the camera cache fed by the client (`NetDriver.cpp:5108–5128, 7703–7716`; `PlayerController.cpp:1001–1031`) ✔ | `ANCLPlayerController` overrides `GetPlayerViewPoint` to return the pawn's location, so the view is server-determined and the same for all five configurations |
| 13 | **One clock for all processes:** `FPlatformTime::Cycles64()` is the raw `QueryPerformanceCounter` (`Core/Public/Windows/WindowsPlatformTime.h:39–44`) ✔, which Windows keeps consistent across processes on one machine [web: Microsoft docs] | Server and client events are stamped with `Cycles64()` and joined offline; no time synchronisation is needed on one PC |
| 14 | **Emulated loss is per process and outgoing.** `-PktLoss=<0–100>` (integer percent) is read from the command line or `[PacketSimulationSettings]` (`Net/NetEmulationHelper.cpp:534–560, 628–645, 384–411`) ✔ and applied to outgoing packets (`NetConnection.cpp:2712–2714`) ✔; available in Development (`DO_ENABLE_NET_TEST`, `NetDriver.h:450`) ✔ | "Loss in both directions" = the same `-PktLoss=` on the server and every client. 1, 3 and 5% are exact settings |

**Benchmark-plan changes:**

| # | Finding | Consequence |
|---|---|---|
| B-1 | #3 and #4 | The portfolio's fixed-step, packaged, GPU protocol is replaced for this project by real-time, editor-binary server, headless clients (`01` §7.1), stated in every manifest |
| B-2 | 32 headless clients share the PC with the server | Server on the P-cores (`0xFFFF`), clients on the E-cores (`0xFF0000`); the run script pins itself before each launch so children inherit the mask; client frame rate is checked per run |
| B-3 | An editor-binary client uses ≈ 1.7 GB (probe P2-1), a packaged headless client ≈ 0.35 GB (legacy) to ≈ 0.54 GB (Iris), and packaged clients **do** join the editor-binary server in both systems (Phase 4 check F4, `09`) | **Clients are the packaged Development game.** 32 of them need ≈ 17 GB; 32 editor-binary clients would need ≈ 57 GB on a 32 GB machine |

---

## 2. Launcher engine, server and push model (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| Win64 installed types: Editor (Development, DebugGame), Game (DebugGame, Development, Shipping); no Server, no Client | `Config/BaseEngine.ini:3997–4026` ✔ |
| The type check happens in `CreateRulesObject`, before any module is compiled | `UEBuildTarget.cs:1363–1418` ✔ |
| `bWithPushModel` default `Type == TargetType.Editor`, `[RequiresUniqueBuildEnvironment]` | `Rules/TargetRules.cs:1516–1527` ✔ |
| `WITH_PUSH_MODEL=1/0` global definition | `UEBuildTarget.cs:6573–6579` ✔ |
| Shared-environment violation message; unique environment refused on an installed engine | `UEBuildTarget.cs:1509–1517`; `Rules/RulesAssembly.cs:710–712` ✔ |
| Engine game target is a shared environment with no push override | `Source/UnrealGame.Target.cs` ✔ |
| `IsRunningDedicatedServer()`: false for Game-only builds; `-server` in editor builds | `CoreMisc.h:147–169`; `CoreMisc.cpp:250–262` ✔ |
| Dedicated-server tick cap from `NetServerMaxTickRate` (30) | `GameEngine.cpp:1719–1745`; `BaseEngine.ini:1867` ✔ |
| Iris, Replication Graph: precompiled for editor and game; plugin flags Beta, `EnabledByDefault: false` | `Plugins/Experimental/Iris/Iris.uplugin`; `Plugins/Runtime/ReplicationGraph/ReplicationGraph.uplugin`; `Intermediate/Build/Win64/x64/UnrealGame/Development/IrisCore` ✔ |
| `SetupIrisSupport` adds `UE_WITH_IRIS=1` and an `IrisCore` dependency; `Engine` calls it publicly | `Rules/ModuleRules.cs:1747–1762`; `Runtime/Engine/Engine.Build.cs:514–515` ✔ |

## 3. The replication systems (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| Legacy priority calls the virtual `AActor::GetNetPriority` per viewer | `NetDriver.cpp:5160`; `Actor.h:2835` ✔ |
| Replication Graph does not use `GetNetPriority` or `IsNetRelevantFor` | `ReplicationGraph.h:20` ✔ |
| A replication driver with Iris is a fatal `checkf` | `NetDriver.cpp:7354–7358` ✔ |
| `UBasicReplicationGraph`: "a simple example implementation"; enable via `ReplicationDriverClassName` | `BasicReplicationGraph.h` (class comment) ✔ |
| `net.PushModelSkipUndirtiedReplication` default false | `DataReplication.cpp:75–79` ✔ |
| `AActor`'s own replicated properties are push-based (`ReplicatedMovement`: `COND_SimulatedOrPhysics`, `REPNOTIFY_Always`) | `ActorReplication.cpp:563–581` ✔ |
| Iris default prioritiser = sphere (inner 2,000, outer 10,000; 1.0 / 0.2 / 0.1); the first prioritiser definition is the default | `SphereNetObjectPrioritizer.h:19–35`; `BaseEngine.ini:1493–1495`; `NetObjectPrioritizerDefinitions.h:62–66` ✔ |
| Priorities < 1.0 are not considered that frame; priority accumulates per connection until sent | `NetObjectPrioritizer.h:155–160` ✔; Epic Iris Prioritization docs |
| The connection's own view target and controller get a forced high priority (`Net.RepGraph.ForceConnectionViewerPriority`, default 1) | `Net/Core/Private/Net/Core/Misc/NetCVars.cpp:9–13`; `ReplicationPrioritization.cpp:801–836` ✔ |
| A prioritiser with no objects is never called | `ReplicationPrioritization.cpp:555–567, 760–800` ✔ |
| Iris views: `ViewTarget` handle from `GetConnectionViewTarget()`, position from `GetPlayerViewPoint` | `NetDriver.cpp:7683–7720`; `NetConnection.cpp:861–865` ✔ |
| `FNetHandleManager::GetNetHandle(const UObject*)` is exported | `Net/Core/Public/Net/Core/NetHandle/NetHandleManager.h:30` ✔ |
| Actors start Iris replication in `DispatchBeginPlay`; a deferred variant exists | `Actor.cpp:4768`; `ReplicationSystemUtil.h:203` ✔ |

## 4. Measurement and tooling (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| `NetworkIncoming` (exclusive) around tick dispatch | `LevelTick.cpp:1566–1574` ✔ |
| `NetworkOutgoing` and `ServerReplicateActors` (exclusive) inside `TickFlush` | `NetDriver.cpp:1172, 1210` ✔ |
| CSV names: `<Category>/<Thread>/<Stat>` for timing stats | `Core/Private/ProfilingDebugging/CsvProfiler.cpp:2443–2457` ✔ (to confirm on the first capture) |
| `OnWorldTickEnd` fires after the net flush | `LevelTick.cpp:1929, 2061` ✔ |
| Per-connection public counters `OutTotalBytes`, `InTotalBytes`, `QueuedBits`; `UPlayer::CurrentNetSpeed` | `Engine/Classes/Engine/NetConnection.h:443, 492, 568–587`; `Engine/Classes/Engine/Player.h:26–28` ✔ |
| Iris uses the same bandwidth limits (Epic staff, 2025-10-15) | https://forums.unrealengine.com/t/how-is-server-to-client-bandwidth-controlled-in-iris-in-ue-5-5/2675046 |
| `-ini:`, `-dpcvars=`, `-ExecCmds=` command-line overrides | `ConfigCacheIni.cpp:1714–1723`; `DeviceProfileManager.cpp:737–738`; `UnrealEngine.cpp:2552` ✔ |
| `FApp::GetIdleTime()` public | `Core/Public/Misc/App.h:597` ✔ |

## 5. Existing work, updated (full notes: `Phase2_Raw_Notes/D_existing_work.md`)

| Work | Configs compared | Bandwidth? | Server CPU? | Clients | Prioritiser? | Correctness under loss? |
|---|---|---|---|---|---|---|
| hon454 lab (UE **5.8.3**) | legacy, Replication Graph, Iris (+ 9 legacy steps) | **Yes** (B/s per connection) | Yes (frame, replication ms) | 8 rendering clients, one count | No (Iris priority disabled) | Diagnostic only |
| maguzabi11 Iris-Demo (5.7.1) | Generic vs Iris + role filter | Yes | No | 3 | No | No |
| takennot (5.8, legacy) | baseline vs `GetNetPriority` weights | Yes | No | 1 | **Legacy hook** | No |
| BorMor 100 players (5.7 Preview) | Generic vs Iris (+ patch) | No | Yes (Insights) | 100 on VMs | No | No |
| BorMor forest | Generic vs Iris | No | Yes | 1 | No | No |
| Kieran Newland (2024) | push off/on, skip 0/1 | No | Yes (Insights) | 4 | No | No |
| **This project** | **legacy, + push, Replication Graph, Iris, Iris + gameplay prioritiser** | Yes | Yes (CSV) | **8 / 16 / 32 headless** | **Iris, gameplay focus** | **Yes: position error, staleness, final mismatches, 1–5% loss** |

**The gap, stated conservatively (weaker than the roadmap said):**
- A multi-system comparison with bandwidth **exists** for UE 5.8.3 (hon454). Our C1/C3/C4 comparison re-measures it under a stricter protocol (interleaved runs, pinned cores, three client counts) and adds push model as a separate factor.
- Gameplay-weighted priority **exists** for legacy (`GetNetPriority` override, takennot; Epic's docs).
- We found **no** public Iris prioritiser that uses gameplay state, and **no** published measurement of per-object correctness (position error, staleness) under a bandwidth cap and 1–5% loss for any system.
- A web search cannot prove absence, and studio-internal work is not public. This is a statement about what we found, never "nobody has done this".

The roadmap's gap strength "medium" stands for the prioritiser and correctness part; the comparison part is **weak-to-medium** and is presented as a re-measurement, crediting the prior work. The owner's standing rule applies: the project is kept and narrowed, not swapped.

## 6. Hypothesis status after Phase 2

| # | Hypothesis | Status |
|---|---|---|
| H1 | Legacy CPU is dominated by comparing unchanged properties | Unchanged **[to measure]**. Kieran Newland's single test points the same way |
| H2 | Legacy cost per added connection is roughly constant | Unchanged **[to measure]** |
| H3 | Iris has a lower per-connection slope | Unchanged **[to measure]**; hon454's 8-client result (Iris 2.08 ms vs graph 2.87 ms replication) is one data point |
| H4 | Bytes per client depend mostly on what changes | **Refined:** Iris differs from legacy in its default *prioritiser* (sphere) and its serializers; hon454 saw Iris +3.7% bytes with priority disabled **[to measure]** |
| H5 | Distance-only prioritisation starves gameplay-relevant far objects under a cap | **Supported by code** (the sphere's priority depends on distance only, `SphereNetObjectPrioritizer.cpp:117–160`) **[to measure]** |
| H6 | Loss adds staleness, not permanent divergence | Unchanged **[to measure]**; Iris's resend path is read in S3 |

## 7. Carried into Phase 3

- One editor-binary server for all configurations; packaged headless clients (F4 passed).
- `net.SubObjects.DefaultUseSubObjectReplicationList=1` in `DefaultEngine.ini` for every configuration.
- To check in S1: which modules load after the Iris replication system starts (P2-1f, `IrisCoreModule.cpp:141–160`).
- Real-time runs at 30 Hz; server busy ms from `FApp`; CSV timers `ServerReplicateActors` + `NetworkOutgoing`.
- Bytes from per-connection `OutTotalBytes`, sampled at `OnWorldTickEnd`; saturation from bytes per frame vs `CurrentNetSpeed × dt`.
- Server-determined views (`GetPlayerViewPoint` override); 150 m cull distance everywhere.
- The prioritiser: subclass of the sphere prioritiser, additive focus boost 2.0, identity through `Assign` around `SetPrioritizer`, boost loop as a pure tested function.
- Shared QPC clock; loss with `-PktLoss=` on every process.

**Raw research notes**, with every quote, are in [Phase2_Raw_Notes/](Phase2_Raw_Notes/README.md) as an audit trail. This file is authoritative: everything the design depends on is restated above with its evidence.
