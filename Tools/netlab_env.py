"""P05 NetCostLab - shared helpers for the run scripts: engine paths, CPU affinity, pinned process launch, memory sampling.

performance_core_mask(), pin_self(), applied_mask() and the KERNEL32 setup are copied from P01 (repository
UE5-UpdateBudget, Tools/run_bench.py); nothing is imported across folders, so this repository stays standalone.

Core assignment for every multi-process run (decided in Phase 2, 01b_Research_Findings.md section 1):
  - the dedicated server (the process we measure) gets the performance cores (logical 0-15 on this i7-14650HX, 0xFFFF);
  - the headless clients get the efficiency cores (logical 16-23, 0xFF0000), so they never compete with the server's
    game thread.
How: this script pins ITSELF to a mask right before it starts a process, so the child inherits the mask from its first
instruction (engine start-up included), then re-pins itself for the next kind of process. The real executable is
started directly with Popen, so its own exit code is visible. The mask each child got is read back and recorded.
"""
import ctypes
import datetime as dt
import json
import os
import re
import struct
import subprocess
import sys
import time

UE_ROOT = r"C:\Program Files\Epic Games\UE_5.8"
EDITOR_CMD = os.path.join(UE_ROOT, r"Engine\Binaries\Win64\UnrealEditor-Cmd.exe")
BUILD_BAT = os.path.join(UE_ROOT, r"Engine\Build\BatchFiles\Build.bat")
P05_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(P05_ROOT, "NetCostLab", "NetCostLab.uproject")
ENGINE = "UE 5.8.4 (CL 58902052), launcher build"
# Shared by every networked run (probe_net.py, run_bench.py): the map, the packaged headless client, common switches.
MAP = "/Engine/Maps/Templates/Template_Default"
PACKAGED_CLIENT = os.path.join(P05_ROOT, "Build", "Windows", "NetCostLab", "Binaries", "Win64", "NetCostLab.exe")
COMMON_ARGS = ["-log", "-unattended", "-nosplash", "-nosound", "-traceautostart=0", "-NoLiveCoding"]
IPNETDRIVER = "[/Script/OnlineSubsystemUtils.IpNetDriver]"
REPGRAPH_INI = f"{IPNETDRIVER}:ReplicationDriverClassName=/Script/ReplicationGraph.BasicReplicationGraph"
RAW_DIR = os.path.join(P05_ROOT, "Benchmarks", "Raw")

KERNEL32 = None
if sys.platform == "win32":
    KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    KERNEL32.GetCurrentProcess.restype = ctypes.c_void_p
    KERNEL32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    KERNEL32.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                                                ctypes.POINTER(ctypes.c_size_t)]


def performance_core_mask() -> int | None:
    """Affinity mask of the performance cores on a hybrid CPU; None on a non-hybrid CPU or off Windows.

    Source of truth: GetLogicalProcessorInformationEx(RelationProcessorCore) reports an EfficiencyClass per physical
    core; the P-cores have the higher class. Only processor group 0 exists on a machine with < 64 logical CPUs.
    """
    if KERNEL32 is None:
        return None
    length = ctypes.c_ulong(0)
    KERNEL32.GetLogicalProcessorInformationEx(0, None, ctypes.byref(length))
    buffer = ctypes.create_string_buffer(length.value)
    if not length.value or not KERNEL32.GetLogicalProcessorInformationEx(0, buffer, ctypes.byref(length)):
        return None
    raw, offset, cores = buffer.raw, 0, []
    while offset < length.value:
        relationship, size = struct.unpack_from("<II", raw, offset)
        if relationship == 0 and size >= 48:                     # RelationProcessorCore
            efficiency_class = raw[offset + 9]                   # PROCESSOR_RELATIONSHIP.EfficiencyClass
            mask, group = struct.unpack_from("<QH", raw, offset + 32)   # GroupMask[0]: KAFFINITY Mask, WORD Group
            if group == 0:
                cores.append((efficiency_class, mask))
        offset += size
    classes = {efficiency for efficiency, _ in cores}
    if len(classes) < 2:
        return None
    best = max(classes)
    combined = 0
    for efficiency, mask in cores:
        if efficiency == best:
            combined |= mask
    return combined


def pin_self(mask: int) -> bool:
    """Restricts this script to the CPUs in mask. Every process it starts afterwards inherits the mask from its first
    instruction, so each Unreal process runs pinned for its whole life, engine start-up included."""
    return bool(KERNEL32.SetProcessAffinityMask(KERNEL32.GetCurrentProcess(), mask))


