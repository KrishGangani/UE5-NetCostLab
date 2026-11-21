"""P05 NetCostLab - correctness of one run: state staleness, final-state mismatches and relevancy, from the run's files.

Inputs, all in the run's folder (written by UNCLBenchSubsystem, 05 Part E):
  <run>_server_truth.csv     every drone state change of the window: drone, seq, target slot, shared-clock time
  <run>_server_state.csv     the final state after the settle: kind (D drone, P prop, A avatar), index, CRC, position
  <run>_clientNN.json        a client's report: its slot, and its position error and age histograms (computed live)
  <run>_clientNN_arrivals.csv  every state arrival on that client: drone, seq, target, time, first (newly relevant)
  <run>_clientNN_state.csv   the client's final state of every actor it holds

Rules (01_Problem_Analysis.md section 6.4):
  * Staleness of a change = the client's arrival time of that (drone, seq) minus the server frame start that applied
    it. Both are FPlatformTime::Seconds() of the same machine (QueryPerformanceCounter), so no clock alignment is
    needed. An arrival that is a newly relevant actor's first state (first = 1) is not a delivery of that change and is
    left out; changes a client never received (the drone was not relevant, or a later change overtook it) are counted
    as "not received".
  * A change is a threat change for a client when its new target slot is that client's slot.
  * Final mismatches: actors the client holds whose state CRC differs from the server's after the settle (SC7: must be 0).
  * Relevancy misses: drones and props within the cull distance minus a margin of the client's avatar (server
    positions) that the client does not hold. Expected 0.
  * Percentiles: the same linear interpolation as csv_stats.py; the position histograms are merged by adding bins.

Usage:
  python divergence.py RUN_DIR RUN_TAG [--json OUT.json]
  python divergence.py --selftest
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csv_stats  # noqa: E402
import netlab_env as env  # noqa: E402

CULL_DISTANCE_CM = 15000.0      # NCLNet::CullDistance
RELEVANCY_MARGIN_CM = 500.0     # actors this close to the edge may legitimately be in or out (motion, timeouts)
EXPECTED_RADIUS_CM = CULL_DISTANCE_CM - RELEVANCY_MARGIN_CM


def read_rows(path: str) -> list[dict]:
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_state(path: str) -> dict[tuple[str, int], tuple[int, tuple[float, float, float]]]:
    """(kind, index) -> (crc, position)."""
    state = {}
    for row in read_rows(path):
        state[(row["kind"], int(row["index"]))] = (int(row["crc"]), (float(row["x"]), float(row["y"]), float(row["z"])))
    return state


def merge_histograms(reports: list[dict], key: str) -> dict:
    """Adds the clients' histograms of one metric and returns nearest-rank percentiles at bin resolution."""
    bins: dict[int, int] = {}
    count = negative = 0
    for report in reports:
        histogram = report.get(key, {})
        for value, n in histogram.get("bins", []):
            bins[int(value)] = bins.get(int(value), 0) + int(n)
        count += int(histogram.get("count", 0))
        negative += int(histogram.get("negative", 0))
    if count == 0:
        return {"count": 0}
    result = {"count": count, "negative": negative}
    ordered = sorted(bins.items())
    for p in (50, 95, 99):
        rank, cumulative = max(1, math.ceil(p / 100 * count)), 0
        for value, n in ordered:
            cumulative += n
            if cumulative >= rank:
                result[f"p{p}"] = value
                break
    return result


def analyse_run(run_dir: str, tag: str) -> dict:
    base = os.path.join(run_dir, tag)
    truth_path, server_state_path = base + "_server_truth.csv", base + "_server_state.csv"
    if not (os.path.exists(truth_path) and os.path.exists(server_state_path)):
        return {"error": "server truth or state missing"}
    truth = {(int(r["drone"]), int(r["seq"])): (int(r["target"]), float(r["clock"])) for r in read_rows(truth_path)}
    server_state = read_state(server_state_path)
    avatars = {index: pos for (kind, index), (_, pos) in server_state.items() if kind == "A"}

    clients, reports = [], []
    staleness = {"threat": [], "ambient": []}
    totals = {"mismatches": 0, "relevancy_misses": 0, "not_received": 0, "first_states": 0, "clients_without_slot": 0}
    for report_path in sorted(glob.glob(base + "_client[0-9][0-9].json")):
        number = re.search(r"_client(\d\d)\.json$", report_path).group(1)
        report = env.read_json(report_path)
        if report is None:
            clients.append({"client": number, "error": "report missing or unreadable"})
            totals["clients_without_slot"] += 1
            continue
        reports.append(report)
        slot = int(report.get("slot", -1))
        client = {"client": number, "slot": slot, "first_states": 0}
        if slot < 0:
            # Without its slot a client has no threat class and no home for the relevancy check: never report 0 then.
            client["error"] = "slot unknown"
            totals["clients_without_slot"] += 1

        arrivals_path, state_path = f"{base}_client{number}_arrivals.csv", f"{base}_client{number}_state.csv"
        received = set()
        if os.path.exists(arrivals_path):
            for row in read_rows(arrivals_path):
                if row["first"] == "1":
                    client["first_states"] += 1
                    continue
                key = (int(row["drone"]), int(row["seq"]))
                if key in truth and key not in received:
                    received.add(key)
                    target, server_clock = truth[key]
                    staleness_ms = (float(row["clock"]) - server_clock) * 1000.0
                    staleness["threat" if target == slot and slot >= 0 else "ambient"].append(staleness_ms)
        client["received"] = len(received)
        client["not_received"] = len(truth) - len(received)

        if os.path.exists(state_path):
            client_state = read_state(state_path)
            client["mismatches"] = sum(1 for key, (crc, _) in client_state.items()
                                       if key[0] in ("D", "P") and key in server_state and server_state[key][0] != crc)
            home = avatars.get(slot)
            client["relevancy_misses"] = None if home is None else sum(
                1 for key, (_, pos) in server_state.items()
                if key[0] in ("D", "P") and key not in client_state and math.dist(pos[:2], home[:2]) < EXPECTED_RADIUS_CM)
        for name in ("mismatches", "relevancy_misses", "not_received", "first_states"):
            totals[name] += client.get(name) or 0
        clients.append(client)

    return {
        "changes": len(truth),
        "clients": clients,
        "totals": totals,
        "staleness_ms": {name: csv_stats.summarize(values) if values else {"n": 0} for name, values in staleness.items()},
        "position_error_cm": {name: merge_histograms(reports, f"{name}_error_cm") for name in ("threat", "ambient")},
        "position_age_ms": {name: merge_histograms(reports, f"{name}_age_ms") for name in ("threat", "ambient")},
    }


