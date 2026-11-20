# 00 · Project Overview: NetCostLab

> *"This was the problem. This was my investigation. This was my solution. These were my technical decisions. This was how I measured the results."*

**Status (2026-11-21):** Phases 1–4 are done: problem in `01`, verified research in `01b`, final architecture v1.0 in `02`, build steps in `03`, and the foundation (UE project, plugin, scripts; editor and game builds with 0 warnings; packaged smoke run; packaged headless clients joining an editor-binary dedicated server) in `09`. There is no gameplay code yet, and **no results exist yet**.

---

## A. Identity

| | |
|---|---|
| **Name** | NetCostLab: replication cost and a gameplay-aware prioritiser |
| **Technical category** | Multiplayer and network optimisation: server CPU per net tick, bandwidth per client, correctness under packet loss |
| **Main objective** | Measure, on one fixed gameplay workload and one machine, what each of Unreal's replication setups costs: legacy, legacy + push model, Replication Graph, Iris, and Iris with our own **gameplay-aware prioritiser**. Then show whether the prioritiser keeps the objects that matter to each player fresher when bandwidth runs out |
| **Difficulty** | ★★★★☆ Advanced |
| **Core UE technologies** | `UNetDriver` / `UNetConnection`, property replication, push model (`MARK_PROPERTY_DIRTY_FROM_NAME`, `Net.IsPushModelEnabled`), Replication Graph (`UBasicReplicationGraph`), Iris (`UReplicationSystem`, `UNetObjectPrioritizer`, `USphereNetObjectPrioritizer`), packet emulation (`-PktLoss=`), the CSV profiler, Networking Insights, a dedicated server from the editor binary (`-server`) and headless clients (`-nullrhi`) |
| **Relation to existing work** | The comparison itself is not new: a public UE 5.8.3 lab already compares legacy, Replication Graph and Iris with bandwidth at 8 clients, and the legacy `AActor::GetNetPriority` hook has long allowed gameplay-weighted priority. This project adds four things we did not find published together: push model as an isolated factor, scaling slopes over 8/16/32 headless clients, a gameplay-aware **Iris** prioritiser, and staleness/position-error measurements under 1–5% emulated loss on a shared clock. See `01_Problem_Analysis.md` §3 and `01b` §5 |

## B–D. Problem, solution, architecture

- Problem, root-cause hypotheses, success criteria and the measurement plan: **[01_Problem_Analysis.md](01_Problem_Analysis.md)**
- What Phase 2 found in the engine source, and how it changed the plan: **[01b_Research_Findings.md](01b_Research_Findings.md)**
- Solution and architecture (final v1.0): **[02_System_Architecture.md](02_System_Architecture.md)**

## E. Technical demonstration

**What the viewer sees**
- One dedicated server and N headless clients on one PC, started by one script. The server log and a live chart show **server ms per net tick** and **bytes per second per client**.
- A top-down capture of the arena from one client: drones move along fixed loops; drones that are attacking *this* player are outlined red.
- A text overlay names the active configuration: `C1` legacy, `C2` legacy + push model, `C3` Replication Graph, `C4` Iris, `C5` Iris + gameplay prioritiser.

**Before** (C1–C4, bandwidth-capped scenario)
- When the per-client bandwidth cap is reached, Iris's default sphere prioritiser spends it by distance. A drone 80 m away that is shooting at you updates as rarely as any other far drone. On screen its outline jumps forward in steps; the measured position error grows.

**After** (C5)
- The same drones, the same cap. Drones attacking you get a priority boost **for your connection only**, so they update almost every tick on your screen while other players still see them at the normal rate. The price is visible too: ambient far drones get a little staler. Both numbers are reported.

**Recorded material (Phase 10):**
- charts of server ms per net tick and bytes/s per client against client count (8/16/32) for C1–C6, with the fitted slopes;
- position-error and staleness distributions (threat vs ambient drones) for C4 and C5 at 0/1/3/5% loss;
- a short side-by-side capture of the same client view under C4 and C5;
- a Networking Insights walkthrough of one packet.

## G. Portfolio presentation (produced in Phase 10)

1. The problem in one paragraph (`01` §1).
2. The investigation: hypotheses H1–H6 and what the baseline captures showed for each.
3. The launcher-engine findings that shaped the setup (no Server target, no push model in packaged games), with evidence.
4. The architecture diagram (`02` §5) and the per-tick flow of the prioritiser.
5. Results for all six configurations (C6 is the legacy hook with the same focus rule, the fair existing solution for C5), with the full environment and method, and the slopes stated as slopes (no 100-player claims).
6. What did not improve, stated plainly.
7. Video, source repository and the complete `Explanation/` folder.

## H. Industry relevance

**Skills demonstrated:**
- how Unreal replication actually works on the server: polling vs push dirtiness, per-connection prioritisation, bandwidth accounting in `UNetConnection`;
- reading engine source to find what a launcher build can and cannot do, and designing around it;
- writing a plug-in Iris prioritiser against a Beta API without touching engine code;
- multi-process benchmarking on one machine (core pinning, shared clock, interleaved runs);
- measuring correctness, not just speed (staleness and position error under loss);
- honest reporting of scaling slopes on limited hardware.

**Studios and roles that ask for this:**
- Riot (Engine Performance): "bandwidth optimization, server density";
- Epic (Rocket League lead gameplay): "network replication systems (client-server prediction, state synchronization, lag compensation)";
- Embark (ARC Raiders): "performance, tooling, architecture and networking";
- Respawn (Apex): server performance and netcode work described in its public deep dive.

Sources: [research/01](Research/01_job_market.md) (R1, E1, EM1, EA2, §7) and [research/02](Research/02_technical_challenges.md) ([T3-A]–[T3-H]).

**Interview questions it prepares you for:**
- "What does the server do every net tick for every connection? Where does the CPU go?"
- "What is push-model replication, what does it save, and when does it save nothing?"
- "Replication Graph vs Iris: what problem does each solve? Can you use both?"
- "Your server is bandwidth-saturated. How do you decide what gets sent?"
- "How would you make network priority depend on gameplay, not just distance?"
- "How do you measure replication correctness under packet loss?"
- "You only have one PC. How do you benchmark 32 clients honestly?"
- "Why can't you build a dedicated server with the Epic Games Launcher engine, and what did you do instead?"
