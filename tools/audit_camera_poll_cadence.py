"""Compare native camera polls by clock phase/frame, never imply retail parity."""
import argparse
import bisect
import json
from pathlib import Path

from audit_camera_motion_segments import audit
from audit_native_camera_handoff_trace import ROW, vector, angle_degrees


def parse(source):
    rows, phase, previous = [], 0, None
    for index, line in enumerate(source['camera_transform_samples']):
        match = ROW.search(line)
        if not match:
            raise ValueError(f'unparsed row {index}')
        frame = float(match[3])
        if previous is not None and frame < previous - .001:
            phase += 1
        rows.append(dict(index=index, phase=phase, frame=frame,
                         shots=(match[1], match[2]), position=vector(match[7]),
                         forward=vector(match[8])))
        previous = frame
    return rows


def compare(left, right, expected_hash):
    for source in (left, right):
        check = audit(source, expected_hash, source['expected_frame_count'])
        if not check['integrity_pass']:
            raise ValueError(check['errors'])
    if left['venue'] != right['venue']:
        raise ValueError('different venues')
    a, b = parse(left), parse(right)
    phases = {}
    for row in b:
        phases.setdefault(row['phase'], []).append(row)
    frames = {phase: [row['frame'] for row in rows] for phase, rows in phases.items()}
    matched, missing_clock, different_shot = [], 0, 0
    for row in a:
        candidates = phases.get(row['phase'], [])
        at = bisect.bisect_left(frames.get(row['phase'], []), row['frame'])
        nearby = candidates[max(0, at-1):at+1]
        match = min(nearby, key=lambda r: abs(r['frame']-row['frame'])) if nearby else None
        if match is None or abs(match['frame']-row['frame']) > .001:
            missing_clock += 1
        elif match['shots'] != row['shots']:
            different_shot += 1
        else:
            matched.append(dict(left=row['index'], right=match['index'], phase=row['phase'],
                frame=row['frame'], position_component_delta=max(abs(x-y) for x,y in
                    zip(row['position'], match['position'])),
                forward_degrees=angle_degrees(row['forward'], match['forward']),
                elapsed_offset=row['index']*left['fixed_dt']-match['index']*right['fixed_dt']))
    summaries = []
    for phase in sorted({r['phase'] for r in matched}):
        rows = [r for r in matched if r['phase']==phase]
        summaries.append(dict(phase=phase, matched_samples=len(rows),
            peak_position=max(rows, key=lambda r:r['position_component_delta']),
            peak_forward=max(rows, key=lambda r:r['forward_degrees']),
            elapsed_offset_range=[min(r['elapsed_offset'] for r in rows),
                                  max(r['elapsed_offset'] for r in rows)]))
    same_poll_cadence = abs(left['fixed_dt'] - right['fixed_dt']) <= 1e-12
    return dict(venue=left['venue'], exe_sha256=expected_hash,
        left_samples=len(a), right_samples=len(b), left_dt=left['fixed_dt'], right_dt=right['fixed_dt'],
        comparison_kind=('same_source_cadence_repeatability' if same_poll_cadence
                         else 'cross_poll_cadence_sensitivity'),
        matched_samples=len(matched), missing_clock_samples=missing_clock,
        different_shot_samples=different_shot, phases=summaries,
        scope=('Same-source-clock native comparison, not retail parity. Phase increments on clock rewind; '
               'matching tolerance .001 authored frame. At different poll cadences, GH2 CamShot::Shake, '
               'camera picks, and duration draws consume one process-wide source RNG stream, so shot sequences '
               'and live-target filter history may differ. Equal 60 Hz inputs instead test repeatability.'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path)
    parser.add_argument('right', type=Path)
    parser.add_argument('--exe-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = compare(json.loads(args.left.read_text()), json.loads(args.right.read_text()), args.exe_sha256)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f"{result['venue']}: matched={result['matched_samples']} different_shot={result['different_shot_samples']}")
    for phase in result['phases']:
        print(f"phase{phase['phase']}: position={phase['peak_position']['position_component_delta']:.7g} "
              f"forward={phase['peak_forward']['forward_degrees']:.7g}deg elapsed_offset={phase['elapsed_offset_range']}")


if __name__ == '__main__':
    main()
