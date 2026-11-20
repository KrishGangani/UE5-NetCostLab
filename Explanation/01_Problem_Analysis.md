# 01 · Problem Analysis: NetCostLab

**Phase:** 1 (Problem Analysis), written 2026-11-21 and updated after Phase 2 the same day (evidence in [01b_Research_Findings.md](01b_Research_Findings.md)).
**Engine:** UE **5.8.4** (CL 58902052), launcher build at `C:\Program Files\Epic Games\UE_5.8`.
**Status of every claim:** **[verified]** (read in engine source or a cited source), **[hypothesis]** (to be confirmed by measurement), or **[target]** (a pass/fail threshold, not a result).

---

## 1. The problem in one paragraph

A multiplayer server sends the game's state to every connected player, many times per second. In Unreal, every replicated actor is a candidate for every connection on every net tick. So the server's CPU time and its outgoing bandwidth grow with **connections × replicated actors**, not with how much actually changed or how much it matters to each player.

Unreal ships four ways to fight this: the push model, the Replication Graph, Iris, and the prioritisation inside each of them. A team picking one has little to go on except Epic's descriptions and a few community tests. Those tests measure different workloads, rarely report bandwidth together with CPU, and none tests a prioritiser that knows anything about gameplay. And when bandwidth runs out, the shipped Iris prioritisers decide what to send by **distance only**: a far enemy that is shooting at you waits as long as a far crate.

## 2. Where this happens in real games

| Evidence | What it shows | Source |
|---|---|---|
| Fortnite: Battle Royale (Epic, 2017) | The server had to handle 100 players at 20 Hz "and minimize bandwidth". Fixes included capping how many players get updates per frame (25 in the lobby, 50 in game) and rate-limiting client movement | [research/02](Research/02_technical_challenges.md) [T3-A] (raw text checked) |
| Replication Graph blog (Epic) | "up to 100 clients… 50,000 synced actors": testing every actor against every client every frame "does not scale well" | [research/02](Research/02_technical_challenges.md) [T3-B] (raw text checked) |
| Apex Legends (Respawn, not UE) | ~60 kB/s per client at 20 Hz, ~3× that at 60 Hz; "slow-mo" servers at ~200 ms per frame against a 50 ms budget | [research/02](Research/02_technical_challenges.md) [T3-G] |
| Halo: Reach (Bungie, not UE) | A leftover priority boost on idle grenades cost about 20% of bandwidth (as reported by a secondary source) | [research/02](Research/02_technical_challenges.md) [T3-H] |
| UE 5.1 forum, 200 mock clients | ~120 ms+ in `TickFlush`, roughly linear in connections, with stock replication | [research/02](Research/02_technical_challenges.md) [T3-F] (snippet only) |
| Job postings, 2026 | Riot: "bandwidth optimization, server density". Epic: "network replication systems (client-server prediction, state synchronization, lag compensation)" | [research/01](Research/01_job_market.md) §7 (R1, E1) |

**What these share:** server cost is spent per connection and per actor, and the hard part is deciding what each connection needs most when there is not enough time or bandwidth for everything.

## 3. What already exists, and why it doesn't fully settle the question

### 3.1 Engine features (UE 5.8.4)

