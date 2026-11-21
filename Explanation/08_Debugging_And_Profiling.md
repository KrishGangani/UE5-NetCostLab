# 08 · Debugging And Profiling: NetCostLab

**Status:** current to **Phase 5, step S3**: the run's own telemetry (S2) and its correctness files (S3). Phases 7–8 add Networking Insights (`NetTrace.SetTraceVerbosity`), `stat net`, `Net.RepGraph.PrintGraph`, the Iris debugging commands, packet emulation and the arrival logs (S3). Every command here is for UE 5.8.4.

---

## 1. What one benchmark run leaves behind

A run is started by `Tools\run_bench.py` (through the queue) and writes into `Benchmarks\Raw\<batch>\`, which is git-ignored:

| File | Written by | What it holds |
|---|---|---|
| `<run>_server.csv` | The server's director, with `FCsvProfiler::BeginCapture`/`EndCapture` (`05` D2) | Exactly `-NCLCaptureFrames` rows (1,800 = 60 s at 30 Hz), one per server frame of the measured window. The last row is the metadata: `NCLRunTag`, `NCLConfig`, `NCLClients`, `NCLWorkloadHash`, plus the engine's own keys |
| `<run>_server.json` | The director, after the 5 s settle | Window seconds and frames, long frames and saturated connection-frames. Per connection: player slot, bytes, bytes per second, saturated frames |
| `<run>_clientNN.json` | Each client's director, on `Dump` | Its slot, the bytes it received, bytes per second, and its frames and fps over the window it saw |
| `<run>_server_truth.csv` | The director, after the window | Every drone state change of the window: drone, `Seq`, target slot, shared-clock time of the frame that applied it |
| `<run>_server_state.csv`, `<run>_clientNN_state.csv` | Server and each client, after the settle | One row per drone, prop and avatar: kind, index or slot, state CRC, position |
| `<run>_clientNN_arrivals.csv` | Each client | Every drone state arrival in the window and the settle: drone, `Seq`, target, time, `first` (a newly relevant actor) |
| `<run>_server.log`, `<run>_clientNN.log` | Every process (`-abslog=`) | The full engine logs |

The committed summary is `Benchmarks\Raw\<batch>_manifest.json`. It holds the exact command lines, the environment, the mask each process inherited, and the statistics of each run listed below.

## 2. The server CSV columns we use

| Column | Meaning | Source |
|---|---|---|
| `Exclusive/GameThread/ServerReplicateActors` | This frame's replication decision, in every system: legacy, Replication Graph and Iris all run inside it | `NetDriver.cpp:1210–1214, 1373–1378` (`01b` #10) |
| `Exclusive/GameThread/NetworkOutgoing` | The rest of the net driver's flush, which sends the packets | `NetDriver.cpp:1172` |
| `NetFlushMs` (derived) | The two columns above, added per frame by `csv_stats.py`. This is **server ms per net tick**, the headline CPU metric | `01` §6.4 |
| `Exclusive/GameThread/NetworkIncoming` | Packets in (client acks and control messages); context only | The engine's CSV timers |
| `NetCostLab/BusyMsPrevFrame` | The previous frame's busy time: its delta minus the time the server slept (`FApp::GetDeltaTime() − FApp::GetIdleTime()`) | `UnrealEngine.cpp:3156` |
| `NetCostLab/OutBytes` | Bytes sent to all clients in this frame: the sum of the `OutTotalBytes` deltas | `05` D2 |
| `NetCostLab/SatConnections` | Connections whose tick ended saturated this frame (the engine's `IsNetReady() == false` count) | `NetConnection.cpp:5110` |
| `NetCostLab/Clients` | Connections sampled; constant within a run | `05` D2 |
| `FrameTime` | The engine's frame time | The engine |
| `EVENTS` | `NetCostLab/BenchStart` (first captured frame), `NetCostLab/BenchEnd` (last captured frame), and engine events such as `GC` | `06` §19 |

## 3. Reading a capture

```
python -I Tools\csv_stats.py Benchmarks\Raw\<batch>\<run>_server.csv
python -I Tools\csv_stats.py --selftest
```

For each column, the script prints the median, p95, p99, max and mean (linear-interpolation percentiles). It also prints the frame count, whether the file was finalised, and the events it found. `run_bench.py` stores the same numbers for each run in the manifest.

## 3a. Reading a run's correctness

```
python -I Tools\divergence.py Benchmarks\Raw\<batch> <run>
python -I Tools\divergence.py --selftest <scratch folder>
```

Prints the run's changes, the staleness of state changes (threat and ambient: median, p95, p99 in ms), the merged position error (cm) and age (ms) per class, and the totals of final mismatches, relevancy misses, changes not received and first states. `run_bench.py` stores the same block per run in the manifest (`correctness`).

## 4. The director's log lines

These are in `<run>_server.log`, category `LogNetCostLab`:

| Line | Meaning |
|---|---|
| `NCL workload: seed=… drones=… props=… hash=0x…` | The workload that was spawned. Equal hashes mean equal workloads |
| `NCL bench: config=… tag=… waiting for N clients, capture F frames` | The director is active |
| `NCL bench: phase K at T s` | A phase change: 1 warm-up, 2 capture, 3 settle, 4 dump |
| `NCL bench: window closed after F frames, S s, L long frames` | The measured window. A long frame is one longer than two net ticks |
| `NCL bench: done (all clients left)` or `(timeout)` | Why the server ended the run |

`-NCLCountLog=SEC` adds `NCL counts: netmode=… drones=… props=… avatars=…` to every process (`05` C7). The step checks use it; measured runs do not.

## 5. When a run is invalid

`run_bench.py` marks a run invalid, and its record says why, when any of these happens:
- the server did not listen, or did not end by itself within its time budget;
- any process exited with a code other than 0;
- a report is missing or unreadable;
- the CSV does not hold exactly `--capture-frames` frames;
- a client's frame rate was outside 27.3–33 fps: either the 30 fps cap did not hold (`07` S2-8), or the client was starved of CPU.
