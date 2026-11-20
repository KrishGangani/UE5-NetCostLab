# P05 · NetCostLab: replication cost and a gameplay-aware prioritiser

**Author:** Krish Gangani · [github.com/KrishGangani](https://github.com/KrishGangani)

**Domain:** Multiplayer & network optimisation · **Difficulty:** ★★★★☆ · **Engine:** UE 5.8.4

> **Status: in progress. There are no results yet.** Sections marked *(pending)* are filled in only from real measurements, in the phase named. The success criteria are **targets fixed before any code exists, not results**.

| Phase | 1 Problem | 2 Research | 3 Architecture | 4 Foundation | 5 Implementation | 6 Explanation | 7 Testing | 8 Profiling | 9 Review | 10 Portfolio |
|---|---|---|---|---|---|---|---|---|---|---|
| State | ✓ | ✓ | ✓ final | ✓ | next (S1 of 8) | ongoing | — | — | — | — |

---

## Problem statement

A multiplayer server sends every replicated actor to every connection, so its CPU time and bandwidth grow with **connections × actors**, not with what changed or what matters to each player. Unreal ships four answers (push model, Replication Graph, Iris and their prioritisers), but there are few like-for-like measurements, and when bandwidth runs out Iris's shipped prioritisers decide by **distance only**. Details: [Explanation/01_Problem_Analysis.md](Explanation/01_Problem_Analysis.md).

## Engineering approach

One server-driven gameplay workload (256 drones, 768 props, 8/16/32 headless clients on one PC), run under five configurations from **one** server binary:

| ID | Configuration |
|---|---|
| C1 | Generic (legacy) replication |
| C2 | C1 + push model (`MARK_PROPERTY_DIRTY_FROM_NAME`) |
| C3 | C2 + Replication Graph (`UBasicReplicationGraph`) |
| C4 | Iris with its default sphere prioritiser |
| C5 | Iris + our **gameplay-aware prioritiser**: Epic's sphere model plus a boost, for one connection only, for drones that are attacking that connection's pawn |

Measured: server ms per net tick, bytes/s per client, scaling slopes over 8/16/32 clients, and correctness (position error, state staleness, final mismatches) under a bandwidth cap and 0/1/3/5% emulated loss, on a clock shared by all processes. Details: [Explanation/02_System_Architecture.md](Explanation/02_System_Architecture.md).

**What Phase 2 changed (honestly):**
- **The launcher engine cannot build a dedicated-server target, and its packaged games have no push model.** The server is therefore the editor binary in `-server` mode for all five configurations; only comparisons, not absolute server numbers, are claimed ([01b](Explanation/01b_Research_Findings.md) #1–#3).
- **The comparison itself is not new:** a public UE 5.8.3 lab already compares legacy, Replication Graph and Iris with bandwidth at 8 clients, and gameplay-weighted priority has long been possible in legacy through `AActor::GetNetPriority`. The new part is a gameplay-aware **Iris** prioritiser, push model as an isolated factor, scaling slopes, and correctness under loss ([01b](Explanation/01b_Research_Findings.md) §5).

## Architecture diagram

Final (v1.0): [Explanation/02_System_Architecture.md](Explanation/02_System_Architecture.md) §5–6 (mermaid).

## Before / after comparison

*(pending, Phase 8)*. C1–C5 on the same workload; C4 vs C5 under a bandwidth cap.

## Benchmark results

*(pending, Phase 8)*. Median, p95 and p99 server ms per net tick and bytes/s per client at N = 8/16/32, with slopes; position error and staleness for threat vs ambient drones at 0/1/3/5% loss. Raw data in [Benchmarks/](Benchmarks/README.md). The pass/fail criteria were fixed in advance: [01_Problem_Analysis.md §6.5](Explanation/01_Problem_Analysis.md).

## Recorded demonstration

*(pending, Phase 10)*. The same client view under C4 and C5 with threat drones outlined, plus the scaling charts. Saved in `Media/`.

## Source code

The UE project is in [NetCostLab/](NetCostLab/) (UE 5.8.4, C++):
- the reusable system is the plugin `NetCostLabCore` (the gameplay-aware Iris prioritiser, from step S6);
- the workload and the benchmark harness are in the game module `NetCostLab` (from step S1);
- run and analysis scripts are in [Tools/](Tools/).

## Documentation

| File | Contents |
|---|---|
| [00_Project_Overview](Explanation/00_Project_Overview.md) | Identity, demonstration, presentation, industry relevance |
| [01_Problem_Analysis](Explanation/01_Problem_Analysis.md) | Problem, existing work, root-cause hypotheses, workload, configurations, metrics, success criteria, measurement plan |
| [01b_Research_Findings](Explanation/01b_Research_Findings.md) | Phase 2: verified engine behaviour (UE 5.8.4), the launcher-engine limits, existing work, and what they changed |
| [02_System_Architecture](Explanation/02_System_Architecture.md) | Final v1.0: modules, classes, the prioritiser, data flow, per-tick flow, defaults, safety rules, rejected alternatives |
| [03_Implementation_Roadmap](Explanation/03_Implementation_Roadmap.md) | The small, ordered build steps; baselines captured before the prioritiser exists |
| [04](Explanation/04_CPP_Fundamentals.md) · [05](Explanation/05_Line_By_Line_Code_Explanation.md) · [06](Explanation/06_Unreal_APIs_And_Macros.md) · [07](Explanation/07_Common_Mistakes.md) | C++ fundamentals, line-by-line code, APIs and macros, common mistakes. Written step by step with the code (current to Phase 4) |
| [09](Explanation/09_Testing_And_Benchmarks.md) | Every check so far, with its command and result |
| [08](Explanation/08_Debugging_And_Profiling.md) · [10](Explanation/10_Interview_Questions.md) · [11](Explanation/11_Optimization_Decisions.md) · [12](Explanation/12_Future_Scalability.md) | Debugging and profiling, interview questions, optimisation decisions, future scalability. Filled in during Phases 5–10 |
| [Phase2_Raw_Notes/](Explanation/Phase2_Raw_Notes/README.md) | The audit trail behind `01b` |
| [Research/](Explanation/Research/) | The portfolio-wide research this project draws on: job market, technical challenges, existing solutions, local engine verification |

## Why studios care

Riot: "bandwidth optimization, server density". Epic: "network replication systems (client-server prediction, state synchronization, lag compensation)". Fortnite BR's 100-player server work and Respawn's Apex netcode write-up ([research/02](Explanation/Research/02_technical_challenges.md) [T3-A], [T3-G]).