| Existing tool | What it does | What it does not do | Evidence |
|---|---|---|---|
| Generic (legacy) replication | Per actor: compares replicated properties with the last sent state at `NetUpdateFrequency`; per connection: relevancy, priority (`AActor::GetNetPriority`, a virtual hook), bandwidth-limited send | Polls every property of every actor it considers, changed or not | [verified] `NetDriver.cpp:5160` (calls `GetNetPriority`); `Actor.h:2835` |
| Push model | Game code marks properties dirty (`MARK_PROPERTY_DIRTY_FROM_NAME`); the server skips comparing clean ones. Off by default (`Net.IsPushModelEnabled = false`) | **Not compiled into packaged Game builds on the launcher engine** (`WITH_PUSH_MODEL=0`, see `01b` #2). It saves CPU only where properties rarely change | [verified] `PushModel.cpp:5, 434–439`; `PushModel.h:480–489`; `TargetRules.cs:1521–1524` |
| Replication Graph | Server-side graph of persistent nodes that share actor lists across connections (grid spatialisation, always-relevant lists). `UBasicReplicationGraph` is the shipped example | Ignores `GetNetPriority` and `IsNetRelevantFor`. Cannot be combined with Iris. Plugin flag Beta; Epic says it is not deprecated | [verified] `ReplicationGraph.h:20`; `NetDriver.cpp:7354–7358`; `BasicReplicationGraph.h` |
| Iris | A separate replication system: quantised state copied once, filters and prioritisers per connection, push-based dirtiness when the push model is on. Opt-in: `net.Iris.UseIrisReplication` defaults to 0 | Its shipped prioritisers are geometric: sphere (distance), field of view, owner boost, and a count limiter. None uses gameplay state | [verified] `IrisConfig.cpp:15–16`; `BaseEngine.ini:1493–1495`; `Prioritization/*.h` |
| Networking Insights, CSV profiler | Packet-level traces; per-frame CSV timers (`ServerReplicateActors`, `NetworkOutgoing`) and the `Replication` CSV category | Measurement only | [verified] `NetDriver.cpp:1172, 1210`; `BaseEngine.ini:3802–3837` |

### 3.2 Public measurements and community work

| Work | What it measures | What it leaves open | Evidence |
|---|---|---|---|
| BorMor, "100 Players in One Place" (UE 5.7 Preview) | Generic vs Iris server frame time with 100 clients on VMs (83.9 → 60.1 ms per `FEngineLoop::Tick`) | No bandwidth; no Replication Graph, push model or prioritiser | [research/02](Research/02_technical_challenges.md) [T3-E]; https://bormor.dev/posts/iris-one-hundred-players/ |
| BorMor, "Forest of Ten Thousand Trees" | Generic vs Iris server frame rate with 10,201 replicated trees, one client | No bandwidth; one client | https://bormor.dev/posts/iris-forest-of-ten-thousand-trees/ |
| hon454, "UE dedicated server optimization lab" (**UE 5.8.3**) | Nine stacked legacy optimisations, then legacy vs Replication Graph vs Iris: server frame time, replication time, **bytes/s per connection**, 8 rendered clients on one PC, 3 runs, median | **Closest prior work.** One client count (8), no push-model factor, no custom prioritiser; Iris run with distance priority turned off; loss only in a side diagnostic | https://github.com/hon454/ue-dedicated-server-optimization-lab |
| maguzabi11, "Iris-Demo" (UE 5.7.1) | Generic vs Iris with a role-based Iris filter: bytes/s per connection, 3 clients | Policy and system change together; no server CPU; no prioritiser | https://github.com/maguzabi11/Iris-Demo |
| takennot, "ReplicationOptimization" (UE 5.8, legacy) | A `GetNetPriority` override with distance, "critical object" and waiting-time weights: bytes/s and arrival rates, 1 remote client | Legacy only (no Iris prioritiser); no server CPU; no loss; no position error | https://github.com/takennot/ReplicationOptimization |
| Kieran Newland, "Push Model Networking" (2024) | `NetBroadcastTickTime` with and without push model (4 clients) | One workload, no bandwidth | https://www.kierannewland.co.uk/2024/01/15/push-model-networking-unreal-engine/ |
| Epic docs: Actor Priority; Iris Prioritization | How to override `GetNetPriority`; how to write and register a custom `UNetObjectPrioritizer` | Guidance, not measurements | https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-priority-in-unreal-engine · https://dev.epicgames.com/documentation/en-us/unreal-engine/iris-prioritization-in-unreal-engine |

### 3.3 The gap, restated after Phase 2 (weaker than the roadmap said)

The roadmap said the only public Iris benchmark measures frame time, not bandwidth, and compares no configurations. **Phase 2 found that is no longer true:** the hon454 lab compares legacy, Replication Graph and Iris on UE 5.8.3 with bandwidth. And gameplay-weighted priority is an old idea: legacy has always had the `GetNetPriority` hook, and takennot measured one.

What we did **not** find, in any one place:
1. **Push model as an isolated factor** on the same workload, with both CPU and bytes.
2. **Scaling slopes** over 8/16/32 connections for all setups (the 5.8.3 lab used one client count).
3. A **gameplay-aware Iris `UNetObjectPrioritizer`** (Iris removed the per-actor priority hook; the shipped prioritisers are geometric), measured on **correctness**: position error and staleness per object class under a bandwidth cap.
4. **Correctness under 1–5% emulated loss** measured on a clock shared by server and clients.

**Gap strength: medium for items 3–4; weak-to-medium for items 1–2.** We present the five-configuration comparison as a stricter, reproducible re-measurement that builds on the prior work above, and the prioritiser plus the correctness measurements as the new part. We never claim "nobody has done this"; this is a statement about what we found.

## 4. Root-cause hypotheses

Each hypothesis gets a measurement that can confirm or reject it. The baselines (C1–C4) are captured before the prioritiser exists.

| # | Hypothesis | Why we suspect it | How we test it |
|---|---|---|---|
| H1 | **Legacy server CPU is dominated by comparing properties that did not change**, so it scales with replicated actors × properties | Legacy compares each considered actor's properties at its `NetUpdateFrequency`; `Net.IsPushModelEnabled` exists to skip that | C1 vs C2 `ServerReplicateActors` ms on a workload where most properties rarely change (768 props, drone loadout fields). If H1 holds, C2 is clearly cheaper and the saving shrinks when only fast-changing actors exist |
| H2 | **Legacy cost per added connection is roughly constant**, because each connection gets its own relevancy and priority pass over the actor list | The per-connection loop in `ServerReplicateActors`; the 200-client forum report was "roughly linear" | The slope of server ms vs N (8/16/32) for C1/C2, and whether C3 (shared graph lists) has a lower slope |
| H3 | **Iris has a lower per-connection slope** than legacy, because state is copied and quantised once and shared across connections | Epic's Iris introduction; BorMor's frame-time gain at 100 clients | Slope of C4 vs C2 (both with push dirtiness) |
| H4 | **Bytes per client depend mostly on what changes, not on the replication system** | All four setups send the same properties over the same `UNetConnection` and bandwidth accounting (`01b` #9) | Bytes/s per client in the uncapped scenario: C1 ≈ C2 ≈ C3; any Iris difference comes from its serializers (quantisation, delta compression) and its default priority, which we separate in Phase 8 |
| H5 | **Under a bandwidth cap, distance-only prioritisation starves gameplay-relevant far objects** | The sphere prioritiser computes priority from distance alone (`SphereNetObjectPrioritizer.cpp:117–160`); Halo's grenade-boost story shows priority mistakes are expensive | Capped scenario: p95 position error and staleness of drones *attacking this player* vs ambient drones, C4 vs C5 |
| H6 | **Packet loss adds staleness, not permanent divergence**, because lost property state is resent until acknowledged | Both systems retransmit unacknowledged state (to be read in Phase 5 for Iris) | 1/3/5% loss: the final-state mismatch count after a settle period should be 0 in every configuration, while staleness grows with loss |

**Not causes we accept without checking:**
- "The clients are the bottleneck." The clients share the PC. Every run records each client's frame rate and the server's client count, and a run where any client falls below 90% of its target frame rate is flagged.
- "Insights or logging overhead." Trace is off in timed runs (`-traceautostart=0`); per-event logging is written to memory and flushed after the capture window.

## 5. The cost of leaving it unsolved

A server at 30 Hz has **33.3 ms** per tick for everything: gameplay, physics, AI and networking. Fortnite targeted 20 Hz (50 ms) with 100 players [T3-A].

*Arithmetic illustration only, not a measurement:* if the net tick costs *a* ms per connection, then 32 connections cost 32 × *a*. At a hypothetical *a* = 0.5 ms that is 16 ms, half the tick, before any gameplay runs. Halving *a* is the difference between a server that holds its tick rate and one that drops to "slow-mo" like the Apex example [T3-G].

When this is ignored, teams typically:
- lower the player count or the tick rate;
- rent more servers per match;
- or ship with rubber-banding that players notice first on the enemies that matter most.

## 6. What "solved" means: success criteria, defined now

### 6.1 The workload (identical in every run)

All gameplay runs on the server. Clients send no input, so the workload does not depend on the clients.

- **Arena:** 200 m × 200 m, an engine template map; everything is spawned by code from a **fixed seed**.
- **Players:** N ∈ {8, 16, 32} headless clients. Each has an avatar (an actor, not a possessed pawn) that the **server** moves along a seeded loop (radius 20–80 m, 3 m/s). The avatar is the player controller's view target, and the view used for relevancy and priority is that avatar, for every configuration (`02` §4.2).
- **Drones:** 256 actors on seeded loops (4–6 m/s), `NetUpdateFrequency` 30 Hz, replicated movement plus gameplay state: `Health`, `Mode` (patrol/attack), `TargetSlot` (the player it attacks) and four rarely changing loadout fields. Every 2 s a seeded schedule retargets some drones, so about a quarter are attacking a specific player at any moment.
- **Props:** 768 stationary interactive props (doors, pickups), four replicated fields each, `NetUpdateFrequency` 5 Hz, each toggling state on a seeded schedule about once per 20 s.
- **Relevancy:** a 150 m cull distance for drones and props in every configuration (legacy `NetCullDistanceSquared`, `UBasicReplicationGraph` grid, Iris grid filter with exact cull distance).
- **Server tick:** 30 Hz (`NetServerMaxTickRate`, engine default).
- **Push model calls:** the workload code always calls `MARK_PROPERTY_DIRTY_FROM_NAME` when it changes a field. Whether that does anything is decided by the configuration, so the game code is the same everywhere.

### 6.2 Compared configurations (one server binary, switched by command line)

| ID | Configuration | Why it is included |
|---|---|---|
| C1 | Generic replication, push model off | What most projects run |
| C2 | C1 + push model (`Net.IsPushModelEnabled=1`, `net.PushModelSkipUndirtiedReplication=1`) | Epic's CPU fix for the generic path; isolates push dirtiness |
| C3 | C2 + Replication Graph (`UBasicReplicationGraph`) | The shipped graph, out of the box. A game-specific graph could do better; we say so |
| C4 | Iris (`-UseIrisReplication=1`) with push dirtiness, default filters and the default sphere prioritiser | Epic's new system as shipped |
| **C5** | **C4 + our gameplay-aware prioritiser** for drones | Our contribution: the same distance model plus a boost for drones attacking the connection's own avatar |
| C6 | C2 + the **same focus rule in the legacy hook**: drones override `AActor::GetNetPriority` and, for the connection whose view target they attack, return the engine's own view-target priority (`NetPriority × Time × 4`, no distance scaling: `ActorReplication.cpp:56–59`) | **The fair existing solution for C5.** Legacy has always allowed gameplay-aware priority through this virtual hook (§3); comparing C5 only with distance-only Iris would set it against a strawman. Added on the owner's decision (2026-11-21) |

Each step from C1 to C5 changes **one** factor: push, then the graph, then the system, then the prioritiser. C3 → C4 changes the system (the graph is not used with Iris), so C2 is also compared with C4 directly. **C6 differs from C2 by one factor too (the focus rule), so C6 vs C2 and C5 vs C4 measure the same gameplay rule on both systems,** each expressed in its own system's convention (the legacy view-target ×4; Iris's owner-boost +2.0).

### 6.3 Scenarios

| Scenario | Bandwidth per client | Loss | Clients | Configs | Answers |
|---|---|---|---|---|---|
| **A: open** | raised to 1,000,000 B/s, so nothing is capped | 0% | 8, 16, 32 | C1–C6 | CPU and bytes/s per client, scaling slopes (H1–H4) |
| **B: capped** | 24,000 B/s (calibrated once in Phase 5 on C4, so that C4 is saturated in ≥ 90% of server frames; then frozen) | 0, 1, 3, 5% both directions | 16 | C2, C4, C5, C6 (C2 is C6's own baseline; C1 differs from C2 only in CPU, not in what is sent) | Who gets the bandwidth, correctness under loss (H5, H6) |

### 6.4 Metrics

| Metric | Unit | Tool |
|---|---|---|
| Server net-tick time per frame: `ServerReplicateActors` + `NetworkOutgoing` (exclusive CSV timers), median/p95/p99 | ms | CSV profiler on the server; Python stats |
| Server busy time per frame (frame delta minus idle sleep) | ms | Our CSV stat from `FApp::GetDeltaTime()` − `FApp::GetIdleTime()` |
| Bytes sent per client per second over the capture window | B/s | Server-side `UNetConnection::OutTotalBytes` delta per connection; client-side `InTotalBytes` as a cross-check |
| Scaling slope | ms per added client; B/s per added client (server total) | Least-squares fit over N = 8/16/32 |
| Saturated connection-frames (`IsNetReady() == false`) | % | Our per-frame server sample |
| Position error of each drone on each client vs the server at the same instant | cm | Client and server event logs on the shared clock (`FPlatformTime::Cycles64`, QueryPerformanceCounter) joined offline |
| Staleness of discrete state (server change → client receive) | ms | Same logs |
| Final-state mismatches after a 5 s settle with no changes | count | End-of-run state dump on server and clients |

Position error and staleness are reported separately for **threat drones** (attacking this client's avatar) and **ambient drones**.

### 6.5 Pass/fail criteria [target]

| # | Criterion | Threshold |
|---|---|---|
| SC1 | **Same workload everywhere** | The server's workload hash (spawn seeds plus change schedule) is identical for every configuration at a given N, and every client stays connected through every capture (0 disconnects, 0 timeouts) |
| SC2 | **Scenario A is really uncapped** | Saturated connection-frames ≤ 1% in every Scenario A run. Otherwise bytes/s would measure the cap, and the run is invalid |
| SC3 | **Full coverage, honest slopes** | All 18 cells (6 configs × 3 client counts) have ≥ 6 valid runs. Every slope is reported with its run-to-run range. Nothing is extrapolated beyond 32 clients |
| SC4 | **The prioritiser helps the objects that matter** | Scenario B, 0% loss: p95 position error of threat drones on their target's client, **C5 ≤ 0.5 × C4**, with non-overlapping run ranges. If not met, we report it and analyse why. No reframing |
| SC5 | **The price is bounded** | Scenario B, 0% loss: p95 position error of ambient drones, **C5 ≤ 1.5 × C4** |
| SC6 | **The prioritiser is cheap** | Scenario A, N = 32: C5 median server net-tick time ≤ C4 median **+ 5%** |
| SC7 | **Correct under loss** | 1, 3 and 5% loss: **0** final-state mismatches in every run of C2, C4, C5 and C6, and C5's threat-drone staleness p95 ≤ C4's at each loss level |
| SC8 | **The same rule on legacy, reported** | Every Scenario B cell reports C6 vs C2 on the SC4/SC5 metrics next to C5 vs C4. Not pass/fail: it answers whether the Iris prioritiser does at least what the legacy hook does, and we report the answer whichever way it falls |

H1–H4 are answered with data and reported either way. They are findings about Epic's systems, not pass/fail criteria for our code.

## 7. Measurement plan (summary; the full protocol goes in `09_Testing_And_Benchmarks.md`)

### 7.1 What differs from the portfolio's standard protocol, and why

The portfolio protocol is a packaged Development build, a fixed step with `-deterministic`, a real GPU and a 1280×720 window. Three launcher-engine facts and one networking fact force changes (`01b` #1–#4):

| Standard | This project | Reason |
|---|---|---|
| Packaged Development build | **Server:** the Development **editor binary** in dedicated-server mode (`UnrealEditor-Cmd.exe <project> <map> -server`), uncooked. **Clients:** the packaged Development game with `-nullrhi`. Phase 4 check F4 showed it joins the editor-binary server in both systems, at ≈ 0.35–0.54 GB per client, against ≈ 1.7 GB for an editor-binary client (`09` F-4, P2-1e) | The launcher engine cannot build a Server target; a packaged Game binary can never run as a dedicated server; and the packaged game has no push model, so C2 could not exist (`01b` #1–#3) |
| Fixed step `-deterministic -FPS=n` | **Real time**, server at its 30 Hz cap, clients capped at 30 fps | In fixed-step mode the engine never waits, so a server would run faster than real time while bandwidth refill and timeouts use real time (`01b` #4) |
| Real GPU, windowed 1280×720 | No rendering anywhere: the server has no RHI, the clients use `-nullrhi` | Nothing measured here is rendered, and 32 rendering clients would not fit on one GPU |
| Pin to the P-cores | **Server pinned to the P-cores** (mask `0xFFFF`), **clients pinned to the E-cores** (`0xFF0000`) | The server is what we measure; the clients must not steal its cores. Both masks go in every manifest |

What stays the same: the capture window is opened and closed by the server itself (`CSV_EVENT(BenchStart)`, `-csvStartOnEvent=BenchStart -csvCaptureOnEventFrameCount=1800`). `-ExitAfterCsvProfiling` is **not** used: it would end the server the moment the capture ends (`CsvProfiler.cpp:4309–4312`), before the settle period and the state dump that SC7 needs, so the server's director ends the run itself; one discarded priming run per batch; interleaved runs with the configuration order rotated each round; runs per combination = number of configurations in that scenario (6 in A, 4 in B); median, p95 and p99 per run, then the median across runs with [min–max]; an environment record with every batch.

### 7.2 Run shape

Every run is **one queue job** that starts the server, waits until it listens, starts all N clients, waits until all N have joined, warms up for 20 s, captures 60 s (1,800 server frames), lets state settle for 5 s, then stops everything. Logs and CSVs go to `Benchmarks\Raw\`; manifests (exact command lines, environment, masks, per-run statistics) are committed.

**Rough cost:** Scenario A is 108 runs (6 configs × 3 N × 6) + 1 priming run, Scenario B is 64 (4 × 4 loss × 4) + 1, each about 3 minutes including start-up: about 9 hours of queue time, scripted and unattended, split into queue jobs of about 30 minutes so other projects can interleave. Every run's server and clients always run inside one job.

## 8. Limits we accept up front

- **Absolute numbers come from an editor-build server, not a shipping server.** It carries editor-only data and modules. Comparisons between configurations are fair (same binary), but absolute ms would be lower on a real Server target.
- **One PC, loopback network, ≤ 32 clients.** Latency is near zero unless emulated. We report slopes over 8–32 and make no claim about 100 players.
- **`UBasicReplicationGraph` is Epic's example graph.** A game-specific graph might do better; C3 shows the out-of-the-box graph only.
- **No dormancy in the workload.** Dormancy is the biggest single legacy optimisation [T3-C]; it is orthogonal to the questions here and is left as a documented extension.
- **The prioritiser relies on one engine behaviour that is verified in source but not documented:** `UReplicationSystem::SetPrioritizer` calls the prioritiser's `AddObject` synchronously (`01b` #7). The code checks this at run time and refuses to boost if it ever stops being true.
- **Iris is Beta in 5.8** (plugin flag; Epic's docs still say Experimental). Results are stated as "opt-in Iris on 5.8.4".
