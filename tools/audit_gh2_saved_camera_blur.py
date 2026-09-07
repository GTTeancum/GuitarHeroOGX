"""Read the original GH2 WorldDir blur state from a native PCSX2 save.

No emulator launch or input. Offsets are from SLUS_214.47 constructor26Dxxx,
StartBlur26F660, DrawBlur26F7E0 and property accessors2701CC/270268.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from read_pcsx2_savestate import read_member


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    memory = read_member(args.state, "eeMemory.bin")
    u32 = lambda p: struct.unpack_from("<I", memory, p)[0]
    f32 = lambda p: struct.unpack_from("<f", memory, p)[0]
    world = u32(0x3DE5E8)  # Constructor26DC44 sets the current WorldDir.
    if not 0x100000 <= world < len(memory) - 0x400:
        raise ValueError("No loaded GH2 WorldDir in this save")
    material, texture = u32(world + 0x27C), u32(world + 0x280)
    if not all(0x100000 <= p < len(memory) - 0x200 for p in (material, texture)):
        raise ValueError("WorldDir blur resources are not resident")
    vtable = u32(texture)
    report = dict(
        source=str(args.state.resolve()), ee_sha256=hashlib.sha256(memory).hexdigest(),
        scope="Original saved values, not a running render/capture parity claim",
        world=hex(world), task_units=u32(world + 0x25C),
        end_time=f32(world + 0x260), grow=f32(world + 0x264),
        fade_duration=f32(world + 0x268),
        rect=[f32(world + offset) for offset in (0x26C, 0x270, 0x274, 0x278)],
        base_alpha=f32(world + 0x284), current_alpha=f32(world + 0x288),
        material=dict(address=hex(material), blend=u32(material + 0x2C)),
        texture=dict(address=hex(texture), type=u32(texture + 0x48),
                     width=u32(texture + 0x4C), height=u32(texture + 0x50),
                     bpp=u32(texture + 0x54), vtable=hex(vtable),
                     virtual_words={hex(o): hex(u32(vtable + o))
                                    for o in range(0x80, 0xC0, 4)}),
    )
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"WorldDir {world:#x}: grow={report['grow']} alpha={report['base_alpha']} "
          f"current={report['current_alpha']} texture_type={report['texture']['type']:#x}")


if __name__ == "__main__":
    main()
