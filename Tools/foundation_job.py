"""P05 NetCostLab - one queue job for the Phase 2 probe and every Phase 4 check, in dependency order.

Why one job: every step needs the machine to itself, and the queue serves ten projects. Running them back to back in one
job keeps the order fixed and avoids waiting in line six times.

Steps (a step is skipped if a step it depends on failed):
  F1/F2  build.bat NetCostLabEditor Development     editor target = game module + plugin (also the server's modules)
  P2-1   probe_net.py                               Server target attempt + editor -server with 2 editor clients x 3
  F1     build.bat NetCostLab Development           game target (the packaged clients)
  F3a    package.bat Development                    build, cook, stage, pak, archive into ..\\Build
  F3b    smoke_game.bat packaged                    the real packaged exe, headless, loads the map and quits
  F4     probe_net.py --client packaged ...         editor -server + 2 PACKAGED headless clients, legacy + Iris

Every child's output is streamed to this job's log (the queue's idle watchdog sees progress) and copied to
Benchmarks\\Raw\\<batch>\\<step>.log. A summary manifest with exit codes and warning/error counts goes to
Benchmarks\\Raw\\<batch>_manifest.json.

Run through the queue only:
  python -I -u "D:\\UNREAL PORTFOLIO\\tools\\ue_queue.py" run --project P05 --label "..." --timeout 4500 --idle 1500
      -- 'python -I -u "D:\\UNREAL PORTFOLIO\\P05_NetCostLab\\Tools\\foundation_job.py"'
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import netlab_env as env  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
PY = [sys.executable, "-I", "-u"]
WARNING = re.compile(r"\bwarning\b[ :]|: Warning: ", re.IGNORECASE)
ERROR = re.compile(r"\berror\b[ :]|: Error: ", re.IGNORECASE)
NOISE = re.compile(r"0 error\(s\)|0 warning\(s\)|-WarningsAsErrors|warnings? as errors|LogInit: Display", re.IGNORECASE)

STEPS = [
    ("F1-F2_build_editor", ["cmd", "/c", os.path.join(TOOLS, "build.bat"), "NetCostLabEditor", "Development"], []),
    ("P2-1_probe", PY + [os.path.join(TOOLS, "probe_net.py")], ["F1-F2_build_editor"]),
    ("F1_build_game", ["cmd", "/c", os.path.join(TOOLS, "build.bat"), "NetCostLab", "Development"], []),
    ("F3a_package", ["cmd", "/c", os.path.join(TOOLS, "package.bat"), "Development"], ["F1_build_game"]),
    ("F3b_smoke_packaged", ["cmd", "/c", os.path.join(TOOLS, "smoke_game.bat"), "packaged"], ["F3a_package"]),
    ("F4_packaged_clients", PY + [os.path.join(TOOLS, "probe_net.py"), "--client", "packaged", "--setups",
                                  "legacy_push,iris", "--skip-server-target", "--label", "f4"],
     ["F1-F2_build_editor", "F3a_package"]),
]


def run_step(name: str, command: list[str], out_dir: str) -> dict:
    print(f"\n[job] ===== {name}: {' '.join(command)}", flush=True)
    started = time.time()
    warnings, errors, samples = 0, 0, []
    log_path = os.path.join(out_dir, f"{name}.log")
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                 encoding="utf-8", errors="replace")
        for line in child.stdout:
            print(line, end="", flush=True)
            log.write(line)
            if NOISE.search(line):
                continue
            if ERROR.search(line):
                errors += 1
                if len(samples) < 12:
                    samples.append(line.strip()[:240])
            elif WARNING.search(line):
                warnings += 1
                if len(samples) < 12:
                    samples.append(line.strip()[:240])
        code = child.wait()
    result = {"command": command, "exit_code": code, "seconds": round(time.time() - started, 1),
              "warning_lines": warnings, "error_lines": errors, "samples": samples}
    print(f"[job] ===== {name}: exit {code}, {result['seconds']} s, {warnings} warning lines, {errors} error lines",
          flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="P05 Phase 2 probe + Phase 4 checks (run through the queue only)")
    parser.add_argument("--only", default="", help="comma-separated step names; steps outside the list count as "
                                                   "already done (a re-run after a fix)")
    parser.add_argument("--label", default="foundation")
    args = parser.parse_args()
    selected = [s for s in STEPS if not args.only or s[0] in args.only.split(",")]
    batch, out_dir = env.new_batch(args.label)
    manifest = {"batch": batch, "purpose": "Phase 2 probe P2-1 and Phase 4 checks F1-F4, one queue job",
                "engine": env.ENGINE, "only": args.only or "all", "steps": {}}
    selected_names = {s[0] for s in selected}
    failed = set()
    for name, command, needs in selected:
        missing = [n for n in needs if n in failed or (n in selected_names and n not in manifest["steps"])]
        if missing:
            manifest["steps"][name] = {"skipped_because": missing}
            failed.add(name)
            print(f"[job] ===== {name}: skipped (needs {missing})", flush=True)
            continue
        result = run_step(name, command, out_dir)
        manifest["steps"][name] = result
        if result["exit_code"] != 0:
            failed.add(name)
    manifest_path = env.write_manifest(batch, manifest)
    print(f"\n[job] manifest: {manifest_path}; failed steps: {sorted(failed) or 'none'}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