def applied_mask() -> int:
    """The affinity this script actually has (read back from Windows, not the one requested)."""
    process_mask, system_mask = ctypes.c_size_t(0), ctypes.c_size_t(0)
    KERNEL32.GetProcessAffinityMask(KERNEL32.GetCurrentProcess(), ctypes.byref(process_mask), ctypes.byref(system_mask))
    return process_mask.value


def all_cores_mask() -> int:
    return (1 << os.cpu_count()) - 1


def server_and_client_masks() -> tuple[int, int]:
    """(server mask, client mask): P-cores for the server, the remaining (efficiency) cores for the clients."""
    p_mask = performance_core_mask()
    everything = all_cores_mask()
    if p_mask is None:          # not a hybrid CPU: no split possible, everyone shares everything (recorded as such)
        return everything, everything
    return p_mask, everything & ~p_mask


def launch_pinned(mask: int, exe: str, args: list[str], log_handle=None) -> tuple[subprocess.Popen, str]:
    """Pins this script to `mask`, starts the real executable directly, and returns (process, mask it inherited).

    The list form of Popen quotes paths with spaces correctly and keeps the child's own exit code. The script stays on
    `mask` until the next launch_pinned() or restore_full_affinity() call.
    """
    pin_self(mask)
    inherited = f"0x{applied_mask():X}"
    process = subprocess.Popen([exe] + args, stdout=log_handle or subprocess.DEVNULL, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    return process, inherited


def restore_full_affinity() -> None:
    pin_self(all_cores_mask())


def kill_tree(process: subprocess.Popen) -> None:
    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
    try:
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        pass


def working_sets_mb(image_name: str) -> list[float]:
    """Working-set size (MB) of every running process with this image name, from `tasklist` (no extra packages)."""
    result = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {image_name}", "/FO", "CSV", "/NH"],
                            capture_output=True, text=True)
    sizes = []
    for line in result.stdout.splitlines():
        fields = [f.strip('"') for f in line.split('","')]
        if len(fields) >= 5 and fields[0].lower() == image_name.lower():
            kilobytes = fields[4].replace("K", "").replace(",", "").replace(".", "").strip()
            if kilobytes.isdigit():
                sizes.append(round(int(kilobytes) / 1024.0, 1))
    return sizes


def sanitize(text: str) -> str:
    """Removes personal details before a manifest is committed: the user-profile folder becomes %USERPROFILE%, and the
    computer and user names become <host> / <user>. Unreal names each player "<computer name>-<hex>", so join lines in
    the logs would otherwise carry the machine's name."""
    profile = os.environ.get("USERPROFILE", "")
    if profile:
        for variant in (profile, profile.replace("\\", "/"), profile.replace("\\", "\\\\")):
            text = text.replace(variant, "%USERPROFILE%")
    for variable, placeholder in (("COMPUTERNAME", "<host>"), ("USERNAME", "<user>")):
        name = os.environ.get(variable, "")
        if len(name) >= 3:
            text = re.sub(rf"(?i)\b{re.escape(name)}\b", placeholder, text)
    return text


def new_batch(label: str) -> tuple[str, str]:
    """A batch name (date, time, label) and its folder Benchmarks/Raw/<batch>/ (git-ignored: full logs), created."""
    batch = dt.datetime.now().strftime("%Y-%m-%d_%H%M%S") + "_" + label
    out_dir = os.path.join(RAW_DIR, batch)
    os.makedirs(out_dir, exist_ok=True)
    return batch, out_dir


def write_manifest(batch: str, manifest: dict) -> str:
    """Writes Benchmarks/Raw/<batch>_manifest.json (committed), passed through sanitize(); returns its path."""
    path = os.path.join(RAW_DIR, f"{batch}_manifest.json")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(sanitize(json.dumps(manifest, indent=2)))
    return path


def wait_for_text(path: str, needle: str, timeout: float, count: int = 1) -> bool:
    """Polls a log file until `needle` appears `count` times or the timeout passes. Prints a heartbeat for the queue."""
    deadline = time.time() + timeout
    last_beat = 0.0
    while time.time() < deadline:
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                if handle.read().count(needle) >= count:
                    return True
        except OSError:
            pass
        if time.time() - last_beat > 10:
            print(f"  waiting for '{needle}' x{count} in {os.path.basename(path)}", flush=True)
            last_beat = time.time()
        time.sleep(1.0)
    return False
