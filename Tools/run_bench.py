"""P05 NetCostLab - run a benchmark batch: an editor-binary dedicated server plus N headless packaged clients per run.

Run it through the machine queue only; every run's server and clients start and stop inside that one queue job:
  python -I -u "D:\\UNREAL PORTFOLIO\\tools\\ue_queue.py" run --project P05 --label "..." --timeout 3600 --idle 600 --
      'python -I -u "D:\\UNREAL PORTFOLIO\\P05_NetCostLab\\Tools\\run_bench.py" --configs C1,C4 --clients 8 --runs 2'

What it does (01_Problem_Analysis.md section 7, Benchmarks/README.md):
  1. Records the environment, then for every (clients x config) combination runs --runs rounds, INTERLEAVED: the config
     order rotates every round, so every config sits in every order position equally often. One discarded priming run
     (suffix _r0_prime) comes first unless --no-prime.
  2. A run: start the server pinned to the P-cores, wait until it listens, start N clients pinned to the E-cores
     (this script pins itself before each launch, so every child inherits its mask from its first instruction), wait
     until the server exits by itself (its director quits after the Dump phase), give the clients time to write their
     reports and quit, and kill anything left in a finally block.
  3. Collects the server CSV, the server and client reports and the logs into Benchmarks/Raw/<batch>/ (git-ignored),
     computes per-run statistics, and writes Benchmarks/Raw/<batch>_manifest.json (committed, sanitised).

A run is valid when the server exited by itself with 0, every client wrote its report, the CSV holds exactly
--capture-frames frames, and every client ran at its 30 fps cap (within 10%). The manifest is rewritten after
every run. Runs with long frames (> 2 server ticks) stay valid but carry their count.
"""
from __future__ import annotations

import argparse
import datetime as dt
import glob
import json
import os
import re
import statistics
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csv_stats  # noqa: E402
import netlab_env as env  # noqa: E402

MAP, PACKAGED_CLIENT, COMMON, IPNETDRIVER = env.MAP, env.PACKAGED_CLIENT, env.COMMON_ARGS, env.IPNETDRIVER
PUSH = ["Net.IsPushModelEnabled=1", "net.PushModelSkipUndirtiedReplication=1"]

# Per configuration: Iris on/off (both sides), server-only console variables, server-only config overrides,
# server-only extra arguments (01_Problem_Analysis.md section 6.2, 02_System_Architecture.md section 9).
CONFIGS = {
    "C1": {"iris": 0, "cvars": ["Net.IsPushModelEnabled=0"], "ini": [], "args": []},
    "C2": {"iris": 0, "cvars": PUSH, "ini": [], "args": []},
    "C3": {"iris": 0, "cvars": PUSH, "ini": [env.REPGRAPH_INI], "args": []},
    "C4": {"iris": 1, "cvars": ["Net.IsPushModelEnabled=1"], "ini": [], "args": []},
    "C5": {"iris": 1, "cvars": ["Net.IsPushModelEnabled=1"], "ini": [], "args": ["-NCLPrioritizer=Gameplay"]},
    "C6": {"iris": 0, "cvars": PUSH + ["ncl.LegacyFocusPriority=1"], "ini": [], "args": []},
}
SCENARIO_RATE = {"A": 1_000_000, "B": 24_000}   # bytes per second per client (01 section 6.3)
SERVER_TICK_HZ = 30
CLIENT_FPS = 30
CLIENT_FPS_TOLERANCE = 1.1     # valid: every client between 27.3 and 33 fps (the cap held, and no client starved)
SERVER_LISTEN_TIMEOUT = 240.0
JOIN_MARGIN_SECONDS = 180.0    # start-up and joining of N clients, on top of warm-up + capture + settle
CLIENT_EXIT_GRACE_SECONDS = 30.0


def powershell(command: str) -> str:
    result = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True)
    return result.stdout.strip()


