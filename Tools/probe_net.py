"""P05 NetCostLab - Phase 2 probe P2-1: what can this launcher engine run as a dedicated server with headless clients?

One queue job (every Unreal process runs inside it):
  1. Ask UnrealBuildTool to build the Server target (NetCostLab/Source/NetCostLabServer.Target.cs).
     Expected from the source (UEBuildTarget.cs:1396-1404): refused, because the installed engine lists no Server type.
  2. For three replication setups (legacy + push model, Replication Graph, Iris), start the editor binary as a dedicated
     server (-server) pinned to the P-cores, plus two headless editor-binary clients (-game -nullrhi) pinned to the
     E-cores (the script pins itself, then starts each real exe with Popen, so it inherits the mask); wait until both clients are
     welcomed, sample every process's working set, then stop everything.
  3. Write <batch>_manifest.json (findings + exact command lines) to Benchmarks/Raw; full logs go to a git-ignored
     sub-folder.

Usage (through the queue only):
  python -I -u "D:\\UNREAL PORTFOLIO\\tools\\ue_queue.py" run --project P05 --label "P05 Phase 2 probe" --timeout 1800
      --idle 400 -- python -I -u "D:\\UNREAL PORTFOLIO\\P05_NetCostLab\\Tools\\probe_net.py"
  Phase 4 check F4 (editor-binary server + PACKAGED headless clients, no UBT step):
      ... probe_net.py --client packaged --setups legacy_push,iris --skip-server-target --label f4
"""
import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import netlab_env as env  # noqa: E402  (the folder is put on the path explicitly; python -I ignores the script dir)

MAP = "/Engine/Maps/Templates/Template_Default"
CLIENTS = 2
PACKAGED_CLIENT = os.path.join(env.P05_ROOT, "Build", "Windows", "NetCostLab", "Binaries", "Win64", "NetCostLab.exe")
COMMON = ["-log", "-unattended", "-nosplash", "-nosound", "-traceautostart=0", "-NoLiveCoding"]

# Each setup: extra arguments for server and clients, plus console commands the server prints at startup as evidence.
SETUPS = {
    "legacy_push": {
        "args": ["-UseIrisReplication=0", "-dpcvars=Net.IsPushModelEnabled=1"],
        "server_exec": "Net.IsPushModelEnabled,net.Iris.UseIrisReplication,net.Iris.PushModelMode",
    },
    "repgraph": {
        "args": ["-UseIrisReplication=0",
                 "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ReplicationDriverClassName="
                 "/Script/ReplicationGraph.BasicReplicationGraph"],
        "server_exec": "Net.IsPushModelEnabled,Net.RepGraph.PrintGraph",
    },
    "iris": {
        "args": ["-UseIrisReplication=1", "-dpcvars=Net.IsPushModelEnabled=1"],
        "server_exec": "Net.IsPushModelEnabled,net.Iris.UseIrisReplication,net.Iris.PushModelMode",
    },
}

# Log lines kept as evidence (regex, searched in every log).
EVIDENCE = [
    r"IpNetDriver listening on port \d+",
    r"Bringing .* up for play \(max tick rate \d+\)",
    r"Join succeeded: .*",
    r"Welcomed by server .*",
    r"Cmdline -UseIrisReplication=\d is forcing .*",
    r"ReplicationDriverClass is null.*",
    r"Net\.IsPushModelEnabled = .*",
    r"net\.Iris\.UseIrisReplication = .*",
    r"net\.Iris\.PushModelMode = .*",
    r"Setting Device Profile CVar: .*",
    r"UReplicationGraph.*|ReplicationGraphNode_GridSpatialization2D.*",
    r"LogIris: .*",
    r"Iris.*mismatch.*",
    r"Fatal error.*|Assertion failed.*",
]


def scan(path: str) -> dict:
    found, errors, warnings = {}, 0, 0
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
    except OSError:
        return {"missing": True}
    for line in lines:
        if ": Error: " in line:
            errors += 1
        elif ": Warning: " in line:
            warnings += 1
        for pattern in EVIDENCE:
            match = re.search(pattern, line)
            if match:
                hits = found.setdefault(pattern, [])
                if len(hits) < 4:
                    hits.append(match.group(0).strip()[:220])
    return {"errors": errors, "warnings": warnings, "evidence": found, "lines": len(lines)}


def first_lines(path: str, needle: str, limit: int = 15) -> list[str]:
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return [line.strip()[:220] for line in handle if needle in line][:limit]
    except OSError:
        return []


def probe_server_target(out_dir: str) -> dict:
    print("[probe] step 1: UnrealBuildTool, Server target", flush=True)
    log_path = os.path.join(out_dir, "ubt_server_target.log")
    command = f'"{env.BUILD_BAT}" NetCostLabServer Win64 Development -Project="{env.PROJECT}" -WaitMutex'
    started = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        code = subprocess.call(command, stdout=log, stderr=subprocess.STDOUT, shell=True)
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    match = re.search(r"[^\r\n]*(not currently supported|cannot be built|engine distribution)[^\r\n]*", text)
    result = {"command": command, "exit_code": code, "seconds": round(time.time() - started, 1),
              "message": match.group(0).strip() if match else None}
    print(f"[probe]   exit {code}: {result['message']}", flush=True)
    return result


