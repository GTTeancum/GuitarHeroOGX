"""Read GH2 USA CamShots from an existing PCSX2 native save, without UI/input."""
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
    parser.add_argument("--references-to", help="Retain bounded pointer references to this shot name")
    args = parser.parse_args()
    memory = read_member(args.state, "eeMemory.bin")
    u32 = lambda p: struct.unpack_from("<I", memory, p)[0]
    f32 = lambda p: struct.unpack_from("<f", memory, p)[0]
    def string(p):
        if not 0 < p < len(memory) - 256:
            return None
        end = memory.find(b"\0", p, p + 256)
        if end <= p:
            return None
        text = memory[p:end]
        return text.decode("ascii") if all(32 <= c < 127 for c in text) else None
    def rows(p):
        return [list(struct.unpack_from("<3f", memory, p+i*16)) for i in range(4)]
    def reference(p):
        # 261C28/261C34: ObjPtr target plus optional directory-relative symbol.
        target = u32(p+8)
        name = None
        if 0x100000 <= target < len(memory)-0x150:
            obj = u32(target)
            if 0x100000 <= obj < len(memory)-32:
                name = string(u32(obj+0x14))
        return dict(target=hex(target), target_name=name, subpart=string(u32(p+12)))
    def frames(shot):
        begin, end = u32(shot+0x10), u32(shot+0x14)
        # Record stride corroborated by container arithmetic and adjacent saved
        # records. Preserve raw scalars rather than guessing unnamed fields.
        if not (0x100000 <= begin <= end < len(memory) and
                (end-begin) % 0x110 == 0 and (end-begin)//0x110 <= 128):
            return []
        result = []
        for p in range(begin, end, 0x110):
            first, last = u32(p+0x84), u32(p+0x88)
            targets = []
            # 266E80..266EB4: vector at +84, ObjPtr stride 16.
            if 0x100000 <= first <= last < len(memory) and (last-first) % 16 == 0 and last-first <= 1024:
                targets = [reference(q) for q in range(first, last, 16)]
            result.append(dict(address=hex(p),
                raw_scalars={hex(i):f32(p+i) for i in range(0,0x20,4)},
                authored_transform_rows=rows(p+0x20),
                raw_shake_fields={hex(i):f32(p+i) for i in range(0x68,0x78,4)},
                target_cache=list(struct.unpack_from("<3f", memory, p+0xA0)),
                targets=targets, parent=reference(p+0xB0),
                parent_cache_rows=rows(p+0xC0)))
        return result
    candidates = []
    # Constructor262064/26206C installs this RndAnimatable vtable at shot+0C.
    pattern = struct.pack("<I", 0x3F0C08)
    pos = memory.find(pattern)
    while pos >= 0:
        shot = pos - 12
        pos = memory.find(pattern, pos + 4)
        if shot < 0x100000 or shot + 0x150 >= len(memory) or shot % 4:
            continue
        obj = u32(shot)
        if not 0x100000 <= obj < len(memory) - 64:
            continue
        category = string(u32(shot + 0x40))
        if category is None:
            continue
        strings = {hex(offset): string(u32(obj + offset)) for offset in range(4, 32, 4)}
        candidates.append(dict(address=hex(shot), object=hex(obj), category=category,
            object_string_fields={k:v for k,v in strings.items() if v},
            frame=f32(shot + 4), rate=u32(shot + 8), duration=f32(shot + 0x104),
            enabled=u32(shot + 0x108), raw_phase_flag_10c=u32(shot + 0x10C),
            used=u32(shot + 0x110), weight=f32(shot + 0x44),
            fade_time=f32(shot + 0x3C), path_ease=f32(shot + 0x54),
            path_object=hex(u32(shot + 0x50)), directory=hex(u32(obj + 0x18)),
            keyframes=frames(shot),
            # 262F38..263408: persistent per-CamShot shake state. +A0/+B0
            # are the emitted translation/angular offsets after integration.
            shake_state={hex(i):list(struct.unpack_from("<3f", memory, shot+i))
                         for i in range(0xA0,0x100,0x10)},
            frame_container_words=[hex(u32(shot + offset)) for offset in range(0x10,0x24,4)]))
    references = []
    directory_pointer_objects = []
    active_cameras = []
    for directory in sorted({int(c["directory"], 16) for c in candidates}):
        if not 0x100000 <= directory < len(memory) - 0x500:
            continue
        for offset in range(0, 0x500, 4):
            target = u32(directory + offset)
            if not 0x100000 <= target < len(memory) - 32 or target % 4:
                continue
            obj = u32(target)
            if not 0x100000 <= obj < len(memory) - 32 or obj % 4:
                continue
            if not 0x100000 <= u32(obj) < 0x600000:
                continue
            name = string(u32(obj + 0x14))
            if name:
                directory_pointer_objects.append(dict(directory=hex(directory),
                    offset=hex(offset), target=hex(target), object=hex(obj), name=name))
                if offset == 0x21C:
                    # SetPos262B80 loads WorldDir::mCam at +21C. Projection
                    # builder1B1FB4..1B1FC8 reads near/far/FOV at +2C0/+2C4/+2C8.
                    active_cameras.append(dict(directory=hex(directory), address=hex(target), name=name,
                        current_shot=hex(u32(directory + 0x304)),
                        near=f32(target + 0x2C0), far=f32(target + 0x2C4), fov=f32(target + 0x2C8),
                        local_rows=[list(struct.unpack_from("<3f", memory, target+0x20+i*16)) for i in range(4)],
                        world_rows=[list(struct.unpack_from("<3f", memory, target+0x60+i*16)) for i in range(4)],
                        projection_raw_rows=[list(struct.unpack_from("<4f", memory, target+0x100+i*16)) for i in range(4)],
                        inverse_projection_raw_rows=[list(struct.unpack_from("<4f", memory, target+0x140+i*16)) for i in range(4)],
                        world_projection_raw_rows=[list(struct.unpack_from("<4f", memory, target+0x180+i*16)) for i in range(4)],
                        inverse_world_projection_raw_rows=[list(struct.unpack_from("<4f", memory, target+0x1C0+i*16)) for i in range(4)]))
    if args.references_to:
        for candidate in candidates:
            if candidate["object_string_fields"].get("0x14") != args.references_to:
                continue
            needle = struct.pack("<I", int(candidate["address"], 16))
            p = memory.find(needle)
            while p >= 0 and len(references) < 64:
                if p % 4 == 0 and 32 <= p < len(memory) - 36:
                    references.append(dict(address=hex(p),
                        words={hex(a):hex(u32(a)) for a in range(p-32,p+36,4)}))
                p = memory.find(needle, p+1)
    table_count = u32(0x3DE9B8)
    table_base, table_slope = u32(0x3DE9BC), u32(0x3DE9C0)
    sine_table = None
    if table_count == 64 and all(0x100000 <= p < len(memory)-260 for p in (table_base, table_slope)):
        sine_table = dict(intervals=table_count, scale=f32(0x5239F8),
            values=[f32(table_base+i*4) for i in range(table_count+1)],
            slopes=[f32(table_slope+i*4) for i in range(table_count+1)])
    args.output.write_text(json.dumps(dict(source=str(args.state.resolve()),
        scope="Original GH2 saved EE memory; constructor-identified CamShot objects, not execution or matched runtime parity",
        ee_sha256=hashlib.sha256(memory).hexdigest(), camshots=candidates,
        directory_pointer_objects=directory_pointer_objects,
        active_cameras=active_cameras,
        sine_table=sine_table,
        references_to=args.references_to, references=references), indent=2), encoding="utf-8")
    print(f"Retained {len(candidates)} constructor-identified GH2 CamShots")


if __name__ == "__main__":
    main()
