#!/usr/bin/env python3
"""Measure how the outer-quadrature error depends on the normalized corner ratio.

The reduction to isotropic space divides each axis by its own sigma, so a
circular corner r becomes an elliptical corner with semi-axes (r/sigma_x,
r/sigma_y). The quadrature error grows with how much those two semi-axes
differ, which raises the question of whether the sample count could be
dispatched on that ratio (N=4 below a threshold, N=8 above) instead of being
fixed at 8.

This script works entirely in the normalized space (sigma = 1 on both axes),
which is exact: anisotropic_reduction_study.py already showed the reduction
itself contributes no systematic error (<= 0.51 levels at N=32 against a
brute-force 2D convolution in the original space).

Reference = high-resolution midpoint quadrature of the same reduced form.
All errors in 8-bit levels (1/255).
"""

import math

TRUNC = 2.0
REFERENCE_N = 800

NORM = math.sqrt(2.0 * math.pi) * (
    0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
    - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0)))
)
LOW = 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0)))
MASS_SCALE = math.sqrt(2.0 * math.pi) / NORM


def kernel1d(t):
    if abs(t) > TRUNC:
        return 0.0
    return math.exp(-t * t / 2.0) / NORM


def cdf(u):
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    return (0.5 * (1.0 + math.erf(u / math.sqrt(2.0))) - LOW) * MASS_SCALE


def half_width(y, half_x, half_y, rx, ry):
    ay = abs(y)
    if ay > half_y:
        return None
    flat = half_y - ry
    if ay <= flat:
        return half_x
    dy = ay - flat
    inner = ry * ry - dy * dy
    if inner <= 0.0:
        return half_x - rx
    return half_x - rx + (rx / ry) * math.sqrt(inner)


def row_span(x, y, half_x, half_y, rx, ry):
    w = half_width(y, half_x, half_y, rx, ry)
    if w is None or w <= 0.0:
        return 0.0
    return cdf(x + w) - cdf(x - w)


def coverage(px, py, half_x, half_y, rx, ry, n_quad):
    """Reduced form with adaptive axis selection, N-point outer quadrature."""
    if abs(px) > abs(py):
        along_c, along_h = py, half_y
        swap = False
    else:
        along_c, along_h = px, half_x
        swap = True
    lo, hi = along_c - along_h, along_c + along_h
    start = min(max(-TRUNC, lo), hi)
    end = min(max(TRUNC, lo), hi)
    if end <= start:
        return 0.0
    step = (end - start) / n_quad
    accum = 0.0
    wsum = 0.0
    for i in range(n_quad):
        s = start + (i + 0.5) * step
        w = kernel1d(s)
        if swap:
            span = row_span(py, px - s, half_y, half_x, ry, rx)
        else:
            span = row_span(px, py - s, half_x, half_y, rx, ry)
        accum += span * w
        wsum += w
    if wsum <= 0.0:
        return 0.0
    return accum * (cdf(end) - cdf(start)) / wsum


def probe_points(half_x, half_y, steps=11):
    span_x = half_x + TRUNC
    span_y = half_y + TRUNC
    pts = []
    for i in range(steps):
        for j in range(steps):
            pts.append((-span_x + 2.0 * span_x * i / (steps - 1),
                        -span_y + 2.0 * span_y * j / (steps - 1)))
    return pts


def shapes_for_ratio(ratio):
    """Normalized shapes whose corner semi-axis ratio equals the given value.

    Covers several shape aspects and corner sizes, including the fully rounded
    case (corner == half size) and shapes near the isotropic worst case found by
    quadrature_study.py (half 10 x 4).
    """
    out = []
    for half_x, half_y in ((6.0, 6.0), (10.0, 4.0), (4.0, 10.0), (10.0, 2.5), (3.0, 3.0)):
        for corner_frac in (0.25, 0.5, 1.0):
            # Pick the larger semi-axis from the smaller side so both fit.
            base = corner_frac * min(half_x, half_y)
            for rx, ry in ((base * ratio, base), (base, base * ratio)):
                if rx <= half_x + 1e-9 and ry <= half_y + 1e-9:
                    out.append((half_x, half_y, rx, ry))
    return out


def main():
    ratios = (1.0, 1.5, 2.0, 2.5, 3.0, 4.0)
    counts = (4, 6, 8)

    print("Outer-quadrature error vs normalized corner semi-axis ratio.")
    print(f"kernel: Gaussian truncated at +/-{TRUNC:.0f} sigma, renormalized")
    print(f"reference: same form at N={REFERENCE_N}; 11x11 probe grid per shape")
    print("errors in 8-bit levels (1/255), worst over all shapes of that ratio")
    print()
    header = f"{'ratio':>7s}{'shapes':>8s}" + "".join(f"{'N=' + str(n):>9s}" for n in counts)
    print(header)
    print("-" * len(header))

    for ratio in ratios:
        shapes = shapes_for_ratio(ratio)
        worst = {n: 0.0 for n in counts}
        worst_shape = {n: None for n in counts}
        for half_x, half_y, rx, ry in shapes:
            pts = probe_points(half_x, half_y)
            for px, py in pts:
                ref = coverage(px, py, half_x, half_y, rx, ry, REFERENCE_N)
                for n in counts:
                    err = abs(coverage(px, py, half_x, half_y, rx, ry, n) - ref) * 255.0
                    if err > worst[n]:
                        worst[n] = err
                        worst_shape[n] = (half_x, half_y, rx, ry, px, py)
        row = f"{ratio:7.1f}{len(shapes):8d}" + "".join(f"{worst[n]:9.2f}" for n in counts)
        print(row)
        for n in counts:
            hx, hy, rx, ry, px, py = worst_shape[n]
            print(f"         N={n}: half=({hx:.1f},{hy:.1f}) corner=({rx:.2f},{ry:.2f})"
                  f" at probe=({px:.2f},{py:.2f})")

    print()
    print("Reference convergence check on the worst ratio-2 shape:")
    shapes = shapes_for_ratio(2.0)
    hx, hy, rx, ry = shapes[0]
    pts = probe_points(hx, hy)
    drift = 0.0
    for px, py in pts:
        a = coverage(px, py, hx, hy, rx, ry, REFERENCE_N)
        b = coverage(px, py, hx, hy, rx, ry, 2 * REFERENCE_N)
        drift = max(drift, abs(a - b) * 255.0)
    print(f"  N={REFERENCE_N} vs N={2 * REFERENCE_N}: max drift {drift:.6f} levels")


if __name__ == "__main__":
    main()
