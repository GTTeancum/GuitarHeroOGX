"""Compare native music_start dispatch telemetry with original-disc MIDI timing."""
import argparse
import json
from pathlib import Path
import re


def audit(chart, capture, start):
    authored = chart["music_start"]
    lines = capture.get("music_start_samples", [])
    actual = []
    for line in lines:
        match = re.search(r"tick=(\d+) chart=([\d.]+) clock=([\d.]+) intro=(\d+)", line)
        if match:
            tick, chart_time, clock, intro = match.groups()
            actual.append(dict(tick=int(tick), chart=float(chart_time),
                               clock=float(clock), intro=int(intro)))
    checks = dict(native_smoke=capture.get("smoke_pass") is True,
                  source_has_one_event=len(authored) == 1,
                  native_has_one_event=len(actual) == 1 and len(lines) == 1)
    if len(authored) == len(actual) == 1:
        source, native = authored[0], actual[0]
        expected = max(source["seconds"], start)
        checks.update(source_tick=native["tick"] == source["tick"],
                      source_tempo_map=abs(native["chart"] - source["seconds"]) < 1e-5,
                      not_during_intro=native["intro"] == 0,
                      first_due_tick=-1e-5 <= native["clock"] - expected <= 1/30 + 1e-5)
    return dict(pass_all=all(checks.values()), checks=checks, actual=actual,
                source_path=chart["path"], source_sha256=chart["sha256"],
                executable_sha256=capture["exe_sha256"],
                scope="Authored event timing and native delivery only; not complete camera parity")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--song", default="songs/shoutatthedevil/shoutatthedevil.mid")
    parser.add_argument("--start", type=float, default=0)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.source.read_text(encoding="utf-8"))
    chart = next(row for row in source["charts"] if row["path"] == args.song)
    result = audit(chart, json.loads(args.capture.read_text(encoding="utf-8")), args.start)
    args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"{args.capture.parent.name}: {sum(result['checks'].values())}/{len(result['checks'])} checks passed")
    return 0 if result["pass_all"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
