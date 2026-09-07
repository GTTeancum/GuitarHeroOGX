"""Compare live retail Crowd data saved by PCSX2 to native region membership.

No emulator, host input, desktop capture or modification of the save/asset.
The source layout is GH1 USA; validate the constructor vtable and list bounds.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

from read_pcsx2_savestate import read_member


class Memory:
    def __init__(self, data):
        self.data = data

    def unpack(self, form, address):
        size = struct.calcsize(form)
        if not 0x100000 <= address <= len(self.data) - size:
            raise ValueError(f"Invalid EE address {address:#x}")
        return struct.unpack_from(form, self.data, address)

    def u(self, address):
        return self.unpack("<I", address)[0]

    def string(self, address):
        return self.data[address:address+80].split(b"\0")[0].decode("ascii")

    def nodes(self, head):
        result = []
        node = self.u(head)
        while node != head:
            if node in result or len(result) >= 4096:
                raise ValueError("Invalid/cyclic list")
            result.append(node)
            node = self.u(node)
        return result

    def vector(self, address):
        start, end = self.unpack("<2I", address)
        if end < start or (end-start) % 4 or end-start > 16384:
            raise ValueError("Invalid pointer vector")
        return list(self.unpack(f"<{(end-start)//4}I", start)) if end > start else []

    def position(self, node):
        return list(self.unpack("<3f", node + 0x40))


def snapshot(data):
    mem = Memory(data)
    # Crowd constructor 0x170684..0x170694 stores this vtable at object+4.
    needle = struct.pack("<I", 0x2F8CD0)
    candidates = []
    offset = data.find(needle, 0x100000)
    while offset >= 0:
        if offset % 4 == 0:
            candidates.append(offset-4)
        offset = data.find(needle, offset+4)
    if len(candidates) != 1:
        raise ValueError(f"Expected one live Crowd; found {len(candidates)}")
    crowd = candidates[0]
    start, end = mem.unpack("<2I", crowd+8)
    if not 0 < end-start <= 28*64 or (end-start) % 28:
        raise ValueError("Invalid archetype vector")
    names, archetypes, instances, active = {}, [], [], []
    for address in range(start, end, 28):
        multi = mem.u(address+4)
        if mem.u(multi+0x3C) != 0x2F9B80:
            raise ValueError("Unexpected RndMultiMesh vtable")
        name = mem.string(mem.u(multi+0x50))
        names[address] = name
        # Active MultiMesh list plus SetSizes-held and SetRegion-held nodes.
        lists = [mem.nodes(mem.u(p)) for p in (multi+0x4C, address+0x10, address+0x18)]
        nodes = [node for row in lists for node in row]
        count = mem.u(address+8)
        if len(nodes) != count or len(set(nodes)) != count:
            raise ValueError(f"Population conservation failed: {name}")
        archetypes.append(dict(name=name, count=count, live_lists=list(map(len, lists))))
        instances.extend(dict(mesh=name, position=mem.position(node)) for node in nodes)
        active.extend(dict(mesh=name, position=mem.position(node)) for node in lists[0])
    regions = []
    selected = mem.u(crowd+0x24)
    for index, node in enumerate(mem.nodes(mem.u(crowd+0x20))):
        region = node+0x10  # list payload alignment, confirmed SetRegion's +0x24 pointer
        members, owners = mem.vector(region), mem.vector(region+0x10)
        if len(members) != len(owners):
            raise ValueError("Region parallel arrays differ")
        regions.append(dict(index=index, selected=region == selected,
                            center=list(mem.unpack("<3f", region+0x20)),
                            radius=mem.unpack("<f", region+0x30)[0],
                            plane_z=mem.unpack("<f", region+0x40)[0],
                            members=[dict(mesh=names[owner], position=mem.position(member))
                                     for member, owner in zip(members, owners)]))
    return dict(crowd_address=hex(crowd), archetypes=archetypes, instances=instances, regions=regions,
                active=active, sizes=list(mem.unpack("<2f", crowd+0x80)))


def matching(a, b):
    points = a["position"] + b["position"]
    return (len(points) == 6 and all(map(math.isfinite, points)) and
            a["mesh"] == b["mesh"] and
            max(abs(x-y) for x, y in zip(a["position"], b["position"])) < 1e-4)


def membership(a, b):
    remaining = list(b)
    for item in a:
        found = next((i for i, other in enumerate(remaining) if matching(item, other)), None)
        if found is None:
            return False
        remaining.pop(found)
    return not remaining


def compare(source, native):
    results = []
    for a, b in zip(source["regions"], native["regions"]):
        av = a["center"]+[a["radius"], a["plane_z"]]
        bv = b["center"]+[b["radius"], b["plane_z"]]
        error = max(abs(x-y) for x, y in zip(av, bv))
        results.append(dict(index=a["index"], retail_count=len(a["members"]),
                            native_count=len(b["members"]),
                            index_equal=a["index"] == b["index"],
                            membership_equal=membership(a["members"], b["members"]),
                            order_equal=len(a["members"]) == len(b["members"]) and all(
                                matching(x, y) for x, y in zip(a["members"], b["members"])),
                            bounds_finite=len(av) == len(bv) == 5 and all(map(math.isfinite, av+bv)),
                            bounds_max_error=error))
    report = dict(regions=results,
                  all_instances_match=bool(source["instances"]) and membership(source["instances"], native["instances"]),
                  region_count_equal=bool(source["regions"]) and len(source["regions"]) == len(native["regions"]))
    report["passed"] = report["region_count_equal"] and report["all_instances_match"] and all(
        row["index_equal"] and row["membership_equal"] and row["order_equal"] and
        row["bounds_finite"] and row["bounds_max_error"] < 1e-3 for row in results)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--milo", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--live-state", action="store_true",
                        help="Also compare the current visible flat population; separate from initialization membership")
    args = parser.parse_args()
    memory = read_member(args.state, "eeMemory.bin")
    source = snapshot(memory)
    command = [str(args.inspector.resolve()), str(args.milo.resolve())]
    if args.live_state:
        selected = [row["index"] for row in source["regions"] if row["selected"]]
        if len(selected) != 1:
            raise ValueError("Need one selected retail region for live-state comparison")
        command += [str(selected[0]), *map(str, source["sizes"])]
    run = subprocess.run(command,
                         capture_output=True, text=True, check=True,
                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    native = dict(instances=[], regions=[])
    for line in run.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "INSTANCE":
            native["instances"].append(dict(mesh=fields[1], position=list(map(float, fields[2:5]))))
        elif fields[0] == "REGION":
            native["regions"].append(dict(index=int(fields[1]), members=[],
                center=list(map(float, fields[3:6])), radius=float(fields[6]), plane_z=float(fields[7])))
        elif fields[0] == "MEMBER":
            native["regions"][int(fields[1])]["members"].append(
                dict(mesh=fields[2], position=list(map(float, fields[3:6]))))
        elif fields[0] == "ACTIVE":
            native.setdefault("active", []).append(dict(mesh=fields[1], position=list(map(float, fields[2:5]))))
        elif fields[0] == "LIVE":
            native.update(active=[], live_flat_count=int(fields[1]), live_promoted_count=int(fields[2]))

    report = dict(source=str(args.state.resolve()), asset=str(args.milo.resolve()),
                  ee_sha256=hashlib.sha256(memory).hexdigest(),
                  milo_sha256=hashlib.sha256(args.milo.read_bytes()).hexdigest(),
                  inspector_sha256=hashlib.sha256(args.inspector.read_bytes()).hexdigest(),
                  source_snapshot=source, native=native, **compare(source, native))
    if args.live_state:
        report["live_membership_equal"] = ("active" in native and membership(source["active"], native["active"]))
        report["passed"] = report["passed"] and report["live_membership_equal"]
        print(f"Live flat membership matches: {report['live_membership_equal']} "
              f"retail={len(source['active'])} native={native.get('live_flat_count')}")
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    print(f"All original instance origins match: {report['all_instances_match']}")
    for row in report["regions"]:
        print(row)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