def environment_record(server_mask: int, client_mask: int) -> dict:
    return {
        "date": dt.datetime.now().isoformat(timespec="seconds"),
        "engine": env.ENGINE,
        "server_binary": "UnrealEditor-Cmd.exe -server (Development editor build), uncooked",
        "client_binary": "packaged Development game, -nullrhi",
        "toolchain_of_last_build": last_build_toolchain(),
        "cpu": powershell("(Get-CimInstance Win32_Processor).Name"),
        "ram_gb": powershell("[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)"),
        "os": powershell("$o=Get-CimInstance Win32_OperatingSystem; $o.Caption + ' build ' + $o.BuildNumber"),
        "power_plan": powershell("powercfg /getactivescheme"),
        # PowerOnline is the charger state itself; Win32_Battery.BatteryStatus says "fully charged" (3) on AC as well.
        "on_ac_power": powershell("(Get-CimInstance -Namespace root/wmi -ClassName BatteryStatus | "
                                  "Select-Object -First 1).PowerOnline") or "no battery reported",
        "affinity": {"server_mask": f"0x{server_mask:X}", "client_mask": f"0x{client_mask:X}"},
        "logical_cpus": os.cpu_count(),
        "map": MAP,
        "client_fps_cap": CLIENT_FPS,
    }


def last_build_toolchain() -> str:
    """The compiler and SDK of the most recent UnrealBuildTool run (its log is rewritten by every build; the batch's
    own package step is the last build before the runs)."""
    log = os.path.join(os.environ.get("LOCALAPPDATA", ""), "UnrealBuildTool", "Log.txt")
    try:
        with open(log, encoding="utf-8", errors="replace") as handle:
            match = re.search(r"Using Visual Studio (\S+) toolchain .*? and Windows (\S+) SDK", handle.read())
    except OSError:
        return "unknown (no UnrealBuildTool log)"
    return f"MSVC {match.group(1)}, Windows SDK {match.group(2)}" if match else "unknown"


def read_json(path: str) -> dict | None:
    """A report, or None if it is missing or unreadable (a process killed while writing it)."""
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def network_args(args) -> list[str]:
    """Switches every process gets: the per-client rate in the three places it is set (the client's request, the
    server's maximum and its internet maximum; 01b #9: World.cpp:7471, 7987-7989), and emulated loss when asked
    (-PktLoss, parsed at net driver start: NetDriver.cpp:788, NetEmulationHelper.cpp:547)."""
    rate = args.rate
    ini = [f"{IPNETDRIVER}:MaxClientRate={rate}", f"{IPNETDRIVER}:MaxInternetClientRate={rate}",
           f"[/Script/Engine.Player]:ConfiguredInternetSpeed={rate}"]
    return ["-ini:Engine:" + ",".join(ini)] + ([f"-PktLoss={args.loss}"] if args.loss else [])


def server_log_path(run_dir: str, tag: str) -> str:
    return os.path.join(run_dir, tag + "_server.log")


def server_command(config: str, clients: int, tag: str, run_dir: str, args) -> list[str]:
    spec = CONFIGS[config]
    command = [env.PROJECT, MAP, "-server", f"-abslog={server_log_path(run_dir, tag)}",
               f"-UseIrisReplication={spec['iris']}", "-dpcvars=" + ",".join(spec["cvars"])] + network_args(args)
    if spec["ini"]:
        command.append("-ini:Engine:" + ",".join(spec["ini"]))
    return command + [f"-NCLClients={clients}", f"-NCLWarmup={args.warmup}", f"-NCLCaptureFrames={args.capture_frames}",
                      f"-NCLSettle={args.settle}", f"-NCLSeed={args.seed}", f"-NCLRunTag={tag}", f"-NCLConfig={config}",
                      f"-NCLOutDir={run_dir}", "-NCLExit"] + spec["args"] + COMMON + args.extra.split()


def client_command(config: str, index: int, tag: str, run_dir: str, args) -> list[str]:
    return ["127.0.0.1", "-nullrhi", f"-abslog={os.path.join(run_dir, f'{tag}_client{index:02d}.log')}",
            f"-UseIrisReplication={CONFIGS[config]['iris']}"] + network_args(args) \
        + [f"-NCLRunTag={tag}", f"-NCLOutDir={run_dir}", f"-NCLClientIndex={index}", f"-NCLClientFPS={CLIENT_FPS}",
           "-NCLExit"] + COMMON \
        + args.client_extra.split()


