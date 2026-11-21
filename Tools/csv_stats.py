"""P05 NetCostLab - statistics for Unreal CSV-profiler captures (adapted from P01 UpdateBudget's csv_stats.py).

No tool shipped with UE 5.8 reports p95/p99 (P01 01b_Research_Findings.md section 4), so this script does.
The server's benchmark director opens and closes the capture itself (FCsvProfiler::BeginCapture/EndCapture), so every
data row of a P05 capture is a measured frame: nothing is sliced off.

Usage:
    python csv_stats.py CAPTURE.csv [CAPTURE2.csv ...] [--columns A,B,...] [--json OUT.json]
    python csv_stats.py --selftest

Rules (documented so every number can be reproduced):
  * A finalised Unreal capture ends with two non-data rows: the full header and a "[key],value,..." metadata row.
    Both are dropped, and the END header is used for column lookup (the first header is only a prefix of it when
    columns appear mid-capture). If they are missing (crash or partial write) every row is data and the result is flagged.
  * The metadata row can be very long, so the csv field-size limit is raised.
  * The EVENTS column holds "Category/Name##timestamp" entries separated by ";".
  * Percentiles use linear interpolation between closest ranks (numpy's default "linear" method):
      position = p/100 * (n - 1);  value = lower + (upper - lower) * fraction.
  * NetFlushMs, the server's net-tick time, is derived per frame as ServerReplicateActors + NetworkOutgoing
    (exclusive timers, 01b_Research_Findings.md #10); a frame counts only if both cells are present.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys

SERVER_REPLICATE = "Exclusive/GameThread/ServerReplicateActors"
NETWORK_OUTGOING = "Exclusive/GameThread/NetworkOutgoing"
NET_FLUSH = "NetFlushMs"
DEFAULT_COLUMNS = (
    "FrameTime",
    SERVER_REPLICATE,
    NETWORK_OUTGOING,
    "Exclusive/GameThread/NetworkIncoming",
    "NetCostLab/BusyMsPrevFrame",
    "NetCostLab/OutBytes",
    "NetCostLab/SatConnections",
    "NetCostLab/Clients",
)

csv.field_size_limit(1 << 30)


def percentile(sorted_values: list[float], p: float) -> float:
    """Linear-interpolation percentile of an already sorted, non-empty list. p in [0, 100]."""
    if not sorted_values:
        raise ValueError("percentile of an empty list")
    position = (p / 100.0) * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * fraction


def summarize(values: list[float]) -> dict:
    """Count, mean, min, median, p95, p99 and max of a list of numbers."""
    ordered = sorted(values)
    return {
        "n": len(ordered),
        "mean": sum(ordered) / len(ordered),
        "min": ordered[0],
        "median": percentile(ordered, 50.0),
        "p95": percentile(ordered, 95.0),
        "p99": percentile(ordered, 99.0),
        "max": ordered[-1],
    }


def load_capture(path: str) -> tuple[list[str], list[list[str]], dict, bool]:
    """Return (header, data rows, metadata dict, finalised) of an Unreal CSV capture."""
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        rows = list(csv.reader(handle))
    if len(rows) < 2:
        raise ValueError(f"{path}: too few rows to be an Unreal capture")
    # With HasHeaderRowAtEnd, the END header is authoritative: columns can be added during the capture, so the
    # first header is only a prefix of it (seen on real captures: 282 vs 286 columns).
    first_header = rows[0]
    finalised = (len(rows) >= 3 and bool(rows[-1]) and rows[-1][0].strip().startswith("[")
                 and rows[-2][:len(first_header)] == first_header)
    if not finalised:
        # A partial write: keep only rows that are not a header or the metadata row (a header may already be there).
        data = [row for row in rows[1:] if row[:len(first_header)] != first_header and not (row and row[0].startswith("["))]
        return first_header, data, {}, False

    header, data, meta_row = rows[-2], rows[1:-2], rows[-1]
    # Metadata pairs: [key],value,[key],value,...  Stored without brackets, lower-case.
    metadata = {}
    for index in range(0, len(meta_row) - 1, 2):
        key = meta_row[index].strip().strip("[]").lower()
        if key:
            metadata[key] = meta_row[index + 1].strip()
    return header, data, metadata, True


def events_index(header: list[str]) -> int | None:
    """Position of the EVENTS column, or None (looked up once per capture, not per frame)."""
    return next((i for i, h in enumerate(header) if h.lower() == "events"), None)


def frame_events(index: int | None, row: list[str]) -> list[str]:
    """Event names ("Category/Name", timestamps removed) recorded on one frame; index from events_index()."""
    if index is None or index >= len(row) or not row[index].strip():
        return []
    return [entry.split("##")[0].strip() for entry in row[index].split(";") if entry.strip()]


def column_values(header: list[str], rows: list[list[str]], column: str) -> list[float]:
    """Numeric values of one column (empty cells skipped). Raises ValueError if the column is absent."""
    index = header.index(column)
    values = []
    for row in rows:
        if index < len(row) and row[index].strip() != "":
            try:
                values.append(float(row[index]))
            except ValueError:
                pass            # a truncated last row of a partial capture
    return values


def row_sums(header: list[str], rows: list[list[str]], columns: tuple[str, ...]) -> list[float]:
    """Per-frame sum of several columns; a frame where any of them is absent or empty is skipped."""
    if any(column not in header for column in columns):
        return []
    indices = [header.index(column) for column in columns]
    return [sum(float(row[i]) for i in indices) for row in rows
            if all(i < len(row) and row[i].strip() != "" for i in indices)]


def summarize_rows(header: list[str], rows: list[list[str]], columns: tuple[str, ...]) -> dict:
    result = {}
    for column in columns:
        if column in header:
            values = column_values(header, rows, column)
            if values:
                result[column] = summarize(values)
    flush = row_sums(header, rows, (SERVER_REPLICATE, NETWORK_OUTGOING))
    if flush:
        result[NET_FLUSH] = summarize(flush)
    return result


def analyse(path: str, columns: tuple[str, ...]) -> dict:
    header, data, metadata, finalised = load_capture(path)
    event_counts: dict[str, int] = {}
    index = events_index(header)
    for row in data:
        for event in frame_events(index, row):
            key = event.split("/")[0] if event.startswith("PSO/") else event
            event_counts[key] = event_counts.get(key, 0) + 1
    return {"file": os.path.basename(path), "frames": len(data), "finalised": finalised,
            "metadata": metadata, "events": event_counts, "columns": summarize_rows(header, data, columns)}


def selftest() -> None:
    """Checks the statistics and parsing against values worked out by hand."""
    values = [float(v) for v in range(1, 11)]                   # 1..10
    assert percentile(values, 50) == 5.5                          # position 4.5 -> 5 + 0.5*(6-5)
    assert abs(percentile(values, 95) - 9.55) < 1e-12             # position 8.55 -> 9 + 0.55*1
    assert abs(percentile(values, 99) - 9.91) < 1e-12             # position 8.91 -> 9 + 0.91*1
    assert percentile([7.0], 99) == 7.0
    s = summarize([4.0, 1.0, 3.0, 2.0])
    assert s["n"] == 4 and s["min"] == 1.0 and s["max"] == 4.0 and s["mean"] == 2.5 and s["median"] == 2.5
    header = ["EVENTS", "FrameTime", SERVER_REPLICATE, NETWORK_OUTGOING]
    assert events_index(header) == 0 and events_index(["FrameTime"]) is None
    assert frame_events(0, ["NetCostLab/BenchStart##1.5;PSO/Encountered new graphics PSO##1.6", "3", "1", "2"]) == \
        ["NetCostLab/BenchStart", "PSO/Encountered new graphics PSO"]
    assert frame_events(0, ["", "3", "1", "2"]) == [] and frame_events(None, ["x"]) == []
    rows = [["", "33", "1.5", "0.25"], ["", "33", "", "0.5"], ["", "33", "2.0", "0.75"]]
    assert row_sums(header, rows, (SERVER_REPLICATE, NETWORK_OUTGOING)) == [1.75, 2.75]   # the frame with a gap is skipped
    assert row_sums(header, rows, (SERVER_REPLICATE, "Missing")) == []
    assert summarize_rows(header, rows, ("FrameTime",))[NET_FLUSH]["median"] == 2.25
    print("selftest: OK (percentiles, summary, event parsing and the derived net-flush column match hand-worked values)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", nargs="*")
    parser.add_argument("--columns", default=",".join(DEFAULT_COLUMNS))
    parser.add_argument("--json")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)

    if args.selftest:
        selftest()
        return 0
    if not args.captures:
        parser.error("give at least one capture, or --selftest")

    columns = tuple(c.strip() for c in args.columns.split(",") if c.strip())
    results = [analyse(path, columns) for path in args.captures]
    for result in results:
        flag = "" if result["finalised"] else "  [NOT FINALISED: partial capture, treat with care]"
        print(f"{result['file']}  ({result['frames']} frames, tag={result['metadata'].get('nclruntag', '?')}, events={result['events']}){flag}")
        for column, s in result["columns"].items():
            print(f"  {column:<44} median {s['median']:9.3f}  p95 {s['p95']:9.3f}  p99 {s['p99']:9.3f}  max {s['max']:9.3f}  mean {s['mean']:9.3f}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(results, handle, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