def selftest(scratch: str) -> None:
    """A two-client run written by hand: every number below is worked out on paper."""
    os.makedirs(scratch, exist_ok=True)
    tag = os.path.join(scratch, "t")

    def write(name: str, text: str) -> None:
        with open(tag + name, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)

    write("_server_truth.csv", "drone,seq,target,clock\n0,5,1,100.000\n1,7,-1,100.500\n2,3,0,101.000\n")
    write("_server_state.csv", "kind,index,crc,x,y,z\nD,0,11,0,0,0\nD,1,22,1000,0,0\nD,2,33,20000,0,0\n"
                               "P,0,44,0,14000,0\nA,0,0,0,0,0\nA,1,0,0,0,0\n")
    # Client 00 (slot 1): drone 0's change (a threat for slot 1) after 40 ms, drone 1's after 60 ms, a first state.
    write("_client00.json", json.dumps({"slot": 1, "ambient_error_cm": {"count": 3, "negative": 0, "bins": [[10, 2], [30, 1]]}}))
    write("_client00_arrivals.csv", "drone,seq,target,clock,first\n0,5,1,100.040,0\n1,7,-1,100.560,0\n2,3,0,101.500,1\n")
    write("_client00_state.csv", "kind,index,crc,x,y,z\nD,0,11,0,0,0\nD,1,99,1000,0,0\nP,0,44,0,14000,0\n")
    # Client 01 (slot 0): drone 2's change (a threat for slot 0) after 20 ms; it lacks the prop 140 m away.
    write("_client01.json", json.dumps({"slot": 0, "ambient_error_cm": {"count": 1, "negative": 0, "bins": [[50, 1]]}}))
    write("_client01_arrivals.csv", "drone,seq,target,clock,first\n2,3,0,101.020,0\n")
    write("_client01_state.csv", "kind,index,crc,x,y,z\nD,0,11,0,0,0\nD,1,22,1000,0,0\nD,2,33,20000,0,0\n")

    result = analyse_run(scratch, "t")
    assert result["changes"] == 3
    threat = result["staleness_ms"]["threat"]
    assert threat["n"] == 2 and [round(threat["min"]), round(threat["max"])] == [20, 40], threat
    assert result["staleness_ms"]["ambient"]["n"] == 1 and round(result["staleness_ms"]["ambient"]["median"]) == 60
    assert result["totals"]["first_states"] == 1 and result["totals"]["not_received"] == (3 - 2) + (3 - 1)
    assert result["clients"][0]["mismatches"] == 1 and result["clients"][1]["mismatches"] == 0
    # Drone 2 is 200 m away from both avatars (not expected); the prop at 140 m is within 145 m of client 01's avatar.
    assert result["clients"][0]["relevancy_misses"] == 0 and result["clients"][1]["relevancy_misses"] == 1
    assert result["totals"]["clients_without_slot"] == 0
    merged = result["position_error_cm"]["ambient"]
    assert merged["count"] == 4 and merged["p50"] == 10 and merged["p95"] == 50, merged
    print("selftest: OK (staleness join, threat split, first states, mismatches, relevancy and histogram merge)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_dir", nargs="?")
    parser.add_argument("tag", nargs="?")
    parser.add_argument("--json")
    parser.add_argument("--selftest", metavar="SCRATCH_DIR", help="run the self-test, writing its files into SCRATCH_DIR")
    args = parser.parse_args(argv)
    if args.selftest:
        selftest(args.selftest)
        return 0
    if not (args.run_dir and args.tag):
        parser.error("give RUN_DIR and RUN_TAG, or --selftest SCRATCH_DIR")
    result = analyse_run(args.run_dir, args.tag)
    print(json.dumps({k: v for k, v in result.items() if k != "clients"}, indent=2))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(result, handle, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
