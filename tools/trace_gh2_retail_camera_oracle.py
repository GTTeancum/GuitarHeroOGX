#!/usr/bin/env python3
"""Arm one contained GH2 input edge and trace retail CameraManager at cadence.

The optional input is delivered through the prepatched guest JoypadPoll hook.
This tool never activates PCSX2 and never emits host keyboard, mouse, or
controller input.
"""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path

from drive_pcsx2_joypad_pine import (
    BUTTON_BITS,
    DEFAULT_HOOK_REQUEST,
    GH2_TITLE,
    write_many_u32,
)
from trace_pcsx2_readonly import PineClient, ReadSpec


# Retail WorldDir camera fields recovered from WorldDir::Handle at 0x0026fb90.
# The `current_shot` handler reads WorldDir+0x304; the surrounding live layout
# is next CamShot*, current CamShot*, mCamStartTime, and mFreeCam.
CAMERA_MANAGER_NEXT = 0x00B783F8
CAMERA_MANAGER_CURRENT = 0x00B78404
CAMERA_MANAGER_START = 0x00B78408
CAMERA_MANAGER_FREECAM = 0x00B7840C
CAMERA_OBJECT = 0x00B92EF0
VALID_EE_START = 0x00080000
VALID_EE_END = 0x02000000
MAX_NAME_BYTES = 64


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def read_many(client: PineClient, specs: list[ReadSpec]) -> dict[str, object]:
    _, values = client.read_many(specs)
    return {spec.label: value for spec, value in zip(specs, values)}


def is_ee_pointer(value: int) -> bool:
    return VALID_EE_START <= value < VALID_EE_END


