#!/usr/bin/env python3
"""Build a labeled contact sheet from ordered raster proof files."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def label_for(path: Path) -> str:
    stem = path.stem
    if len(stem) > 3 and stem[:2].isdigit() and stem[2] == "-":
        stem = stem[3:]
    canonical = {
        "red-octane-gh1": "Red Octane (GH1)",
        "red-octane-gh2": "Red Octane (GH2)",
        "redoctane-club-gh2": "Red Octane (GH2)",
    }
    if stem in canonical:
        return canonical[stem]
    return stem.replace("-", " ").title()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--label-height", type=int, default=34)
    args = parser.parse_args()

    output_path = args.output.resolve()
    files = sorted(
        path
        for path in args.input.iterdir()
        if path.suffix.lower() in {".bmp", ".png", ".jpg", ".jpeg"}
        and path.resolve() != output_path
    )
    if not files:
        raise SystemExit(f"no raster images in {args.input}")
    if args.columns < 1 or args.width < 1 or args.label_height < 1:
        raise SystemExit("columns, width, and label height must be positive")

    with Image.open(files[0]) as first:
        aspect = first.height / first.width
    image_height = round(args.width * aspect)
    rows = (len(files) + args.columns - 1) // args.columns
    sheet = Image.new(
        "RGB",
        (args.columns * args.width, rows * (image_height + args.label_height)),
        (18, 18, 18),
    )
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=18)
    for index, path in enumerate(files):
        column = index % args.columns
        row = index // args.columns
        x = column * args.width
        y = row * (image_height + args.label_height)
        with Image.open(path) as source:
            frame = source.convert("RGB").resize(
                (args.width, image_height), Image.Resampling.LANCZOS
            )
        sheet.paste(frame, (x, y))
        draw.rectangle(
            (x, y + image_height, x + args.width, y + image_height + args.label_height),
            fill=(18, 18, 18),
        )
        draw.text(
            (x + 8, y + image_height + 7),
            label_for(path),
            font=font,
            fill=(245, 245, 245),
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    print(f"contact_sheet={args.output} images={len(files)} size={sheet.width}x{sheet.height}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
