"""P05 NetCostLab - one queue job for the checks of one Phase 5 step, in order.

Steps (each optional, always in this order; a later step is skipped once an earlier one failed):
  --build              build.bat NetCostLabEditor Development, then build.bat NetCostLab Development
  --tests FILTER       the editor's automation tests whose names start with FILTER (headless, -nullrhi); the editor's
                       log goes into the batch folder and every test's result into the manifest. The editor itself
                       exits non-zero if no test matched or one failed (AutomationCommandline.cpp:346-355, 493-504)
  --package            package.bat Development (needed whenever packaged clients must run new code)
  --smoke              smoke_game.bat packaged
  --probe "ARGS"       probe_net.py ARGS (repeatable): editor-binary server + headless clients
  --bench "ARGS"       run_bench.py ARGS (repeatable): measured runs with the director (step S2 on)

Every child's output is streamed to the job log and copied to Benchmarks\\Raw\\<batch>\\<step>.log; the summary manifest
goes to Benchmarks\\Raw\\<batch>_manifest.json (sanitised).

Run through the queue only, for example:
  python -I -u "D:\\UNREAL PORTFOLIO\\tools\\ue_queue.py" run --project P05 --label "..." --timeout 4500 --idle 1500 --
      'python -I -u "D:\\UNREAL PORTFOLIO\\P05_NetCostLab\\Tools\\check_job.py" --label s1 --build --package
       --probe "--client packaged --setups legacy,iris --skip-server-target --label s1a --extra=-NCLCountLog=5"'
  (an --extra value that starts with "-" must be written --extra=VALUE, or argparse reads it as an option)
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import foundation_job  # noqa: E402  (TOOLS, PY, run_step: streaming, counting, logging)
import netlab_env as env  # noqa: E402

TOOLS, PY = foundation_job.TOOLS, foundation_job.PY
# One line per test in the editor's log: "Test Completed. Result={Success} Name={...} Path={...}".
TEST_RESULT = re.compile(r"Test Completed\. Result=\{(\w+)\} Name=\{[^}]*\} Path=\{([^}]*)\}")


def split_args(text: str) -> list[str]:
    """Splits a --probe string: whitespace separates arguments except inside double quotes, the quotes are removed and
    backslashes are kept (Windows paths). shlex would treat a backslash as an escape character."""
    return [token.replace('"', "") for token in re.findall(r'(?:[^\s"]|"[^"]*")+', text)]


def test_results(log_path: str) -> dict:
    """Result per test path, from the editor's log of this run (a fresh batch folder, so never an old run's log)."""
    if not os.path.exists(log_path):
        return {}
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        return {path: result for result, path in TEST_RESULT.findall(handle.read())}


def main() -> int:
    parser = argparse.ArgumentParser(description="P05 per-step checks (run through the queue only)")
    parser.add_argument("--label", required=True)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--tests", default="")
    parser.add_argument("--package", action="store_true")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--probe", action="append", default=[])
    parser.add_argument("--bench", action="append", default=[])
    args = parser.parse_args()

    batch, out_dir = env.new_batch(args.label)
    tests_log = os.path.join(out_dir, "tests_editor.log")
    steps = []
    if args.build:
        steps.append(("build_editor", ["cmd", "/c", os.path.join(TOOLS, "build.bat"), "NetCostLabEditor", "Development"]))
        steps.append(("build_game", ["cmd", "/c", os.path.join(TOOLS, "build.bat"), "NetCostLab", "Development"]))
    if args.tests:
        # Relative to Tools (no spaces): "cmd /c" strips the outer quotes of a line with two quoted, spaced arguments.
        steps.append(("tests", ["cmd", "/c", os.path.join(TOOLS, "run_tests.bat"), args.tests,
                                os.path.relpath(tests_log, TOOLS)]))
    if args.package:
        steps.append(("package", ["cmd", "/c", os.path.join(TOOLS, "package.bat"), "Development"]))
    if args.smoke:
        steps.append(("smoke", ["cmd", "/c", os.path.join(TOOLS, "smoke_game.bat"), "packaged"]))
    for kind, script in (("probe", "probe_net.py"), ("bench", "run_bench.py")):     # each repeatable, in this order
        for index, script_args in enumerate(getattr(args, kind)):
            steps.append((f"{kind}{index + 1}", PY + [os.path.join(TOOLS, script)] + split_args(script_args)))

    manifest = {"batch": batch, "purpose": f"step checks '{args.label}'", "engine": env.ENGINE, "steps": {}}
    failed = None
    for name, command in steps:
        if failed:
            manifest["steps"][name] = {"skipped_because": failed}
            print(f"[job] ===== {name}: skipped (after {failed} failed)", flush=True)
            continue
        result = foundation_job.run_step(name, command, out_dir)
        if name == "tests":
            result["results"] = test_results(tests_log)
        manifest["steps"][name] = result
        if result["exit_code"] != 0:
            failed = name
    manifest_path = env.write_manifest(batch, manifest)
    print(f"\n[job] manifest: {manifest_path}; failed step: {failed or 'none'}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