def probe_setup(name: str, setup: dict, out_dir: str, server_mask: int, client_mask: int,
                client_kind: str = "editor") -> dict:
    print(f"[probe] setup {name}", flush=True)
    server_log = os.path.join(out_dir, f"{name}_server.log")
    client_logs = [os.path.join(out_dir, f"{name}_client{i}.log") for i in range(CLIENTS)]
    for path in [server_log] + client_logs:
        if os.path.exists(path):
            os.remove(path)

    server_args = [env.PROJECT, MAP, "-server", f"-abslog={server_log}", f"-ExecCmds={setup['server_exec']}"] \
        + COMMON + setup["args"]
    if client_kind == "packaged":      # the real game exe (not the bootstrap); a staged build knows its own project
        client_exe = PACKAGED_CLIENT
        client_args = [["127.0.0.1", "-nullrhi", f"-abslog={path}"] + COMMON + setup["args"] for path in client_logs]
    else:                              # the editor binary in -game mode, uncooked
        client_exe = env.EDITOR_CMD
        client_args = [[env.PROJECT, "127.0.0.1", "-game", "-nullrhi", f"-abslog={path}"] + COMMON + setup["args"]
                       for path in client_logs]
    result = {"server_command": server_args, "client_exe": client_exe, "client_command": client_args[0], "steps": {}}
    processes, inherited_masks = [], []
    started = time.time()
    try:
        process, mask = env.launch_pinned(server_mask, env.EDITOR_CMD, server_args)
        processes.append(process)
        inherited_masks.append(mask)
        result["steps"]["server_listening"] = env.wait_for_text(server_log, "IpNetDriver listening on port", 240)
        result["steps"]["server_listening_s"] = round(time.time() - started, 1)
        if result["steps"]["server_listening"]:
            for args in client_args:
                process, mask = env.launch_pinned(client_mask, client_exe, args)
                processes.append(process)
                inherited_masks.append(mask)
            joined = env.wait_for_text(server_log, "Join succeeded", 300, count=CLIENTS)
            welcomed = all(env.wait_for_text(path, "Welcomed by server", 30) for path in client_logs)
            result["steps"]["clients_joined"] = joined
            result["steps"]["clients_welcomed"] = welcomed
            result["steps"]["joined_s"] = round(time.time() - started, 1)
            time.sleep(15)                                   # a short steady state with everyone connected
            result["working_set_mb"] = {image: sorted(env.working_sets_mb(image), reverse=True)
                                        for image in sorted({"UnrealEditor-Cmd.exe", os.path.basename(client_exe)})}
            result["exited_early"] = [p.poll() for p in processes]     # None = still running, as it should be
    finally:
        for process in reversed(processes):
            env.kill_tree(process)
        env.restore_full_affinity()
        time.sleep(3)
    result["inherited_masks"] = inherited_masks     # server first, then the clients
    result["server_log"] = scan(server_log)
    result["client_logs"] = [scan(path) for path in client_logs]
    result["server_errors_sample"] = first_lines(server_log, ": Error: ")
    result["client0_errors_sample"] = first_lines(client_logs[0], ": Error: ")
    print(f"[probe]   {name}: {json.dumps(result['steps'])} mem={result.get('working_set_mb')}", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="P05 network probe (run it through the machine queue only)")
    parser.add_argument("--client", choices=("editor", "packaged"), default="editor")
    parser.add_argument("--setups", default=",".join(SETUPS))
    parser.add_argument("--skip-server-target", action="store_true")
    parser.add_argument("--label", default="probe")
    args = parser.parse_args()
    batch = dt.datetime.now().strftime("%Y-%m-%d_%H%M%S") + "_" + args.label
    raw_dir = os.path.join(env.P05_ROOT, "Benchmarks", "Raw")
    out_dir = os.path.join(raw_dir, batch)
    os.makedirs(out_dir, exist_ok=True)
    server_mask, client_mask = env.server_and_client_masks()
    manifest = {
        "batch": batch,
        "purpose": "Phase 2 probe P2-1: Server target, editor-binary dedicated server, headless clients, 3 setups",
        "engine": "UE 5.8.4 (CL 58902052), launcher build",
        "server_binary": "UnrealEditor-Cmd.exe -server (Development editor build), uncooked project",
        "client_binary": args.client,
        "map": MAP,
        "clients": CLIENTS,
        "affinity": {"server_mask": f"0x{server_mask:X}", "client_mask": f"0x{client_mask:X}",
                     "rule": "server on the performance cores, clients on the remaining cores"},
        "logical_cpus": os.cpu_count(),
    }
    if not args.skip_server_target:
        manifest["server_target"] = probe_server_target(out_dir)
    manifest["setups"] = {}
    for name in args.setups.split(","):
        manifest["setups"][name] = probe_setup(name, SETUPS[name], out_dir, server_mask, client_mask, args.client)
    manifest_path = os.path.join(raw_dir, f"{batch}_manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        handle.write(env.sanitize(json.dumps(manifest, indent=2)))
    print(f"[probe] manifest: {manifest_path}", flush=True)
    ok = all(s["steps"].get("clients_joined") for s in manifest["setups"].values())
    return 0 if ok else 3


if __name__ == "__main__":
    sys.exit(main())
