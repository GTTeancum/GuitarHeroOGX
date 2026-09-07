"""Read original GH1 camera/projection evidence from a native PCSX2 save."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from read_pcsx2_savestate import read_member


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    mem = read_member(args.state, "eeMemory.bin")
    u32 = lambda a: struct.unpack_from("<I", mem, a)[0]
    f32 = lambda a: struct.unpack_from("<f", mem, a)[0]
    controller = u32(0x363824)
    camera = u32(controller + 8)
    xfm = u32(camera + 0x50)
    if not all(0x100000 <= a < len(mem)-1024 for a in (controller, camera, xfm)):
        raise ValueError("Not the expected GH1 USA gameplay camera layout")
    fov = f32(camera + 0x308)
    mxx, mzx = f32(camera + 0x140), f32(camera + 0x164)
    if not 0.01 < fov < 3 or mxx <= 0 or mzx >= 0:
        raise ValueError("Camera is not a valid perspective sample")
    ratio = -mxx/mzx
    vertical = 2*math.atan(math.tan(fov/2)*ratio)
    world = [list(struct.unpack_from("<3f", mem, xfm+0x60+i*16)) for i in range(4)]
    target_xfm = u32(u32(controller+0x14)+0x10)
    target = list(struct.unpack_from("<3f", mem, target_xfm+0x90))
    delta = [target[k]-world[3][k] for k in range(3)]
    axes = [sum(delta[k]*row[k] for k in range(3)) for row in world[:3]]
    projected = [axes[0]*mxx/axes[1], -axes[2]*mzx/axes[1]]
    desired_out = [f32(controller+0x24), f32(controller+0x28)]
    result = dict(source="PCSX2 native GH1 USA savestate; no emulator or desktop operation",
                  state=str(args.state.resolve()), ee_sha256=hashlib.sha256(mem).hexdigest(),
                  controller=hex(controller), camera=hex(camera), transform=hex(xfm),
                  helper_filter=f32(controller+0x2c),
                  helper_player_index=struct.unpack_from("<i", mem, controller+0x74)[0],
                  helper_object=hex(u32(controller+0x18)),
                  helper_filter_config_key="SystemConfig(arena).cam_filter",
                  camera_world=world, target_world=target,
                  target_projected_normalized=projected, authored_singer_out=desired_out,
                  framing_endpoint_error=max(abs(a-b) for a,b in zip(projected, desired_out)),
                  gh1_fov_radians=fov, gh1_fov_degrees=math.degrees(fov),
                  local_project_mxx=mxx, local_project_mzx=mzx, effective_y_ratio=ratio,
                  expected_gh1_mxx=1/math.tan(fov/2),
                  gh2_vertical_fov_radians=vertical,
                  gh2_vertical_fov_degrees=math.degrees(vertical),
                  gh2_projection_after_conversion=[ratio/math.tan(vertical/2), -1/math.tan(vertical/2)],
                  wrong_projection_without_conversion=[ratio/math.tan(fov/2), -1/math.tan(fov/2)])
    result['horizontal_fov_verified'] = abs(mxx-result['expected_gh1_mxx']) < 0.00001
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"GH1 horizontal FOV verified={result['horizontal_fov_verified']}; "
          f"source={math.degrees(fov):.6f} deg -> GH2 vertical={math.degrees(vertical):.6f} deg; "
          f"mxx={mxx:.6f} mzx={mzx:.6f}")
    return 0 if result['horizontal_fov_verified'] else 1


if __name__ == "__main__":
    raise SystemExit(main())
