"""Check a narrowly gated retail saved camera against the recovered pose math.

This is a static retail oracle, not a live native-game or all-venue parity test.
Only two-key, linear, unparented, unresolved-target, no-path, zero-angular-shake
snapshots are supported. No fitted parameters or captured camera basis are used
to construct the prediction.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation


def ps2_float(value):
    """Finite scalar arithmetic rounded toward zero, not host round-to-nearest."""
    nearest = np.float32(value)
    if abs(float(nearest)) > abs(value):
        nearest = np.nextafter(nearest, np.float32(0))
    return float(nearest)


def source_sine(angle, table):
    # 2DC500 range reduction, 2DC4B8 slope/value lookup. Explicitly reproduce
    # the EE truncating scalar operations; do not fit to captured camera rows.
    half_pi = float(np.float32(math.pi/2))
    quadrant = math.trunc(ps2_float(angle*float(np.float32(2/math.pi))))
    remainder = ps2_float(angle-ps2_float(quadrant*half_pi))
    if remainder < 0:
        remainder = ps2_float(remainder+half_pi)
        quadrant -= 1
    coordinate = ps2_float(remainder*table["scale"])
    if quadrant & 1:
        coordinate = ps2_float(table["intervals"]-coordinate)
    index = math.trunc(coordinate)
    fraction = ps2_float(coordinate-index)
    value = ps2_float(table["values"][index]+ps2_float(fraction*table["slopes"][index]))
    return -value if quadrant & 2 else value


def positive_trace_quat(m):
    # 2DA348..2DA3EC, positive trace branch. Unlike scipy's matrix importer,
    # retail does NOT orthogonalize the input or normalize this quaternion.
    trace = float(np.trace(m))
    if trace <= 0:
        raise ValueError("This static oracle currently supports positive-trace matrices only")
    root = math.sqrt(trace+1)
    scale = 0.5/root
    return np.array([(m[1,2]-m[2,1])*scale, (m[2,0]-m[0,2])*scale,
                     (m[0,1]-m[1,0])*scale, root*0.5])


def audit(data):
    reports = []
    for camera in data["active_cameras"]:
        shot = next(s for s in data["camshots"] if s["address"] == camera["current_shot"])
        keys = shot["keyframes"]
        reasons = []
        if len(keys) != 2:
            reasons.append("requires exactly two keys")
        if shot["path_object"] != "0x0":
            reasons.append("referenced path requires separate evaluation")
        for key in keys:
            if key["parent"]["target"] != "0x0" or any(t["target"] != "0x0" for t in key["targets"]):
                reasons.append("resolved parent/target requires separate evaluation")
            if key["raw_scalars"]["0x8"] != 0:
                reasons.append("nonlinear frame blend")
        if any(shot["shake_state"]["0xb0"]):
            reasons.append("angular shake requires separate evaluation")
        if reasons:
            reports.append(dict(shot=shot["address"], supported=False, reasons=reasons))
            continue
        a, b = keys
        # Saved frame layout: hold +0, blend +4, ease +8, start frame +C.
        duration = a["raw_scalars"]["0x4"]
        start = a["raw_scalars"]["0xc"] + a["raw_scalars"]["0x0"]
        if duration <= 0 or not start <= shot["frame"] <= start+duration:
            reports.append(dict(shot=shot["address"], supported=False, reasons=["outside blend interval"]))
            continue
        t = (shot["frame"] - start)/duration
        ma, mb = np.array(a["authored_transform_rows"]), np.array(b["authored_transform_rows"])
        # Row-vector source matrices -> column-vector quaternion convention.
        qa, qb = positive_trace_quat(ma[:3]), positive_trace_quat(mb[:3])
        if np.dot(qa, qb) < 0:
            qb = -qb
        q = qa + (qb-qa)*t
        q /= np.linalg.norm(q)
        basis = Rotation.from_quat(q).as_matrix().T
        position = ma[3] + (mb[3]-ma[3])*t
        position += np.array(shot["shake_state"]["0xa0"]) @ basis
        # Even zero angular shake goes through MakeRotMatrix (266D1C).
        # Its diagonal contains two source cos(0) products, not literal ones.
        cosine_zero = source_sine(float(np.float32(math.pi/2)), data["sine_table"])
        source_zero_rotation_scale = ps2_float(cosine_zero*cosine_zero)
        basis *= source_zero_rotation_scale
        fov = a["raw_scalars"]["0x10"]*(1-t) + b["raw_scalars"]["0x10"]*t
        actual = np.array(camera["local_rows"])
        errors = dict(position=float(np.max(np.abs(actual[3]-position))),
                      basis=float(np.max(np.abs(actual[:3]-basis))), fov=abs(camera["fov"]-fov))
        tolerances = dict(position=0.0001, basis=0.00001, fov=0.000001)
        reports.append(dict(shot=shot["address"], name=shot["object_string_fields"].get("0x14"),
            supported=True, frame=shot["frame"], blend=t, predicted_position=position.tolist(),
            source_cosine_zero=cosine_zero, source_zero_rotation_scale=source_zero_rotation_scale,
            predicted_basis=basis.tolist(), predicted_fov=fov,
            errors=errors, tolerances=tolerances,
            passed=all(errors[k] <= tolerances[k] for k in errors)))
    return dict(scope="Static retail pose reconstruction only; no native runtime or global RNG parity claimed",
                ee_sha256=data["ee_sha256"], reports=reports,
                passed=bool(reports) and all(r.get("passed", False) for r in reports))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit(json.loads(args.snapshot.read_text(encoding="utf-8")))
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    for row in result["reports"]:
        print(row.get("name", row["shot"]), "PASS" if row.get("passed") else "FAIL/UNSUPPORTED", row.get("errors", row.get("reasons")))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
