"""Classify CamShot interpolation branches present in a saved GH2 directory.

This consumes ``audit_gh2_saved_camshots.py`` JSON.  It deliberately separates
serialized target-list identity (SameTargets) from live non-null target
resolution (HasTargets), matching the two independent checks in retail Interp.
"""

import argparse
import json
from collections import Counter
from pathlib import Path


def pointer_is_non_null(value):
    try:
        return int(str(value), 0) != 0
    except (TypeError, ValueError):
        return False


def target_signature(frame):
    return Counter(
        (str(target.get("target", "0x0")), target.get("subpart"))
        for target in frame.get("targets", [])
    )


def parent_pointer(frame):
    return frame.get("parent", {}).get("target", "0x0")


def classify_pair(left, right):
    left_has_targets = any(
        pointer_is_non_null(target.get("target"))
        for target in left.get("targets", [])
    )
    right_has_targets = any(
        pointer_is_non_null(target.get("target"))
        for target in right.get("targets", [])
    )
    left_signature = target_signature(left)
    right_signature = target_signature(right)
    # 2664D0..26659C checks total size and membership, without consuming
    # matched entries; preserve its behavior even for repeated references.
    same_targets = (sum(left_signature.values()) == sum(right_signature.values())
                    and all(identity in right_signature for identity in left_signature))
    if not (left_has_targets or right_has_targets):
        branch = "no-live-targets"
    elif same_targets:
        branch = "same-targets"
    elif left_has_targets and right_has_targets:
        branch = "different-targets"
    elif left_has_targets:
        branch = "target-disappears"
    else:
        branch = "target-appears"
    return {
        "branch": branch,
        "left_has_targets": left_has_targets,
        "right_has_targets": right_has_targets,
        "same_targets": same_targets,
        "left_signature": [list(item) + [count]
                           for item, count in target_signature(left).items()],
        "right_signature": [list(item) + [count]
                            for item, count in target_signature(right).items()],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = json.loads(args.input.read_text(encoding="utf-8"))
    branch_counts = Counter()
    branch_examples = {}
    shots = []
    non_null_path_shots = []
    non_null_parent_shots = []
    for shot in source.get("camshots", []):
        name = shot.get("object_string_fields", {}).get("0x14")
        frames = shot.get("keyframes", [])
        pairs = []
        for index in range(max(0, len(frames) - 1)):
            result = classify_pair(frames[index], frames[index + 1])
            result["left_index"] = index
            result["right_index"] = index + 1
            pairs.append(result)
            branch_counts[result["branch"]] += 1
            branch_examples.setdefault(result["branch"], []).append({
                "shot": name,
                "left_index": index,
                "right_index": index + 1,
            })

        path_pointer = shot.get("path_object", "0x0")
        if pointer_is_non_null(path_pointer):
            non_null_path_shots.append(name)
        parent_pointers = [parent_pointer(frame) for frame in frames]
        if any(pointer_is_non_null(pointer) for pointer in parent_pointers):
            non_null_parent_shots.append(name)
        shots.append({
            "name": name,
            "duration": shot.get("duration"),
            "path_pointer": path_pointer,
            "parent_pointers": parent_pointers,
            "keyframe_count": len(frames),
            "pairs": pairs,
        })

    report = {
        "source": str(args.input.resolve()),
        "source_ee_sha256": source.get("ee_sha256"),
        "scope": (
            "Original GH2 saved CamShot objects. SameTargets is classified "
            "by serialized target-entry count and directional membership, "
            "including null slots and without consuming duplicate matches; HasTargets and "
            "parent/path coverage require non-null saved object pointers."
        ),
        "camshot_count": len(shots),
        "keyframe_pair_count": sum(len(shot["pairs"]) for shot in shots),
        "branch_counts": dict(sorted(branch_counts.items())),
        "branch_examples": {
            branch: examples[:8]
            for branch, examples in sorted(branch_examples.items())
        },
        "non_null_path_shots": non_null_path_shots,
        "non_null_parent_shots": non_null_parent_shots,
        "limitations": [
            "A null saved path pointer does not prove that other venues have no paths.",
            "A null saved parent pointer does not cover BuildTransform's parent branch.",
            "This is structural branch coverage, not a time-aligned output-pose comparison.",
        ],
        "shots": shots,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(
        f"Classified {report['camshot_count']} CamShots / "
        f"{report['keyframe_pair_count']} keyframe pairs: "
        + ", ".join(f"{key}={value}"
                    for key, value in report["branch_counts"].items())
    )


if __name__ == "__main__":
    main()
