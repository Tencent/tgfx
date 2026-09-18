#!/usr/bin/env python3
"""Which normalized geometry actually forces the quadrature point count up?

anisotropic_reduction_study.py showed N=4 reaching 3.55 levels once sigma differs per axis, and
the design doc attributes that to anisotropy. But the shader only ever sees sigma-normalized
geometry, so anisotropy cannot matter on its own: it can only matter through the normalized shape.
This script separates the two candidate causes that a per-axis divide produces:

  1. an extreme normalized aspect ratio (halfAlong vs halfCross), which an isotropic blur on an
     elongated shape reaches as well, and
  2. an eccentric corner (cornerAlong != cornerCross), which only a per-axis divide produces.

All lengths are already in sigma units, so sigma == 1 and the kernel support is +/-2. Errors are
reported in 8-bit levels (1/255) against a converged 3000-point reference of the same integral.
"""

import math

TRUNC = 2.0
NORM = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
                                   - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0))))


def density(t):
    if abs(t) > TRUNC:
        return 0.0
    return math.exp(-t * t / 2.0) / NORM


def cdf(u):
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    lo = 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0)))
    cur = 0.5 * (1.0 + math.erf(u / math.sqrt(2.0)))
    return (cur - lo) * math.sqrt(2.0 * math.pi) / NORM


def slice_coverage(along_coord, cross_coord, corner_along, corner_cross, half_along, half_cross):
    """Mirrors shapeBlurSliceCoverage in GLSLShapeBlurFunctions.cpp."""
    delta = min(half_cross - corner_cross - abs(cross_coord), 0.0)
    ratio = corner_along / max(corner_cross, 1e-6)
    curved = half_along - corner_along + ratio * math.sqrt(
        max(0.0, corner_cross * corner_cross - delta * delta))
    return cdf(along_coord + curved) - cdf(along_coord - curved)


def coverage(px, py, half, corner, n):
    """Mirrors shapeBlurRRectCoverage: the quadrature walks the cross axis."""
    if abs(px) > abs(py):
        cross_coord, cross_half, swap = py, half[1], False
    else:
        cross_coord, cross_half, swap = px, half[0], True
    lo = cross_coord - cross_half
    hi = cross_coord + cross_half
    start = min(max(-TRUNC, lo), hi)
    end = min(max(TRUNC, lo), hi)
    if end <= start:
        return 0.0
    step = (end - start) / n
    accum = 0.0
    weight_sum = 0.0
    for i in range(n):
        s = start + (i + 0.5) * step
        w = density(s)
        if swap:
            span = slice_coverage(py, px - s, corner[1], corner[0], half[1], half[0])
        else:
            span = slice_coverage(px, py - s, corner[0], corner[1], half[0], half[1])
        accum += span * w
        weight_sum += w
    if weight_sum <= 0.0:
        return 0.0
    return accum * (cdf(end) - cdf(start)) / weight_sum


def worst_error(half, corner, n, steps=21):
    span_x = half[0] + TRUNC + 0.5
    span_y = half[1] + TRUNC + 0.5
    worst = 0.0
    for i in range(steps):
        for j in range(steps):
            px = -span_x + 2.0 * span_x * i / (steps - 1)
            py = -span_y + 2.0 * span_y * j / (steps - 1)
            ref = coverage(px, py, half, corner, 3000)
            got = coverage(px, py, half, corner, n)
            worst = max(worst, abs(got - ref))
    return worst * 255.0


def main():
    # (label, half, corner). The anisotropic rows restate the design doc's worst cases after
    # normalization: half 40x25 with r=10 divided by the sigma pair given in the doc.
    cases = [
        ("circle, isotropic", (3.0, 3.0), (3.0, 3.0)),
        ("aspect 2.5, circular corner", (10.0, 4.0), (2.0, 2.0)),
        ("aspect 5 (100x20 r=5, sigma 2)", (25.0, 5.0), (2.5, 2.5)),
        ("aspect 5 (100x20 r=5, sigma 5)", (10.0, 2.0), (1.0, 1.0)),
        ("aspect 20, circular corner", (20.0, 1.0), (0.5, 0.5)),
        ("doc 4:1 (sigma 16/4)", (2.5, 6.25), (0.625, 2.5)),
        ("doc 1:3 (sigma 4/12)", (10.0, 2.0833), (2.5, 0.8333)),
        ("doc 1:3, corner circularized", (10.0, 2.0833), (2.0833, 2.0833)),
    ]
    header = f"{'case':34}{'N=4':>8}{'N=8':>8}{'N=16':>8}"
    print("worst error in 8-bit levels, 21x21 probes, kernel truncated at +/-2 sigma")
    print()
    print(header)
    print("-" * len(header))
    for label, half, corner in cases:
        row = f"{label:34}"
        for n in (4, 8, 16):
            row += f"{worst_error(half, corner, n):8.2f}"
        print(row)


if __name__ == "__main__":
    main()
