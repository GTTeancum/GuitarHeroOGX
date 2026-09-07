"""Verify the source six-downbeat intro-to-regular camera handoff.

The native input is emitted by ``audit_camera_venue_matrix.py``.  The retail
input is the compact output of ``summarize_gh2_retail_camera_trace.py``.  This
audit compares the lifecycle contract rather than camera names, which vary by
venue and weighted selection state.
"""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path


def field(line: str, name: str) -> str:
    match = re.search(rf"(?:^| ){re.escape(name)}=([^ ]+)", line)
    if not match:
        raise ValueError(f"missing {name}= in {line!r}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--retail-summary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    native = json.loads(args.native.read_text(encoding="utf-8"))
    retail = json.loads(args.retail_summary.read_text(encoding="utf-8"))
    lifecycle = native.get("lifecycle_samples", [])
    intro_rows = [row for row in lifecycle if row.startswith("[world] intro camera current:")]
    downbeat_rows = [row for row in lifecycle if row.startswith("[world] camera downbeat:")]
    prepoll_rows = [row for row in lifecycle if row.startswith("[world] camera PrePoll:")]
    sweep_rows = native.get("sweeps", [])

    checks: list[dict[str, object]] = []

    def check(name: str, passed: bool, actual: object, expected: object) -> None:
        checks.append({"name": name, "passed": bool(passed), "actual": actual,
                       "expected": expected})

    check("one native intro-current publication", len(intro_rows) == 1,
          len(intro_rows), 1)
    intro_name = ""
    if intro_rows:
        intro_name = field(intro_rows[0], "shot")
        start = float(field(intro_rows[0], "start_time"))
        bars = int(field(intro_rows[0], "bars_left"))
        check("native intro starts at negative owning duration", start < 0.0,
              start, "< 0")
        check("intro_start_msg owns six bars", bars == 6, bars, 6)

    first_six = downbeat_rows[:6]
    check("six native downbeats retained", len(first_six) == 6,
          len(first_six), 6)
    parsed_downbeats: list[dict[str, object]] = []
    for row in first_six:
        parsed_downbeats.append({
            "bar": int(field(row, "bar")),
            "beat": int(field(row, "beat")),
            "t": float(field(row, "t")),
            "bars_elapsed": int(field(row, "bars_elapsed")),
            "bars_left": int(field(row, "bars_left")),
            "pick_new_shot": int(field(row, "pick_new_shot")),
        })
    expected_bars = list(range(6))
    expected_beats = [0, 4, 8, 12, 16, 20]
    expected_left = [5, 4, 3, 2, 1, 0]
    expected_picks = [0, 0, 0, 0, 0, 1]
    if len(parsed_downbeats) == 6:
        check("native downbeat bars", [row["bar"] for row in parsed_downbeats] == expected_bars,
              [row["bar"] for row in parsed_downbeats], expected_bars)
        check("native downbeat beats", [row["beat"] for row in parsed_downbeats] == expected_beats,
              [row["beat"] for row in parsed_downbeats], expected_beats)
        check("native six-bar countdown", [row["bars_left"] for row in parsed_downbeats] == expected_left,
              [row["bars_left"] for row in parsed_downbeats], expected_left)
        check("native pick only on sixth downbeat",
              [row["pick_new_shot"] for row in parsed_downbeats] == expected_picks,
              [row["pick_new_shot"] for row in parsed_downbeats], expected_picks)
        check("native downbeat times are strictly increasing",
              all(parsed_downbeats[i]["t"] < parsed_downbeats[i + 1]["t"]
                  for i in range(5)),
              [row["t"] for row in parsed_downbeats], "strictly increasing")

    check("native first regular PrePoll retained", bool(prepoll_rows),
          len(prepoll_rows), ">= 1")
    first_regular = ""
    if prepoll_rows:
        row = prepoll_rows[0]
        first_regular = field(row, "shot")
        previous = field(row, "previous")
        changed = int(field(row, "changed"))
        local_frame = float(field(row, "local_frame"))
        check("native PrePoll replaces intro", previous == intro_name,
              previous, intro_name)
        check("native PrePoll publishes a different shot",
              changed == 1 and first_regular != intro_name,
              {"changed": changed, "shot": first_regular},
              {"changed": 1, "shot": "non-intro"})
        check("native regular shot starts at frame zero",
              math.isclose(local_frame, 0.0, abs_tol=1e-6), local_frame, 0.0)

    check("one native first-handoff sweep retained", len(sweep_rows) == 1,
          len(sweep_rows), 1)
    if sweep_rows:
        sweep = sweep_rows[0]
        check("native sweep identifies intro as current source shot",
              field(sweep, "source_previous") == intro_name,
              field(sweep, "source_previous"), intro_name)
        check("native sweep occurs on bar five", int(field(sweep, "bar")) == 5,
              int(field(sweep, "bar")), 5)

    retail_handoffs = [
        row for row in retail.get("transitions", [])
        if str(row.get("from_shot", "")).lower().startswith("intro")
        and not str(row.get("to_shot", "")).lower().startswith("intro")
    ]
    check("one retail intro-to-regular handoff retained", len(retail_handoffs) == 1,
          len(retail_handoffs), 1)
    retail_handoff: dict[str, object] = retail_handoffs[0] if retail_handoffs else {}
    if retail_handoff:
        retail_start = float(retail_handoff["to_task_time"])
        check("retail first regular shot starts at beat 20",
              math.isclose(retail_start, 20.0, abs_tol=0.05),
              retail_start, "20.0 +/- 0.05")

    passed = all(bool(row["passed"]) for row in checks)
    report = {
        "schema": 1,
        "passed": passed,
        "scope": "source lifecycle parity; not camera-pose or full-video parity",
        "native": str(args.native.resolve()),
        "retail_summary": str(args.retail_summary.resolve()),
        "native_intro": intro_name,
        "native_first_regular": first_regular,
        "native_downbeats": parsed_downbeats,
        "retail_handoff": retail_handoff,
        "checks": checks,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"{sum(bool(row['passed']) for row in checks)}/{len(checks)} checks passed")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