def run_once(config: str, clients: int, tag: str, run_dir: str, server_mask: int, client_mask: int, args) -> dict:
    record = {"tag": tag, "config": config, "clients": clients, "loss": args.loss, "rate": args.rate,
              "server_command": server_command(config, clients, tag, run_dir, args),
              "client_command_0": client_command(config, 0, tag, run_dir, args)}
    if args.dry_run:
        print("DRY:", " ".join(record["server_command"]))
        print("DRY:", " ".join(record["client_command_0"]))
        return record
    server_log = server_log_path(run_dir, tag)
    processes, masks = [], []
    started = time.time()
    try:
        server, mask = env.launch_pinned(server_mask, env.EDITOR_CMD, record["server_command"])
        processes.append(server)
        masks.append(mask)
        if not env.wait_for_text(server_log, "IpNetDriver listening on port", SERVER_LISTEN_TIMEOUT):
            record["failure"] = "server did not listen"
            return record
        for index in range(clients):
            client, mask = env.launch_pinned(client_mask, PACKAGED_CLIENT, client_command(config, index, tag, run_dir, args))
            processes.append(client)
            masks.append(mask)
        budget = args.warmup + args.capture_frames / SERVER_TICK_HZ + args.settle + JOIN_MARGIN_SECONDS
        try:
            record["server_exit"] = server.wait(timeout=budget)
        except subprocess.TimeoutExpired:
            record["failure"] = f"server did not finish within {budget:.0f} s"
        deadline = time.time() + CLIENT_EXIT_GRACE_SECONDS
        record["client_exits"] = []
        for client in processes[1:]:
            try:
                record["client_exits"].append(client.wait(timeout=max(1.0, deadline - time.time())))
            except subprocess.TimeoutExpired:
                record["client_exits"].append(None)
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                env.kill_tree(process)
        env.restore_full_affinity()
        record["inherited_masks"] = masks          # read back from Windows per process: server first, then clients
        record["wall_seconds"] = round(time.time() - started, 1)
    record.update(collect(tag, run_dir, clients))
    return record


def collect(tag: str, run_dir: str, clients: int) -> dict:
    """Reads this run's files and computes its statistics."""
    found: dict = {}
    csv_path = os.path.join(run_dir, tag + "_server.csv")
    if os.path.exists(csv_path):
        try:
            found["csv"] = csv_stats.analyse(csv_path, csv_stats.DEFAULT_COLUMNS)
        except (ValueError, OSError) as error:
            found["csv_error"] = str(error)
    else:
        found["csv_error"] = "missing"
    report = read_json(os.path.join(run_dir, tag + "_server.json"))
    if report is None:
        found["server_report_error"] = "missing or unreadable"
    else:
        found["server_report"] = report
        rates = [c["bytes_per_second"] for c in report.get("connections", [])]
        if rates:
            found["bytes_per_second_per_client"] = csv_stats.summarize(rates)
        frames = max(1, report.get("window_frames", 1)) * max(1, len(report.get("connections", [])))
        found["saturated_share"] = report.get("saturated_connection_frames", 0) / frames
    client_reports = [r for r in (read_json(p) for p in sorted(glob.glob(os.path.join(run_dir, tag + "_client*.json"))))
                      if r is not None and "fps" in r and "bytes_per_second" in r]
    found["client_reports"] = len(client_reports)
    if client_reports:
        found["client_fps_min"] = min(c["fps"] for c in client_reports)
        found["client_fps_max"] = max(c["fps"] for c in client_reports)
        found["client_bytes_per_second_median"] = statistics.median(c["bytes_per_second"] for c in client_reports)
    found["all_clients_reported"] = len(client_reports) == clients
    return found


def is_valid(record: dict, capture_frames: int) -> bool:
    """Every process ended by itself with 0, every report is there, the CSV holds exactly the window, and every
    client ran at its cap (within 10%): a client off its cap sends and receives at another cadence."""
    return ("failure" not in record and record.get("server_exit") == 0
            and record.get("client_exits") and all(code == 0 for code in record["client_exits"])
            and bool(record.get("all_clients_reported")) and "server_report" in record
            and record.get("csv", {}).get("frames") == capture_frames
            and CLIENT_FPS / CLIENT_FPS_TOLERANCE <= record.get("client_fps_min", 0.0)
            and record.get("client_fps_max", float("inf")) <= CLIENT_FPS * CLIENT_FPS_TOLERANCE)


