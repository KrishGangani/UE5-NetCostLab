# P05 · NetCostLab: replication cost and a gameplay-aware prioritiser

**Domain:** Multiplayer & network optimisation · **Difficulty:** ★★★★☆ · **Engine:** UE 5.8.4

> **Status: planned; Phases 1–3 (problem analysis, research, architecture) are starting.** The ten projects are developed in parallel, and every Unreal Engine build and benchmark runs one at a time on the same machine, so measurements stay clean. Everything below is the plan. The success criteria are **targets fixed before any code exists, not results**. Real numbers will only come from controlled before/after runs, each traceable to a raw benchmark batch.

## The problem

Server CPU and bandwidth scale with connections × actors. The only public Iris benchmark measures frame time, not bandwidth, and compares no configurations or prioritisers

## Proposed solution

A controlled study on one gameplay workload: legacy, legacy + push model, Replication Graph, Iris, and Iris + our gameplay-aware `UNetObjectPrioritizer`. Correctness is checked under emulated packet loss

## Main Unreal Engine technologies

Replication, push model (`MARK_PROPERTY_DIRTY`), Replication Graph, **Iris (opt-in on 5.8)**, filters/prioritisers, Networking Insights, net emulation, dedicated server, headless clients

## Success criteria (targets, defined before implementation)

Bytes/s per client and server ms per net tick at 8/16/32 clients (one PC; scaling slope reported honestly, no 100-player claims); state divergence under 1–5% loss; all 5 configurations on the same workload

## Existing work it must be compared against

Push model, Replication Graph (not deprecated), Iris filters/prioritisers, BorMor's 100-player Iris benchmark

**The gap it targets:** Multi-configuration comparison with bandwidth, plus a gameplay-aware prioritiser (gap strength: Medium).

**Main risk:** One PC limits client count. Report slopes, not extrapolated claims

## Why studios care

Riot "bandwidth optimization, server density"; Epic "state synchronization"; Fortnite BR server work; Apex netcode

## Method

Every project in the portfolio follows the same ten phases: problem analysis → research → architecture → foundation → implementation → explanation → testing → profiling → review → portfolio. Each phase is documented in `Explanation/` (files 00–12, including a line-by-line code walkthrough), and every claimed improvement must be measured against the existing solutions above on the same workload. Engine: Unreal Engine 5.8.4, C++.

## Folder layout

```text
P05_NetCostLab\
├── README.md        ← this page (becomes the project's portfolio page)
├── Explanation\     ← 00–12, created when the project starts
├── Benchmarks\      ← Raw\ + Summaries\
├── Media\           ← diagrams, screenshots, demo video
├── Tools\           ← analysis and run scripts
└── NetCostLab\    ← the Unreal Engine project (created in its Phase 4)
```
