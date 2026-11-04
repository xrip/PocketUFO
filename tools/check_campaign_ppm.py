#!/usr/bin/env python3
"""Check private campaign frame dumps without changing any golden image."""

from pathlib import Path
import sys


def check(path: Path) -> None:
    with path.open("rb") as image:
        magic = image.readline().strip()
        dimensions = image.readline().strip()
        maximum = image.readline().strip()
        pixels = image.read()

    if magic != b"P6" or dimensions != b"240 320" or maximum != b"255":
        raise SystemExit(f"IMAGE FAILED: {path.name} header is not P6 240x320")
    if len(pixels) != 240 * 320 * 3:
        raise SystemExit(f"IMAGE FAILED: {path.name} has {len(pixels)} raster bytes")

    colors = set()
    nonblack = 0
    for i in range(0, len(pixels), 3):
        color = pixels[i : i + 3]
        colors.add(color)
        nonblack += color != b"\x00\x00\x00"
        if len(colors) >= 32 and nonblack >= 500:
            break
    if len(colors) < 32 or nonblack < 500:
        raise SystemExit(
            f"IMAGE FAILED: {path.name} has only {len(colors)} colors and {nonblack} nonblack samples"
        )
    print(f"ok: {path.name} is 240x320 with {len(colors)} colors and visible content")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: check_campaign_ppm.py APP_DIR IMAGE...")
        return 2
    app_dir = Path(sys.argv[1])
    for name in sys.argv[2:]:
        path = app_dir / name
        if not path.is_file():
            raise SystemExit(f"IMAGE FAILED: missing {path}")
        check(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
