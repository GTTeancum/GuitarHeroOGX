#!/usr/bin/env python3
"""Decode the authored RB2 character-creator deformation clip.

RB2 Wii stores the male/female body-shape poses in revision-14
``CharClipSamples`` objects.  This module reads that native stream directly so
the character baker can apply the game's height/weight deformation instead of
approximating proportions from character names or hand-authored offsets.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from rb2_native_assets import FormatError, Reader, skip_object_fields


CHANNEL_SUFFIXES = (".pos", ".scale", ".quat", ".rotx", ".roty", ".rotz")
RB2_WII_VECTOR_FIXED_SCALE = 1300.0

# CharDeform's five authored control points.  The order is independently
# established by the retail editor commands in char/char_objects.dta and by
# sample zero reconstructing male_guitar's neutral transform graph exactly.
DEFORM_CONTROL_POINTS = (
    (0.5, 0.5),  # neutral
    (1.0, 1.0),  # tall_and_fat
    (0.0, 1.0),  # short_and_fat
    (1.0, 0.0),  # tall_and_skinny
    (0.0, 0.0),  # short_and_skinny
)


@dataclass(frozen=True)
class DeformChannel:
    name: str
    weight: float


@dataclass
class DeformSamples:
    version: int
    channels: list[DeformChannel]
    counts: list[int]
    compression: int
    sample_count: int
    frames: list[float]
    version14_flag: bool
    values: list[dict[str, tuple[float, ...]]]
    bytes_per_sample: int
    runtime_bytes_per_sample: int


@dataclass
class DeformClip:
    revision: int
    char_clip_revision: int
    start_frame: int
    end_frame: int
    frames_per_second: float
    flags: int
    play_flags: int
    blend_width: int
    range_frames: float
    relative: str
    transitions_byte_hint: int
    events: list[dict[str, Any]]
    full: DeformSamples
    one: DeformSamples


def _channel_kind(name: str) -> int:
    lower = name.casefold()
    for index, suffix in enumerate(CHANNEL_SUFFIXES):
        if lower.endswith(suffix):
            return index
    raise FormatError(f"unknown RB2 deform channel {name!r}")


def _component_size(kind: int, compression: int) -> int:
    if kind < 2:
        return 6 if compression >= 2 else 12
    if kind == 2:
        return 4 if compression >= 3 else 8 if compression >= 1 else 16
    return 2 if compression else 4


def _decode_snorm16(reader: Reader) -> float:
    value = struct.unpack("<h", reader.take(2))[0]
    return max(-1.0, value / 32767.0)


def _decode_channel(reader: Reader, kind: int, compression: int) -> tuple[float, ...]:
    if kind < 2:
        if compression >= 2:
            # RB2 Wii stores position and scale vectors as signed normalized
            # words over a fixed 1300-unit domain.  Multiplying by 1300 makes
            # sample zero exactly reproduce the neutral male_guitar local
            # translations and scales; this is an encoded source invariant,
            # not a Duke-specific fit.
            return tuple(
                _decode_snorm16(reader) * RB2_WII_VECTOR_FIXED_SCALE
                for _ in range(3)
            )
        return tuple(reader.f32() for _ in range(3))
    if kind == 2:
        if compression == 0:
            return tuple(reader.f32() for _ in range(4))
        if compression < 3:
            return tuple(_decode_snorm16(reader) for _ in range(4))
        # The revision-14 Duke deformation clip does not use byte quaternions.
        # Keep the branch explicit so another preset fails rather than silently
        # receiving an invented quaternion reconstruction.
        raise FormatError("RB2 byte-compressed deform quaternions are not decoded")
    if compression == 0:
        return (reader.f32(),)
    return (_decode_snorm16(reader),)


def deform_sample_weights(height: float, weight: float) -> list[float]:
    """Return RB2 CharDeform's five-point triangle weights.

    The retail domain is a square with a neutral center.  Each quadrant is a
    triangle joining the center to two adjacent corners.  This is the same
    piecewise-linear surface described by CharDeform::Frame's point/triangle
    implementation and avoids bilinear smoothing that the game did not use.
    """
    height = min(1.0, max(0.0, float(height)))
    weight = min(1.0, max(0.0, float(weight)))
    dh = height - 0.5
    dw = weight - 0.5
    result = [0.0] * 5
    if abs(dh) < 1.0e-9 and abs(dw) < 1.0e-9:
        result[0] = 1.0
        return result

    # Four triangles join the center to one edge of the square.  The dominant
    # axis selects the edge; the minor axis divides weight between its corners.
    if dh >= abs(dw):
        result[0] = 1.0 - 2.0 * dh
        result[1] = dh + dw
        result[3] = dh - dw
    elif -dh >= abs(dw):
        result[0] = 1.0 + 2.0 * dh
        result[2] = -dh + dw
        result[4] = -dh - dw
    elif dw >= abs(dh):
        result[0] = 1.0 - 2.0 * dw
        result[1] = dw + dh
        result[2] = dw - dh
    else:
        result[0] = 1.0 + 2.0 * dw
        result[3] = -dw + dh
        result[4] = -dw - dh
    return result


def evaluate_deform(
    samples: DeformSamples, height: float, weight: float
) -> dict[str, tuple[float, ...]]:
    if samples.sample_count != len(DEFORM_CONTROL_POINTS):
        raise FormatError(
            "RB2 body deform requires the authored five-point sample surface, "
            f"got {samples.sample_count} samples"
        )
    weights = deform_sample_weights(height, weight)
    result: dict[str, tuple[float, ...]] = {}
    for channel in samples.channels:
        values = [sample[channel.name] for sample in samples.values]
        blended = tuple(
            sum(weights[index] * values[index][axis] for index in range(5))
            for axis in range(len(values[0]))
        )
        if channel.name.casefold().endswith(".quat"):
            length = math.sqrt(sum(component * component for component in blended))
            if length > 1.0e-8:
                blended = tuple(component / length for component in blended)
        result[channel.name] = blended
    return result


def _read_samples(reader: Reader) -> DeformSamples:
    version = reader.i32()
    if version not in (14, 16):
        raise FormatError(
            "expected RB2 deform CharBonesSamples revision 14 or 16 at "
            f"0x{reader.pos - 4:X}, got {version}"
        )
    channel_count = reader.i32()
    if channel_count < 0 or channel_count > 4096:
        raise FormatError(f"implausible RB2 deform channel count {channel_count}")
    channels = [DeformChannel(reader.string(), reader.f32()) for _ in range(channel_count)]
    counts = [reader.u32() for _ in range(7 if version > 15 else 10)]
    if counts[:7] != sorted(counts[:7]) or counts[6] != channel_count:
        raise FormatError(
            f"RB2 deform channel boundaries do not cover the channel list: {counts}"
        )
    compression = reader.u32()
    sample_count = reader.u32()
    if compression > 4 or sample_count > 100_000:
        raise FormatError(
            f"invalid RB2 deform compression/sample count {compression}/{sample_count}"
        )
    frame_count = reader.u32()
    if frame_count > 100_000:
        raise FormatError(f"implausible RB2 deform frame count {frame_count}")
    frames = reader.floats(frame_count)
    version14_flag = bool(reader.u8()) if version == 14 else False

    raw_size = sum(_component_size(_channel_kind(row.name), compression) for row in channels)
    # RB2 allocates each decoded runtime sample on a 16-byte stride, but the
    # revision-16 Wii stream writes the channel payloads contiguously.
    bytes_per_sample = raw_size
    runtime_bytes_per_sample = (raw_size + 15) & ~15
    values: list[dict[str, tuple[float, ...]]] = []
    for _ in range(sample_count):
        start = reader.pos
        sample: dict[str, tuple[float, ...]] = {}
        for channel in channels:
            sample[channel.name] = _decode_channel(
                reader, _channel_kind(channel.name), compression
            )
        consumed = reader.pos - start
        if consumed > bytes_per_sample:
            raise FormatError("RB2 deform sample exceeded its authored stride")
        reader.take(bytes_per_sample - consumed)
        values.append(sample)
    return DeformSamples(
        version,
        channels,
        counts,
        compression,
        sample_count,
        frames,
        version14_flag,
        values,
        bytes_per_sample,
        runtime_bytes_per_sample,
    )


def parse_deform_clip(path: Path) -> DeformClip:
    reader = Reader(path.read_bytes())
    revision = reader.u32()
    char_clip_revision = reader.i32()
    if revision != 14 or char_clip_revision != 9:
        raise FormatError(
            "expected RB2 Wii CharClipSamples revision 14 / CharClip revision 9, "
            f"got {revision}/{char_clip_revision}"
        )
    skip_object_fields(reader, 25)
    start_frame = reader.i32()
    end_frame = reader.i32()
    frames_per_second = reader.f32()
    flags = reader.i32()
    play_flags = reader.i32()
    blend_width = reader.i32()
    range_frames = reader.f32()
    relative = reader.string()
    reader.u8()  # revision-9 legacy flag
    transitions_byte_hint = reader.i32()
    transition_count = reader.u32()
    if transition_count > 100_000:
        raise FormatError(f"implausible RB2 deform transition count {transition_count}")
    for _ in range(transition_count):
        reader.string()
        node_count = reader.u32()
        if node_count > 100_000:
            raise FormatError(f"implausible RB2 deform transition-node count {node_count}")
        reader.take(node_count * 8)
    event_count = reader.u32()
    if event_count > 100_000:
        raise FormatError(f"implausible RB2 deform event count {event_count}")
    events = [
        {"event": reader.string(), "beat": reader.f32()}
        for _ in range(event_count)
    ]
    full = _read_samples(reader)
    one = _read_samples(reader)
    if reader.pos != len(reader.data):
        raise FormatError(
            f"RB2 deform clip has {len(reader.data) - reader.pos} residual bytes"
        )
    return DeformClip(
        revision,
        char_clip_revision,
        start_frame,
        end_frame,
        frames_per_second,
        flags,
        play_flags,
        blend_width,
        range_frames,
        relative,
        transitions_byte_hint,
        events,
        full,
        one,
    )


def audit(clip: DeformClip) -> dict[str, Any]:
    return {
        "schema": 1,
        "revision": clip.revision,
        "char_clip_revision": clip.char_clip_revision,
        "start_frame": clip.start_frame,
        "end_frame": clip.end_frame,
        "frames_per_second": clip.frames_per_second,
        "full": {
            "channels": len(clip.full.channels),
            "compression": clip.full.compression,
            "samples": clip.full.sample_count,
            "frames": clip.full.frames,
            "bytes_per_sample": clip.full.bytes_per_sample,
            "runtime_bytes_per_sample": clip.full.runtime_bytes_per_sample,
            "channel_names": [row.name for row in clip.full.channels],
            "channel_weights": [row.weight for row in clip.full.channels],
            "control_points_height_weight": [list(row) for row in DEFORM_CONTROL_POINTS],
        },
        "one": {
            "channels": len(clip.one.channels),
            "compression": clip.one.compression,
            "samples": clip.one.sample_count,
            "frames": clip.one.frames,
            "bytes_per_sample": clip.one.bytes_per_sample,
            "runtime_bytes_per_sample": clip.one.runtime_bytes_per_sample,
            "channel_names": [row.name for row in clip.one.channels],
            "channel_weights": [row.weight for row in clip.one.channels],
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("clip", type=Path)
    parser.add_argument("--audit", type=Path)
    args = parser.parse_args()
    result = audit(parse_deform_clip(args.clip))
    output = json.dumps(result, indent=2) + "\n"
    if args.audit:
        args.audit.parent.mkdir(parents=True, exist_ok=True)
        args.audit.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
