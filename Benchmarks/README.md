# P05 · Benchmarks

Every number in this project's write-up must trace back to a file in this folder. The method is in [../Explanation/01_Problem_Analysis.md §7](../Explanation/01_Problem_Analysis.md). The results, batch by batch, are in [../Explanation/09_Testing_And_Benchmarks.md](../Explanation/09_Testing_And_Benchmarks.md).

## Measurement rules

1. **Baselines first.** C1–C4 are captured (S5) before the gameplay prioritiser exists, on the same workload, binaries and machine as every later run.
2. **One server binary for all five configurations:** the Development **editor binary** in dedicated-server mode (`UnrealEditor-Cmd.exe … -server`), uncooked. The launcher engine cannot build a Server target, a packaged game can never be a dedicated server, and packaged games on the launcher engine have no push model ([01b](../Explanation/01b_Research_Findings.md) #1–#3). Clients are the **packaged Development game**, headless (`-nullrhi`): Phase 4 check F4 showed they join the editor-binary server in both systems at ≈ 0.35–0.54 GB each, against ≈ 1.7 GB for an editor-binary client (`09`).
3. **Real time, not a fixed step.** The server runs at its 30 Hz cap, clients at 30 fps. A fixed-step server would simulate faster than real time while bandwidth and timeouts follow the wall clock (`01b` #4).
4. **The server opens and closes the capture window itself** (`CSV_EVENT(BenchStart)` after all clients joined and a 20 s warm-up; 1,800 captured frames = 60 s).
5. **Core pinning, recorded per run:** the server on the performance cores (mask `0xFFFF`), the clients on the efficiency cores (`0xFF0000`). The run script pins itself to the right mask right before starting each process (the child inherits it from its first instruction), starts the real executable directly with `Popen` (so its own exit code is recorded), and records the mask each process actually inherited.
6. **One discarded priming run per batch**, then **runs per combination = the number of configurations in the batch**, interleaved, with the configuration order rotated every round (Scenario A: 5 configs → 5 runs; Scenario B: 3 configs → 3 runs).
7. **Statistics:** median, p95 and p99 per run (linear-interpolation percentiles), then the median across runs with the [min–max] across runs. Slopes are least-squares fits over N = 8/16/32 and are never extrapolated.
8. **The environment is recorded with every batch** (below), automatically, by the run script.

## Folders

| Folder | What goes in it | Rule |
|---|---|---|
| `Raw\` | Per batch: server CSV captures, server and client logs, arrival and truth logs, one `<batch>_manifest.json` | **Never edited.** Only the manifests are in git (exact command lines, environment, masks, per-run statistics). Logs hold machine-specific paths and stay local; every batch cited in the docs is attached as `<batch>_raw.zip` to the repository's **Releases** page |
| `Summaries\` | Computed statistics, SC1–SC7 pass/fail tables, charts | Each summary names the batch it was computed from |

## File naming

`<batch>_<Config>_<Scenario>_N<clients>[_L<loss%>]_r<run>.<ext>`, with `<batch>` = `YYYY-MM-DD_HHMMSS[_label]`

- `<Config>`: `C1` legacy · `C2` legacy + push · `C3` Replication Graph · `C4` Iris · `C5` Iris + gameplay prioritiser.
- `<Scenario>`: `A` (open: 1,000,000 B/s per client, no loss) or `B` (capped, 0/1/3/5% loss).
- Example: `2026-11-30_101500_C4_B_N16_L3_r2_server.csv`, `…_client07_arrivals.bin`, `…_server_truth.bin`.
- `_r0_prime` files are the discarded priming run, kept for transparency and excluded from every summary.
- Probes and checks use the label as the batch suffix: `<YYYY-MM-DD_HHMMSS>_probe_manifest.json` (Phase 2, P2-1), `<YYYY-MM-DD_HHMMSS>_f4_manifest.json` (Phase 4, F4).

## Environment record (written into every manifest and summary)

```text
Engine:            UE 5.8.4 (CL 58902052), launcher build
Server:            UnrealEditor-Cmd.exe -server (Development editor build), uncooked
Clients:           packaged Development game, headless (-nullrhi): Build\Windows\NetCostLab\Binaries\Win64\NetCostLab.exe
Compiler:          MSVC 14.44.35229 (VS 2022 17.14), Windows SDK 10.0.22621.0
CPU / RAM:         i7-14650HX (8P + 8E, 24 logical) / 32 GB
Affinity:          server 0xFFFF (P-cores), clients 0xFF0000 (E-cores)
Storage:           project on D: (Kingston NVMe), engine on C: (WD NVMe)
OS:                Windows 11 build 26200
Power:             AC, power plan recorded per batch
Network:           loopback UDP, port 7777; emulated loss per run (-PktLoss= on server and clients)
Tick:              server 30 Hz (NetServerMaxTickRate), clients 30 fps
Command lines:     exact, per process, in the manifest
Warm-up excluded:  20 s     Capture: 60 s (1,800 server frames)     Settle before the state dump: 5 s
```

## Run log

| Batch | Configs | N | Runs | Purpose | Summary |
|---|---|---|---|---|---|
| `2026-11-21_022445_foundation` | (all Phase 4 steps) | — | 1 | Build editor + game, package, packaged smoke, plus the two networked checks below, in one queue job. Not a measurement | results in `09`, Phase 4 |
| `2026-11-21_022542_probe` (Phase 2, P2-1) | legacy + push, Replication Graph, Iris | 2 editor-binary clients | 1 each | What the launcher engine can run: Server target build attempt, editor-binary dedicated server, headless clients, memory per process. Not a measurement | results in `09`, P2-1 |
| `2026-11-21_023102_f4` (Phase 4, F4) | legacy + push, Iris | 2 packaged clients | 1 each | Packaged headless clients against the editor-binary server: decides the client binary. Not a measurement | results in `09`, F-4 |
