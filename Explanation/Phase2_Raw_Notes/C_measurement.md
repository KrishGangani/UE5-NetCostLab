> **Raw Phase 2 research notes (C: what to measure and how, on server and clients), UE 5.8.4, 2026-11-21.** Evidence trail. **The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md); treat anything not restated there as unverified.** Paths are relative to `Engine/`.

# P05 NetCostLab: Phase 2 research, part C: measurement

## C1. Where the server spends its net tick, as seen by the CSV profiler

World tick order (`Source/Runtime/Engine/Private/LevelTick.cpp`):
- `:1566–1574` incoming: `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(NetworkIncoming)` around `BroadcastTickDispatch` / `BroadcastPostTickDispatch`.
- `:1922–1937` outgoing: `TRACE_CPUPROFILER_EVENT_SCOPE(NetBroadcastTickTime)` + `SCOPE_CYCLE_COUNTER(STAT_NetBroadcastTickTime)` around `BroadcastPreTickFlush` + `BroadcastTickFlush`, then `BroadcastPostTickFlush`. `NetBroadcastTickTime` is the Insights/stat scope used by BorMor and Kieran Newland; it is **not** a CSV stat.

`UNetDriver::TickFlush` (`Source/Runtime/Engine/Private/NetDriver.cpp:1168–1330`):
- `:1172` `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(NetworkOutgoing);` around the whole function.
- `:1210` `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ServerReplicateActors);`, then `:1212–1214` `if (ReplicationSystem) InternalIrisUpdateTransactional(DeltaSeconds); else if (ClientConnections.Num() > 0) ... ServerReplicateActors(DeltaSeconds);`. A replication driver (Replication Graph) is called from inside `ServerReplicateActors`.
- `:1373–1378` `InternalIrisUpdate`: `UpdateIrisReplicationViews(); SendClientMoveAdjustments(); ReplicationSystem->NetUpdate(DeltaSeconds);`.
- `:1261–1308` the connection ticks (packets are written and sent here for every system); parallel only for Iris when `FIrisNetDriverConfig::bCanUseParallelNetConnectionTick` (default false, `Engine.h:294`) and `MinConnectionsForParallelTick` allow it.
- `:1772–1780` `PostTickFlush`: `ReplicationSystem->PostSendUpdate()` (Iris only, outside both CSV scopes; small).

**So for every configuration:** `Exclusive/GameThread/ServerReplicateActors` = deciding and serialising what to send (legacy, graph or Iris), and `Exclusive/GameThread/NetworkOutgoing` = the rest of `TickFlush`, mostly connection ticks and packet sends. Their sum is the server's net-flush time per frame.

CSV column names: timing stats get a `<Thread>/` prefix and categorised stats a `<Category>/` prefix (`Source/Runtime/Core/Private/ProfilingDebugging/CsvProfiler.cpp:2443–2457`); exclusive stats use the `Exclusive` category (`:73`, `:2708–2720`). Hence `Exclusive/GameThread/ServerReplicateActors` **[inferred from the naming code; to confirm on the first capture]**.

The `Replication` CSV category from the network metrics database (`Config/BaseEngine.ini:3796–3837`): listener `UNetworkMetricsCSV_Replication` with `IntervalSeconds=0` (every frame) for `OutRate`, `InRate`, `SatConnections`, `NumConnections`, `OutKBytes`, `ServerReplicateActorTimeMS`, `OutPacketsLost`, and others. Several are marked `EnableMode=ENetworkMetricEnableMode::EnableForNonIrisOnly` (e.g. `GatherPrioritizeTimeMS`, `ReplicateActorTimeMS`, `NumOpenChannels`), so they are **not comparable across systems**. `OutRate`/`InRate` are bytes per second over a stat period (`NetDriver.cpp:8869–8870`). The Iris category `CSV_DEFINE_CATEGORY(Iris, WITH_SERVER_CODE)` (`Net/Iris/Private/Iris/IrisConfig.cpp:55`) adds Iris-only timers.

Idle time: a dedicated server sleeps away the rest of its tick and records it in `FApp::SetIdleTime` (`UnrealEngine.cpp:3094–3096, 3156`); `FApp::GetIdleTime()` and `FApp::GetDeltaTime()` are public (`Core/Public/Misc/App.h:597`). **Busy time = delta − idle** can be published by game code as one more CSV stat.

