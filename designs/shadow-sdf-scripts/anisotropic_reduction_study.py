#!/usr/bin/env python3
"""Verify the anisotropic-to-isotropic reduction used by the closed-form shadow.

The design claims that convolving a round-cornered rect with an ANISOTROPIC
Gaussian (sigma_x != sigma_y) is exactly equivalent to:
  1. scaling coordinates and geometry by (1/sigma_x, 1/sigma_y),
  2. convolving with a unit ISOTROPIC Gaussian,
where the original circular corner (radius r) becomes an ELLIPTICAL corner with
semi-axes (r/sigma_x, r/sigma_y).

That claim is the basis of the whole anisotropic fast path, so it must be
checked numerically rather than assumed.

Ground truth: brute-force 2D convolution in the ORIGINAL space, using the
anisotropic truncated Gaussian directly against the exact shape indicator.
Comparison: the reduced form (elliptical-corner rowSpan + outer quadrature),
which is what the shader will compute.

All errors reported in 8-bit levels (1/255).
"""

import math

TRUNC = 2.0  # kernel truncated at +/- 2 sigma, matching the existing filter

# Mass of exp(-t^2/2) over [-TRUNC, TRUNC].
NORM = math.sqrt(2.0 * math.pi) * (
    0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
    - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0)))
)


def kernel1d(t):
    """Truncated, renormalized 1D Gaussian density in sigma units."""
    if abs(t) > TRUNC:
        return 0.0
    return math.exp(-t * t / 2.0) / NORM


def cdf(u):
    """CDF of the truncated, renormalized Gaussian in sigma units."""
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    lo = 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0)))
    cur = 0.5 * (1.0 + math.erf(u / math.sqrt(2.0)))
    return (cur - lo) * math.sqrt(2.0 * math.pi) / NORM


def inside_round_rect(x, y, half_x, half_y, r):
    """Exact indicator of an axis-aligned rect with circular corners radius r."""
    ax, ay = abs(x), abs(y)
    if ax > half_x or ay > half_y:
        return 0.0
    # Corner region test.
    cx, cy = half_x - r, half_y - r
    if ax <= cx or ay <= cy:
        return 1.0
    dx, dy = ax - cx, ay - cy
    return 1.0 if dx * dx + dy * dy <= r * r else 0.0


def coverage_bruteforce(px, py, half_x, half_y, r, sigma_x, sigma_y, n=421):
    """Ground truth: 2D convolution in ORIGINAL space with anisotropic kernel.

    Separable midpoint quadrature over the kernel support box, evaluating the
    exact shape indicator. No reduction, no elliptical approximation.
    """
    lo_x, hi_x = -TRUNC * sigma_x, TRUNC * sigma_x
    lo_y, hi_y = -TRUNC * sigma_y, TRUNC * sigma_y
    step_x = (hi_x - lo_x) / n
    step_y = (hi_y - lo_y) / n
    total = 0.0
    for i in range(n):
        sx = lo_x + (i + 0.5) * step_x
        wx = kernel1d(sx / sigma_x) / sigma_x
        if wx == 0.0:
            continue
        row = 0.0
        for j in range(n):
            sy = lo_y + (j + 0.5) * step_y
            wy = kernel1d(sy / sigma_y) / sigma_y
            if wy == 0.0:
                continue
            row += inside_round_rect(px - sx, py - sy, half_x, half_y, r) * wy
        total += row * wx * step_x * step_y
    return total


def half_width_elliptical(y, half_x, half_y, rx, ry):
    """Row half-width for an elliptical corner (semi-axes rx along x, ry along y).

    This is the formula the shader's rowSpan uses after the reduction.
    """
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
    w = half_width_elliptical(y, half_x, half_y, rx, ry)
    if w is None or w <= 0.0:
        return 0.0
    return cdf(x + w) - cdf(x - w)


