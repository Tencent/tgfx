#!/usr/bin/env python3
"""Measures the shadow outline in a Figma screenshot to tell how spread treats corners.

The screenshot must show a rectangle with a drop shadow that has a non-zero spread, with the
shadow offset far enough that at least one shadow corner is not covered by the rectangle itself.

Usage: python3 measure_figma_shadow_corner.py <screenshot.png>
"""

import sys


def load(path):
    from PIL import Image
    return Image.open(path).convert('RGBA')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    image = load(sys.argv[1])
    pixels = image.load()
    width, height = image.size

    # Sample the solid shadow colour from the densest region, and the backdrop away from it.
    shadow = pixels[600, 1100]
    backdrop = pixels[1200, 200]
    print('shadow colour   %s' % (shadow,))
    print('backdrop colour %s' % (backdrop,))

    # 50% iso-line on the blue channel separates shadow from backdrop.
    threshold = (shadow[2] + backdrop[2]) // 2
    print('blue threshold  %d' % threshold)

    def inside(x, y):
        return pixels[x, y][2] >= threshold

    columns = []
    for y in range(0, height, 2):
        row = [x for x in range(0, width, 2) if inside(x, y)]
        if row:
            columns.append((y, min(row), max(row)))
    if not columns:
        print('no shadow found')
        return 1
    top = columns[0][0]
    bottom = columns[-1][0]
    left = min(c[1] for c in columns)
    right = max(c[2] for c in columns)
    print('shadow bbox x %d..%d  y %d..%d' % (left, right, top, bottom))

    print()
    print('left edge profile (y -> leftmost inside x)')
    straight = 0
    baseline = None
    for y in range(bottom - 180, bottom + 6, 5):
        if y < 0 or y >= height:
            continue
        row = [x for x in range(left - 6, left + 220) if 0 <= x < width and inside(x, y)]
        value = row[0] if row else None
        if value is not None:
            if baseline is None:
                baseline = value
            if value == baseline:
                straight += 1
        print('  y=%d  x=%s' % (y, value))
    print()
    print('samples where the left edge stayed exactly at x=%s: %d' % (baseline, straight))
    print('A long run of a constant x means the edge is straight right up to the corner,')
    print('i.e. spread did not round the sharp corner geometrically.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