## C2. Bandwidth: one accounting path for all four systems

- Per connection, every packet: `NetConnection.cpp:2562` `PacketBytes = SendBuffer.GetNumBytes() + PacketOverhead`; `:2573–2583` `QueuedBits += PacketBytes * 8`; `:2585–2590` `OutBytes += PacketBytes; OutTotalBytes += PacketBytes; Driver->OutBytes += ...; GNetOutBytes += ...`.
- The counters are public members (`Engine/Classes/Engine/NetConnection.h:525` `public:`; `:568–587` `InBytes, OutBytes`, `InTotalBytes, OutTotalBytes`, `InBytesPerSecond, OutBytesPerSecond`, `InTotalPacketsLost, OutTotalPacketsLost`). `int32`: 2^31 bytes ≈ 2.1 GB, far above a 60 s run at 1 MB/s.
- Refill once per connection tick (`NetConnection.cpp:5114–5140`): `DeltaBits = CurrentNetSpeed * BandwidthDeltaTime * 8.f`, with `BandwidthDeltaTime` clamped to `1/DesiredTickRate`; at most two ticks of credit (`AllowedLag`).
- Saturation test: `UNetConnection::IsNetReady()` (`:2731–2751`) returns `QueuedBits + SendBuffer.GetNumBits() <= 0`, unless `net.DisableBandwithThrottling` (non-Shipping, `:101`, `:139–142`) or congestion control is set.
- **Iris uses the same test:** `Engine/Private/Net/Experimental/Iris/DataStreamChannel.cpp:216–217, 247–248` `if (!IsNetReady()) ...`, and `:452` keeps writing only `while ... IsNetReady() && !IsPacketWindowFull()`. Epic staff said the same: "Currently, Iris uses the same configurable bandwidth limits as the default replication system" (Alex Koumandarakis, 2025-10-15, https://forums.unrealengine.com/t/how-is-server-to-client-bandwidth-controlled-in-iris-in-ue-5-5/2675046). Iris also caps packets per batch: `net.Iris.ReplicationWriterMaxAllowedPacketsIfNotHugeObject` (`DataStreamChannel.cpp:48`) and keeps a packet-window safety margin, `net.Iris.PacketSequenceSafetyMargin` (`:37–38`).
- **Where the per-connection rate comes from:**
  - The client asks for `UPlayer::ConfiguredInternetSpeed` (or `ConfiguredLanSpeed` with `?LAN`) (`NetConnection.cpp:588–596, 704–711`) and sends it with `NMT_Netspeed` (`PendingNetGame.cpp:396`).
  - The server clamps it: `World.cpp:7471` `Connection->CurrentNetSpeed = FMath::Clamp(Rate, 1800, NetDriver->MaxClientRate);`. For a non-LAN game, `World.cpp:7987–7989` lowers `MaxClientRate` to `MaxInternetClientRate` when that is smaller.
  - Defaults: `BaseEngine.ini:1838–1840` `[/Script/Engine.Player] ConfiguredInternetSpeed=100000, ConfiguredLanSpeed=100000`; `:1860–1861` `MaxClientRate=100000, MaxInternetClientRate=100000` (bytes per second).
  - So **changing the cap means setting all three** (client request, server max, server internet max). Scenario A raises them; Scenario B lowers them.

## C3. Emulated loss

- `DO_ENABLE_NET_TEST` is `!(UE_BUILD_SHIPPING)` (`Engine/Classes/Engine/NetDriver.h:450`): available in Development builds.
- `FPacketSimulationSettings` (`NetDriver.h:456–590`): `PktLoss` "% of packets dropped (i.e. 0 = None, 100 = All)" (integer), `PktIncomingLoss`, `PktLag`, `PktLagVariance`, `PktLagMin/Max`, `PktDup`, `PktOrder`, `PktJitter`, `PktBufferBloatInMS`, and others.
- Sources of the settings (`Source/Runtime/Engine/Private/Net/NetEmulationHelper.cpp`): ini section `[PacketSimulationSettings]` in Engine.ini (`:384–411`, `:490–505`, optional `<NetDriverDefinition>` prefix); command line `-PktLoss=` etc. (`:534–560`, `:628–645`, the same prefix allowed); profiles `[PacketSimulationProfile.<Name>]` and `-PktEmulationProfile=` (`:420–445`, `:541–545`); console `NetEmulation.PktLoss` (`:341–353`).
- Applied at init with the command line winning over ini (`NetDriver.cpp:764–790`).
- The drop decision for outgoing packets: `NetConnection.cpp:2712–2714` `PacketSimulationSettings.PktLoss > 0 && ShouldDropPacketOfSize(NumBits) && FMath::FRand() * 100.f < PktLoss`. Each process drops **its own outgoing** packets, so loss "in both directions" means passing `-PktLoss=` to the server and to every client.

## C4. A clock shared by all processes

- `Core/Public/Windows/WindowsPlatformTime.h:23–30`: `Seconds()` = `QueryPerformanceCounter × SecondsPerCycle + GetSecondsTimeOffset()`, where the offset is the constant `16777216.0` (`:46–48`).
- `:39–44`: `Cycles64()` returns the raw `QueryPerformanceCounter` value.
- Microsoft: QPC time stamps are consistent across processes on one machine ("Acquiring high-resolution time stamps", https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps) **[web; not re-measured here]**.
- **Use:** server and clients stamp events with `FPlatformTime::Cycles64()`; the offline join needs no clock synchronisation at all. This only works because everything runs on one PC, which is our setup anyway.

## C5. Which "view" each system uses for relevancy and priority

- Legacy and Replication Graph: `FNetViewer::FNetViewer(UNetConnection*, float)` (`NetDriver.cpp:5108–5128`): `ViewLocation = ViewTarget->GetActorLocation()`, then `ViewingController->GetPlayerViewPoint(ViewLocation, ViewRotation)` if there is a controller.
- Iris: `UpdateIrisReplicationViews` (`NetDriver.cpp:7683–7720`): the same two steps.
- `APlayerController::GetPlayerViewPoint` (`PlayerController.cpp:1001–1031`): spectators use the client-synced location; otherwise the **camera cache** if it was ever updated (it is fed by the client's camera updates), else the view target's location.
- **Consequence:** with real clients, the server-side view depends on what the clients send. Overriding `GetPlayerViewPoint` (virtual) in our player controller to return the possessed pawn's location makes the view purely server-determined and **identical for all five configurations**.

## C6. Command-line overrides used by the run scripts

- `-ini:<IniName>:[Section]:Key=Value,[Section2]:Key2=Value2` (`Core/Private/Misc/ConfigCacheIni.cpp:1714–1723`), compiled in when `ALLOW_INI_OVERRIDE_FROM_COMMANDLINE` = `(UE_SERVER || !(UE_BUILD_SHIPPING))` (`Core/Public/Misc/ConfigCacheIni.h:57`).
- `-dpcvars=Name=Value,...` applied before device profiles (`Engine/Private/DeviceProfiles/DeviceProfileManager.cpp:737–738`). P01 found cheat cvars cannot be set this way; the cvars used here (`Net.IsPushModelEnabled`, `net.Iris.UseIrisReplication`, `net.PushModelSkipUndirtiedReplication`) are not cheat cvars.
- `-ExecCmds=a,b` queued as deferred commands (`Engine/Private/UnrealEngine.cpp:2552`); it also works on a dedicated server.
- CSV capture on an event (from P01, `01b` §4): `-csvStartOnEvent=`, `-csvCaptureOnEventFrameCount=`, `-ExitAfterCsvProfiling`.
- `t.MaxFPS` (`UnrealEngine.cpp:12137`) caps client frame rate when no fixed step is used.

## C7. Networking Insights

- `NetTrace.SetTraceVerbosity` console command (`Net/Core/Private/Net/Core/Trace/NetTraceInternal.cpp:936–945`).
- Docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-insights-in-unreal-engine (snippet only in [research/03](../Research/03_existing_solutions_gap_analysis.md)). It is used for drill-downs only, never during timed runs (tracing has overhead).