def read_c_string(client: PineClient, address: int) -> tuple[int, str | None]:
    if not is_ee_pointer(address):
        return 0, None
    specs = [
        ReadSpec(address + offset, "u8", f"name_{offset}")
        for offset in range(MAX_NAME_BYTES)
    ]
    status, values = client.read_many(specs)
    raw = bytes(int(value) for value in values)
    return status, raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--button", choices=sorted(BUTTON_BITS), default="cross")
    parser.add_argument(
        "--no-input",
        action="store_true",
        help="Trace read-only without arming the process-local Joypad hook",
    )
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--lead", type=float, default=0.5)
    parser.add_argument("--interval-ms", type=float, default=1000.0 / 60.0)
    parser.add_argument("--hook-request", type=lambda value: int(value, 0), default=DEFAULT_HOOK_REQUEST)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=28011)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.duration <= 0 or args.lead < 0 or args.interval_ms <= 0:
        parser.error("duration and interval must be positive; lead must be nonnegative")

    fixed_specs = [
        ReadSpec(CAMERA_MANAGER_NEXT, "u32", "next_shot"),
        ReadSpec(CAMERA_MANAGER_CURRENT, "u32", "current_shot"),
        ReadSpec(CAMERA_MANAGER_START, "f32", "shot_start_time"),
        ReadSpec(CAMERA_MANAGER_FREECAM, "u32", "free_camera"),
        # RndTransformable stores padded local Transform rows at +0x20 and
        # cached world Transform rows at +0x60.  Read all twelve meaningful
        # floats from both spaces: comparing only the local basis silently
        # omitted translation and could not detect parent/world composition.
        ReadSpec(CAMERA_OBJECT + 0x20, "f32", "camera_local_r0_x"),
        ReadSpec(CAMERA_OBJECT + 0x24, "f32", "camera_local_r0_y"),
        ReadSpec(CAMERA_OBJECT + 0x28, "f32", "camera_local_r0_z"),
        ReadSpec(CAMERA_OBJECT + 0x30, "f32", "camera_local_r1_x"),
        ReadSpec(CAMERA_OBJECT + 0x34, "f32", "camera_local_r1_y"),
        ReadSpec(CAMERA_OBJECT + 0x38, "f32", "camera_local_r1_z"),
        ReadSpec(CAMERA_OBJECT + 0x40, "f32", "camera_local_r2_x"),
        ReadSpec(CAMERA_OBJECT + 0x44, "f32", "camera_local_r2_y"),
        ReadSpec(CAMERA_OBJECT + 0x48, "f32", "camera_local_r2_z"),
        ReadSpec(CAMERA_OBJECT + 0x50, "f32", "camera_local_v_x"),
        ReadSpec(CAMERA_OBJECT + 0x54, "f32", "camera_local_v_y"),
        ReadSpec(CAMERA_OBJECT + 0x58, "f32", "camera_local_v_z"),
        ReadSpec(CAMERA_OBJECT + 0x60, "f32", "camera_world_r0_x"),
        ReadSpec(CAMERA_OBJECT + 0x64, "f32", "camera_world_r0_y"),
        ReadSpec(CAMERA_OBJECT + 0x68, "f32", "camera_world_r0_z"),
        ReadSpec(CAMERA_OBJECT + 0x70, "f32", "camera_world_r1_x"),
        ReadSpec(CAMERA_OBJECT + 0x74, "f32", "camera_world_r1_y"),
        ReadSpec(CAMERA_OBJECT + 0x78, "f32", "camera_world_r1_z"),
        ReadSpec(CAMERA_OBJECT + 0x80, "f32", "camera_world_r2_x"),
        ReadSpec(CAMERA_OBJECT + 0x84, "f32", "camera_world_r2_y"),
        ReadSpec(CAMERA_OBJECT + 0x88, "f32", "camera_world_r2_z"),
        ReadSpec(CAMERA_OBJECT + 0x90, "f32", "camera_world_v_x"),
        ReadSpec(CAMERA_OBJECT + 0x94, "f32", "camera_world_v_y"),
        ReadSpec(CAMERA_OBJECT + 0x98, "f32", "camera_world_v_z"),
        ReadSpec(CAMERA_OBJECT + 0x2C0, "f32", "camera_near"),
        ReadSpec(CAMERA_OBJECT + 0x2C4, "f32", "camera_far"),
        ReadSpec(CAMERA_OBJECT + 0x2C8, "f32", "camera_fov"),
    ]
    mask = 0 if args.no_input else 1 << BUTTON_BITS[args.button]
    samples: list[dict[str, object]] = []
    statuses: set[int] = set()
    interval = args.interval_ms / 1000.0

    with PineClient(args.host, args.port, args.timeout) as client:
        status, title = client.title()
        statuses.add(status)
        if title != GH2_TITLE:
            raise RuntimeError(f"refusing to trace/input title {title!r}")

        started = time.perf_counter()
        deadline = started + args.duration
        armed = False
        consumed = False
        index = 0
        while True:
            now = time.perf_counter()
            if now >= deadline:
                break
            if not args.no_input and not armed and now - started >= args.lead:
                statuses.add(
                    write_many_u32(
                        client,
                        [(args.hook_request + 4, mask), (args.hook_request, 1)],
                    )
                )
                armed = True

            values = read_many(client, fixed_specs)
            current = int(values["current_shot"])
            if is_ee_pointer(current):
                dynamic_specs = [
                    ReadSpec(current, "u32", "current_shot_hmx_base"),
                    ReadSpec(current + 4, "f32", "current_shot_frame"),
                    ReadSpec(current + 8, "u32", "current_shot_rate"),
                ]
                dynamic_status, dynamic_values = client.read_many(dynamic_specs)
                statuses.add(dynamic_status)
                values.update(
                    {
                        spec.label: value
                        for spec, value in zip(dynamic_specs, dynamic_values)
                    }
                )
                hmx_base = int(values["current_shot_hmx_base"])
                if is_ee_pointer(hmx_base):
                    name_status, name_pointer = client.read(
                        ReadSpec(hmx_base + 0x14, "u32", "current_shot_name_pointer")
                    )
                    statuses.add(name_status)
                    values["current_shot_name_pointer"] = int(name_pointer)
                    string_status, shot_name = read_c_string(client, int(name_pointer))
                    statuses.add(string_status)
                    values["current_shot_name"] = shot_name
                else:
                    values["current_shot_name_pointer"] = None
                    values["current_shot_name"] = None
            else:
                values["current_shot_hmx_base"] = None
                values["current_shot_frame"] = None
                values["current_shot_rate"] = None
                values["current_shot_name_pointer"] = None
                values["current_shot_name"] = None
            request_status, request = client.read(
                ReadSpec(args.hook_request, "u32", "hook_request")
            )
            statuses.add(request_status)
            if armed and int(request) == 0:
                consumed = True
            samples.append(
                {
                    "sample": index,
                    "seconds": now - started,
                    "timestamp_utc": utc_timestamp(),
                    **values,
                    "hook_request": int(request),
                }
            )
            index += 1
            target = started + index * interval
            delay = target - time.perf_counter()
            if delay > 0:
                time.sleep(delay)

    payload = {
        "schema": 1,
        "operation": "GH2 retail process-local camera oracle",
        "host_input": False,
        "title": title,
        "button": None if args.no_input else args.button,
        "mask": f"0x{mask:08x}",
        "hook_request_address": f"0x{args.hook_request:08x}",
        "hook_armed": armed,
        "hook_consumed": consumed,
        "requested_duration_seconds": args.duration,
        "lead_seconds": args.lead,
        "requested_interval_ms": args.interval_ms,
        "actual_duration_seconds": samples[-1]["seconds"] if samples else 0.0,
        "samples": samples,
        "pine_statuses": [f"0x{status:02x}" for status in sorted(statuses)],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                key: payload[key]
                for key in [
                    "operation",
                    "host_input",
                    "title",
                    "button",
                    "hook_armed",
                    "hook_consumed",
                    "actual_duration_seconds",
                    "pine_statuses",
                ]
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
