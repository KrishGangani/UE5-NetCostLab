# 10 · Interview Questions: NetCostLab

**Status:** final for this version (2026-11-21). Each answer points at the code or the measurement behind it.

**1. Why can't you just override `GetNetPriority` under Iris?**
Iris never calls it; priority comes from `UNetObjectPrioritizer`s chosen per object (`06` §28). The legacy hook is kept as C6 for comparison (`05` F1).

**2. How does your prioritiser know which object it is prioritising?**
Iris passes only internal indices. `UReplicationSystem::SetPrioritizer` calls our `AddObject` synchronously, so `Assign()` captures the index it receives during that call (`ReplicationPrioritization.cpp:414, 452`). Public API only; a failed capture is counted and never boosted.

**3. Why an additive boost and not a multiplier?**
A far object's sphere priority is 0.1. Doubling it stays below 1.0, the level at which Iris considers an object. +2.0 lifts any focused object above it (`02` §2).

**4. How do you make a comparison between six replication setups fair?**
One server binary, one seeded workload with a hash (`0x680F4C7A` in every run), the same server-determined views, the same cull distance, pinned cores (server P-cores, clients E-cores), and interleaved runs (`01` §7).

**5. How do you measure what a player sees without logging every packet?**
The client rebuilds the seeded plan and maps the shared QueryPerformanceCounter clock to the server's world time with one anchor, so it knows every drone's true position at any instant. It computes error and age every frame (`05` E1). With 100 ms of lag added on the server, both rose by 117 ms: 100 ms, plus the wait for the next connection tick (`09` S3-c).

**6. Something you measured that surprised you?**
A logged cvar that was not in force: `-dpcvars=t.MaxFPS=30` is overwritten during engine start-up, the clients ran at 2,000 fps, and the server's net flush cost 60% more (`09` S2). Also, under Iris a client's view target can stay unset, because the view-target RPC can arrive before the avatar exists on the client (`07` S3-7).

**7. Why is the server the editor binary?**
The launcher engine cannot build a Server target, and its packaged games have no push model (`01b` #1–#3). So only comparisons between configurations are claimed, never absolute server costs.

**8. What did you not prove?**
The prioritiser's benefit under a bandwidth cap (SC4–SC5) and its cost at N = 32 (SC6): the campaign was cut (`09` SC table). At 1 MB/s with 8 clients nothing is starved, so the boost has nothing to rescue there.
