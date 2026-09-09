#!/usr/bin/env python3
"""Encode a raster image as the simple 32-bpp PS2 HMXBitmap container.

The runtime decoder already supports this source-authored direct-color form.
Portraits are intentionally packed at 64x128 like GH2's selector textures;
the retail 30x40 portrait quad restores the approved 3:4 display aspect.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--height", type=int, default=128)
    parser.add_argument(
        "--content-rect",
        metavar="LEFT,TOP,RIGHT,BOTTOM",
        help=("resize the source into an explicit rectangle inside the output "
              "canvas; useful for matching an authored portrait matte"),
    )
    parser.add_argument(
        "--silhouette-alpha", action="store_true",
        help=("encode dark line art as alpha over a transparent paper field; "
              "GH2's selector materials supply the authored black tint"),
    )
    parser.add_argument(
        "--stock-frame", type=Path,
        help="decoded stock 64x128 portrait; retain its outer card alpha",
    )
    args = parser.parse_args()
    if args.stock_frame and (not args.silhouette_alpha or not args.content_rect):
        parser.error("--stock-frame requires --silhouette-alpha and --content-rect")

    if args.width <= 0 or args.height <= 0 or args.width > 65535 or args.height > 65535:
        raise SystemExit("dimensions must be in 1..65535")
    image = Image.open(args.source).convert("RGBA")
    if args.content_rect:
        try:
            left, top, right, bottom = (
                int(value.strip()) for value in args.content_rect.split(",")
            )
        except (TypeError, ValueError):
            raise SystemExit("--content-rect must be LEFT,TOP,RIGHT,BOTTOM")
        if not (0 <= left < right <= args.width and
                0 <= top < bottom <= args.height):
            raise SystemExit("--content-rect must fit inside the output canvas")
        scaled = image.resize(
            (right - left, bottom - top), Image.Resampling.LANCZOS
        )
        image = Image.new("RGBA", (args.width, args.height), (0, 0, 0, 0))
        image.alpha_composite(scaled, (left, top))
    else:
        image = image.resize((args.width, args.height), Image.Resampling.LANCZOS)

    # HMXBitmap's 32-byte header: magic, bpp, encoding, mip count, width,
    # height, bytes-per-line, Wii alpha, then 17 reserved zero bytes.
    header = struct.pack(
        "<BBiBHHHH", 1, 32, 3, 0, args.width, args.height,
        args.width * 4, 0
    ) + bytes(17)
    frame = Image.open(args.stock_frame).convert("RGBA") if args.stock_frame else None
    if frame and frame.size != image.size:
        parser.error("stock frame dimensions must match the output")
    payload = bytearray()
    for index, (red, green, blue, alpha) in enumerate(image.getdata()):
        if args.silhouette_alpha:
            luminance = (red * 54 + green * 183 + blue * 19) // 256
            alpha = min(alpha, max(0, min(255, (235 - luminance) * 3)))
            red = green = blue = 255
        if frame:
            x, y = index % args.width, index // args.width
            if left <= x < right and top <= y < bottom:
                # Retail selector ink peaks at 0.8 alpha, not fully opaque.
                alpha = round(alpha * 204 / 255)
            else:
                alpha = frame.getpixel((x, y))[3]
            red = green = blue = 255
        payload.extend((red, green, blue, min(128, (alpha + 1) // 2)))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
