#!/usr/bin/env python3
"""Tap one GH2 retail joypad button through PCSX2 guest memory.

This is a process-local oracle helper.  It never activates the PCSX2 window
and never sends host keyboard, mouse, or controller input.  The retail GH2
JoypadPoll layout was recovered from SLUS-21447: port-zero JoypadData begins at
0x00497358 and stores current/new/released button masks at +0/+4/+8.
"""

from __future__ import annotations

import argparse
import json
import struct
import time
from datetime import datetime, timezone
from pathlib import Path

from trace_pcsx2_readonly import PineClient, ReadSpec


GH2_TITLE = "Guitar Hero II"
JOY0_BUTTONS = 0x00497358
JOY0_NEW_PRESSED = JOY0_BUTTONS + 4
JOY0_NEW_RELEASED = JOY0_BUTTONS + 8
WRITE32_OPCODE = 0x06
DEFAULT_HOOK_REQUEST = 0x01FF1000
DEFAULT_HOOK_MASK = DEFAULT_HOOK_REQUEST + 4

BUTTON_BITS = {
    "l2": 0,
    "r2": 1,
    "l1": 2,
    "r1": 3,
    "triangle": 4,
    "circle": 5,
    "cross": 6,
    "square": 7,
    "select": 8,
    "start": 11,
    "up": 12,
    "right": 13,
    "down": 14,
    "left": 15,
}


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def write_many_u32(client: PineClient, values: list[tuple[int, int]]) -> int:
    command = b"".join(
        bytes([WRITE32_OPCODE]) + struct.pack("<II", address, value & 0xFFFFFFFF)
        for address, value in values
    )
    status, payload = client._request(command)
    if payload:
        raise RuntimeError(f"unexpected PINE write payload ({len(payload)} bytes)")
    return status


def read_pad_words(client: PineClient) -> dict[str, int]:
    specs = [
        ReadSpec(JOY0_BUTTONS, "u32", "buttons"),
        ReadSpec(JOY0_NEW_PRESSED, "u32", "new_pressed"),
        ReadSpec(JOY0_NEW_RELEASED, "u32", "new_released"),
    ]
    _, values = client.read_many(specs)
    return {spec.label: int(value) for spec, value in zip(specs, values)}


def tap_button(
    client: PineClient,
    mask: int,
    hold_ms: int,
    release_ms: int,
    write_interval_ms: int,
) -> dict[str, object]:
    before = read_pad_words(client)
    statuses: set[int] = set()
    press_writes = 0
    release_writes = 0
    interval = write_interval_ms / 1000.0
    try:
        press_end = time.perf_counter() + hold_ms / 1000.0
        while time.perf_counter() < press_end:
            statuses.add(
                write_many_u32(
                    client,
                    [
                        (JOY0_BUTTONS, mask),
                        (JOY0_NEW_PRESSED, mask),
                        (JOY0_NEW_RELEASED, 0),
                    ],
                )
            )
            press_writes += 1
            time.sleep(interval)

        release_end = time.perf_counter() + release_ms / 1000.0
        while time.perf_counter() < release_end:
            statuses.add(
                write_many_u32(
                    client,
                    [
                        (JOY0_BUTTONS, 0),
                        (JOY0_NEW_PRESSED, 0),
                        (JOY0_NEW_RELEASED, mask),
                    ],
                )
            )
            release_writes += 1
            time.sleep(interval)
    finally:
        statuses.add(
            write_many_u32(
                client,
                [
                    (JOY0_BUTTONS, before["buttons"]),
                    (JOY0_NEW_PRESSED, 0),
                    (JOY0_NEW_RELEASED, 0),
                ],
            )
        )
    return {
        "before": before,
        "after": read_pad_words(client),
        "press_writes": press_writes,
        "release_writes": release_writes,
        "pine_statuses": [f"0x{status:02x}" for status in sorted(statuses)],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("button", choices=sorted(BUTTON_BITS))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=28011)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--hold-ms", type=int, default=120)
    parser.add_argument("--release-ms", type=int, default=80)
    parser.add_argument("--write-interval-ms", type=int, default=2)
    parser.add_argument(
        "--hook-request",
        type=lambda text: int(text, 0),
        help="arm a prepatched in-JoypadPoll one-shot at this guest address",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 16 <= args.hold_ms <= 1000:
        parser.error("--hold-ms must be between 16 and 1000")
    if not 16 <= args.release_ms <= 1000:
        parser.error("--release-ms must be between 16 and 1000")
    if not 1 <= args.write_interval_ms <= 16:
        parser.error("--write-interval-ms must be between 1 and 16")

    mask = 1 << BUTTON_BITS[args.button]
    with PineClient(args.host, args.port, args.timeout) as client:
        _, title = client.title()
        if title != GH2_TITLE:
            raise RuntimeError(f"refusing to write guest input for title {title!r}")
        if args.hook_request is not None:
            hook_before = []
            for address, label in [
                (args.hook_request, "request"),
                (args.hook_request + 4, "mask"),
            ]:
                _, value = client.read(ReadSpec(address, "u32", label))
                hook_before.append(int(value))
            statuses = [
                write_many_u32(
                    client,
                    [(args.hook_request + 4, mask), (args.hook_request, 1)],
                )
            ]
            deadline = time.perf_counter() + 2.0
            consumed = False
            while time.perf_counter() < deadline:
                _, request = client.read(ReadSpec(args.hook_request, "u32", "request"))
                if int(request) == 0:
                    consumed = True
                    break
                time.sleep(0.002)
            if not consumed:
                write_many_u32(client, [(args.hook_request, 0)])
                raise RuntimeError("guest JoypadPoll hook did not consume the request")
            tap_result = {
                "hook_before": hook_before,
                "hook_consumed": True,
                "pine_statuses": [f"0x{status:02x}" for status in sorted(set(statuses))],
            }
        else:
            tap_result = tap_button(
                client,
                mask,
                args.hold_ms,
                args.release_ms,
                args.write_interval_ms,
            )
        result = {
            "timestamp_utc": utc_timestamp(),
            "operation": "process-local GH2 JoypadData tap",
            "host_input": False,
            "title": title,
            "button": args.button,
            "mask": f"0x{mask:08x}",
            "guest_words": {
                "buttons": f"0x{JOY0_BUTTONS:08x}",
                "new_pressed": f"0x{JOY0_NEW_PRESSED:08x}",
                "new_released": f"0x{JOY0_NEW_RELEASED:08x}",
            },
            "delivery": (
                "post-hardware-sample, pre-edge/message JoypadPoll hook"
                if args.hook_request is not None
                else "direct pulse"
            ),
            **tap_result,
        }

    payload = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload + "\n", encoding="utf-8")
    print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