def summary_line(record: dict) -> str:
    columns = record.get("csv", {}).get("columns", {})
    flush = columns.get(csv_stats.NET_FLUSH, {}).get("median", float("nan"))
    rate = record.get("bytes_per_second_per_client", {}).get("median", float("nan"))
    long_frames = record.get("server_report", {}).get("long_frames", "?")
    return (f"valid={record['valid']} frames={record.get('csv', {}).get('frames')} flush median={flush:.3f} ms "
            f"B/s/client={rate:.0f} sat={record.get('saturated_share', float('nan')):.3f} long_frames={long_frames} "
            f"client_fps_min={record.get('client_fps_min', float('nan')):.1f}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configs", required=True, help="comma-separated, e.g. C1,C2,C3,C4,C6")
    parser.add_argument("--clients", default="8", help="comma-separated client counts, e.g. 8,16,32")
    parser.add_argument("--runs", type=int, default=1, help="rounds per combination (= number of configs for headline batches)")
    parser.add_argument("--scenario", choices=("A", "B"), default="A")
    parser.add_argument("--rate", type=int, help="bytes/s per client (default: the scenario's)")
    parser.add_argument("--loss", type=int, default=0, help="emulated packet loss in percent, every process")
    parser.add_argument("--warmup", type=float, default=20.0)
    parser.add_argument("--capture-frames", type=int, default=1800, help="measured server frames (1800 = 60 s at 30 Hz)")
    parser.add_argument("--settle", type=float, default=5.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--label", default="bench")
    parser.add_argument("--extra", default="", help="extra server arguments, space-separated (use --extra=...)")
    parser.add_argument("--client-extra", default="", help="extra client arguments (use --client-extra=...)")
    parser.add_argument("--no-prime", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    args.rate = args.rate or SCENARIO_RATE[args.scenario]
    configs = [c.strip() for c in args.configs.split(",") if c.strip()]
    unknown = [c for c in configs if c not in CONFIGS]
    if unknown:
        parser.error(f"unknown configs {unknown}")
    client_counts = [int(n) for n in args.clients.split(",")]

    # A dry run only prints the commands: no folder, no environment record, no manifest.
    batch, run_dir = ("dry", os.path.join(env.RAW_DIR, "dry")) if args.dry_run else env.new_batch(args.label)
    server_mask, client_mask = env.server_and_client_masks()
    manifest = {"batch": batch, "scenario": args.scenario, "rate": args.rate, "loss": args.loss,
                "configs": configs, "clients": client_counts, "runs": args.runs,
                "timing": {"warmup_s": args.warmup, "capture_frames": args.capture_frames, "settle_s": args.settle},
                "environment": {} if args.dry_run else environment_record(server_mask, client_mask), "records": []}

    def run(config: str, clients: int, run_index: int, priming: bool = False) -> None:
        suffix = f"{config}_{args.scenario}_N{clients}" + (f"_L{args.loss}" if args.loss else "")
        tag = f"{batch}_{suffix}_r{run_index}" + ("_prime" if priming else "")
        print(f"[bench] {tag}", flush=True)
        record = run_once(config, clients, tag, run_dir, server_mask, client_mask, args)
        if args.dry_run:
            return
        record["priming"] = priming
        record["valid"] = is_valid(record, args.capture_frames)
        manifest["records"].append(record)
        env.write_manifest(batch, manifest)        # after every run, so a crash later keeps the runs already done
        print(f"[bench]   {summary_line(record)}", flush=True)

    if not args.no_prime:
        run(configs[0], client_counts[0], 0, priming=True)
    for round_index in range(1, args.runs + 1):
        rotation = (round_index - 1) % len(configs)
        order = configs[rotation:] + configs[:rotation]       # interleave: rotate the order every round
        for clients in client_counts:
            for config in order:
                run(config, clients, round_index)

    if args.dry_run:
        return 0
    manifest_path = env.write_manifest(batch, manifest)
    invalid = [r["tag"] for r in manifest["records"] if not r["valid"] and not r["priming"]]
    print(f"[bench] manifest: {manifest_path}; invalid runs: {invalid or 'none'}", flush=True)
    return 1 if invalid else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