def coverage_reduced(px, py, half_x, half_y, r, sigma_x, sigma_y, n_quad=4,
                     adaptive_axis=True):
    """The reduced form: normalize by (1/sx, 1/sy), elliptical corner, N-point
    outer quadrature. This mirrors what the shader computes."""
    # Normalize coordinates and geometry.
    cx, cy = px / sigma_x, py / sigma_y
    hx, hy = half_x / sigma_x, half_y / sigma_y
    rx, ry = r / sigma_x, r / sigma_y

    # Adaptive axis selection (integrate along the axis with smaller |coord|).
    if adaptive_axis and abs(cx) > abs(cy):
        along_c, along_h = cy, hy
        swap = False
    else:
        along_c, along_h = cx, hx
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
            # Integrating along x: rows are vertical, swap roles.
            span = row_span(cy, cx - s, hy, hx, ry, rx)
        else:
            span = row_span(cx, cy - s, hx, hy, rx, ry)
        accum += span * w
        wsum += w
    if wsum <= 0.0:
        return 0.0
    return accum * (cdf(end) - cdf(start)) / wsum


def probes(half_x, half_y, sigma_x, sigma_y, steps=9):
    span_x = half_x + TRUNC * sigma_x
    span_y = half_y + TRUNC * sigma_y
    pts = []
    for i in range(steps):
        for j in range(steps):
            pts.append((-span_x + 2.0 * span_x * i / (steps - 1),
                        -span_y + 2.0 * span_y * j / (steps - 1)))
    return pts


def main():
    # (half_x, half_y, r, sigma_x, sigma_y)
    cases = [
        ("isotropic baseline", 40.0, 25.0, 10.0, 6.0, 6.0),
        ("mild aniso 2:1", 40.0, 25.0, 10.0, 12.0, 6.0),
        ("strong aniso 4:1", 40.0, 25.0, 10.0, 16.0, 4.0),
        ("reversed aniso 1:3", 40.0, 25.0, 10.0, 4.0, 12.0),
        ("sharp corner aniso", 40.0, 25.0, 0.0, 12.0, 5.0),
        ("full-round (oval)", 30.0, 30.0, 30.0, 10.0, 5.0),
        ("thin shape aniso", 50.0, 8.0, 4.0, 10.0, 4.0),
        ("blur >> shape", 12.0, 10.0, 4.0, 14.0, 7.0),
    ]

    print("Verifying the anisotropic -> isotropic reduction (elliptical corner).")
    print(f"kernel: Gaussian truncated at +/-{TRUNC:.0f} sigma, renormalized")
    print("ground truth = brute-force 2D convolution in original space")
    print("errors in 8-bit levels (1/255), max over a 9x9 probe grid")
    print()
    header = f"{'case':22s}{'sx':>6s}{'sy':>6s}{'N=4':>9s}{'N=8':>9s}{'N=32':>9s}"
    print(header)
    print("-" * len(header))

    worst_overall = {4: 0.0, 8: 0.0, 32: 0.0}
    for name, hx, hy, r, sx, sy in cases:
        pts = probes(hx, hy, sx, sy)
        truth = [coverage_bruteforce(px, py, hx, hy, r, sx, sy) for px, py in pts]
        row = f"{name:22s}{sx:6.1f}{sy:6.1f}"
        for n in (4, 8, 32):
            err = 0.0
            for (px, py), ref in zip(pts, truth):
                got = coverage_reduced(px, py, hx, hy, r, sx, sy, n_quad=n)
                err = max(err, abs(got - ref))
            worst_overall[n] = max(worst_overall[n], err * 255.0)
            row += f"{err * 255.0:9.2f}"
        print(row)

    print("-" * len(header))
    row = f"{'worst overall':22s}{'':6s}{'':6s}"
    for n in (4, 8, 32):
        row += f"{worst_overall[n]:9.2f}"
    print(row)
    print()
    print("Interpretation: the N=32 column isolates the reduction itself (outer")
    print("quadrature error is negligible there). If N=32 is near zero, the")
    print("reduction is exact and the residual at N=4 is pure quadrature error.")


if __name__ == "__main__":
    main()
