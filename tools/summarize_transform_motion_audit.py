#!/usr/bin/env python3
"""Retain compact, sequential local-transform evidence from a native camera audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re


def vector(line: str, name: str) -> list[float]:
    match = re.search(r"(?:^| )" + re.escape(name) + r"=\(([^)]+)\)", line)
    if not match:
        raise ValueError(f"missing {name}")
    return [float(value) for value in match.group(1).split()]


def normalized_rows(rows: list[list[float]]) -> list[list[float]]:
    return [[value / math.sqrt(sum(v * v for v in row)) for value in row]
            for row in rows]


def rotation_step(a: list[list[float]], b: list[list[float]]) -> float:
    trace = sum(x * y for r1, r2 in zip(normalized_rows(a), normalized_rows(b))
                for x, y in zip(r1, r2))
    return math.degrees(math.acos(max(-1.0, min(1.0, (trace - 1.0) / 2.0))))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audit", type=Path)
    parser.add_argument("--mesh", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    raw = args.audit.read_bytes()
    audit = json.loads(raw)
    rows = []
    base = None
    for line in audit["transform_samples"]:
        if f"mesh={args.mesh} " not in line or f"target={args.target} " not in line:
            continue
        sample = re.search(r" sample=(\d+) ", line)
        frame = re.search(r" source_frame=1:([-\d.]+) ", line)
        if not sample or not frame:
            raise ValueError("sample index or source frame missing")
        local = [vector(line, f"sampled_row{i}") for i in range(3)]
        current_base = [vector(line, f"base_row{i}") for i in range(3)]
        if base is not None and current_base != base:
            raise ValueError("base changed; extend evidence schema before compacting")
        base = current_base
        rows.append({"sample": int(sample.group(1)), "source_frame": float(frame.group(1)),
                     "source_quaternion_xyzw": vector(line, "quat"), "local_rows": local,
                     "step_degrees": round(rotation_step(rows[-1]["local_rows"], local), 6)
                     if rows else None})
    if not rows:
        raise ValueError("no matching local-transform samples")
    animation = [line for line in audit["animation_samples"]
                 if f"mesh={args.target} " in line and "AnimFilter sample" in line]
    filters = sorted({line for line in audit["animation_samples"]
                      if "AnimFilter sample" not in line and "filter=" in line})
    events = {}
    publications = []
    for line in animation:
        event = re.search(r" event=(\S+)", line).group(1)
        if event not in events:
            events[event] = {"first_sample": line, "samples": 0}
        events[event]["samples"] += 1
        events[event]["last_sample"] = line
        publications.append({
            "event": event,
            "frame": float(re.search(r" frame=([-\d.]+)", line).group(1)),
            "blend": float(re.search(r" blend=([-\d.]+)", line).group(1)),
            "quaternion_xyzw": vector(line, "quat"),
        })
    payload = {"schema": 2, "input_sha256": hashlib.sha256(raw).hexdigest(),
               "venue": audit["venue"], "exe_sha256": audit["exe_sha256"],
               "mesh": args.mesh, "target": args.target,
               "scope": "Every matching native draw sample retained in order. Sample indices are not video frame indices. Not a retail parity pass.",
               "music_start": audit["music_start_samples"],
               "performance": audit["performance"], "source_filters": filters,
               "animation_events": events, "animation_publications": publications,
               "stored_base_rows": base,
               "samples": rows,
               "largest_steps": sorted(rows[1:], key=lambda row: row["step_degrees"], reverse=True)[:8]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")
    print(json.dumps({"samples": len(rows), "bytes": args.output.stat().st_size,
                      "largest_steps": [{k: r[k] for k in ("sample", "source_frame", "step_degrees")}
                                        for r in payload["largest_steps"][:4]]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
