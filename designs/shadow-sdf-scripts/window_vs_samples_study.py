#!/usr/bin/env python3
"""Does N=4 still suffice when the kernel window is wider than +/-2 sigma?

quadrature_study.py measured the outer-quadrature error with the kernel
truncated at +/-2 sigma, matching TGFX's existing GaussianBlur1D. But Evan
Wallace's shader integrates over +/-3 sigma and still claims "surprisingly few
samples" with N=4. Since a wider window means the same N spreads over more
ground, the N=4 conclusion must be re-checked per window width rather than
assumed.

This script sweeps the truncation half-width k (in sigma) against N, so the
choice of k and N can be made together instead of one implying the other.

All lengths in sigma units (sigma = 1). Errors in 8-bit levels (1/255).
"""

import math


def make_kernel(k):
    """Return (density, cdf) for a Gaussian truncated to [-k, k] and
    renormalized over that window."""
    mass = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(k / math.sqrt(2.0)))
                                       - 0.5 * (1.0 + math.erf(-k / math.sqrt(2.0))))

    def density(t):
        if abs(t) > k:
            return 0.0
        return math.exp(-t * t / 2.0) / mass

    def cdf(u):
        if u <= -k:
            return 0.0
        if u >= k:
            return 1.0
        lo = 0.5 * (1.0 + math.erf(-k / math.sqrt(2.0)))
        cur = 0.5 * (1.0 + math.erf(u / math.sqrt(2.0)))
        return (cur - lo) * math.sqrt(2.0 * math.pi) / mass

    return density, cdf


def half_width(y, half_x, half_y, r):
    ay = abs(y)
    if ay > half_y:
        return None
    flat = half_y - r
    if ay <= flat:
        return half_x
    dy = ay - flat
    inner = r * r - dy * dy
    if inner <= 0.0:
        return half_x - r
    return half_x - r + math.sqrt(inner)


def row_span(x, y, half_x, half_y, r, cdf):
    w = half_width(y, half_x, half_y, r)
    if w is None or w <= 0.0:
        return 0.0
    return cdf(x + w) - cdf(x - w)


def integration_range(coord_along, half_along, k):
    lo = coord_along - half_along
    hi = coord_along + half_along
    return min(max(-k, lo), hi), min(max(k, lo), hi)


def coverage(px, py, half_x, half_y, r, n, k, density, cdf):
    if abs(px) > abs(py):
        along_coord, along_half, swap = py, half_y, False
    else:
        along_coord, along_half, swap = px, half_x, True
    start, end = integration_range(along_coord, along_half, k)
    if end <= start:
        return 0.0
    step = (end - start) / n
    accum = 0.0
    weight_sum = 0.0
    for i in range(n):
        s = start + (i + 0.5) * step
        w = density(s)
        if swap:
            span = row_span(py, px - s, half_y, half_x, r, cdf)
        else:
            span = row_span(px, py - s, half_x, half_y, r, cdf)
        accum += span * w
        weight_sum += w
    if weight_sum <= 0.0:
        return 0.0
    return accum * (cdf(end) - cdf(start)) / weight_sum


def reference(px, py, half_x, half_y, r, k, density, cdf, n=3000):
    start, end = integration_range(py, half_y, k)
    if end <= start:
        return 0.0
    step = (end - start) / n
    total = 0.0
    for i in range(n):
        s = start + (i + 0.5) * step
        total += row_span(px, py - s, half_x, half_y, r, cdf) * density(s) * step
    return total


SHAPES = [
    (0.5, 0.5, 0.5),
    (1.0, 1.0, 0.5),
    (2.0, 2.0, 1.0),
    (3.0, 3.0, 3.0),
    (5.0, 5.0, 2.0),
    (10.0, 4.0, 2.0),
    (4.0, 10.0, 2.0),
    (20.0, 20.0, 5.0),
    (20.0, 1.0, 0.5),
]


def probes(half_x, half_y, k, steps=15):
    span_x = half_x + k + 0.5
    span_y = half_y + k + 0.5
    pts = []
    for i in range(steps):
        for j in range(steps):
            pts.append((-span_x + 2.0 * span_x * i / (steps - 1),
                        -span_y + 2.0 * span_y * j / (steps - 1)))
    return pts


def main():
    ks = [2.0, 2.5, 3.0, 4.0]
    ns = [4, 6, 8, 12]
    print("outer-quadrature error vs kernel window half-width k and sample count N")
    print("errors in 8-bit levels (1/255), worst case over 9 shapes x 15x15 probes")
    print()
    header = "  k (sigma) " + "".join(f"{f'N={n}':>10}" for n in ns)
    print(header)
    print("-" * len(header))
    for k in ks:
        density, cdf = make_kernel(k)
        row = f"{k:11.1f}"
        for n in ns:
            worst = 0.0
            for half_x, half_y, r in SHAPES:
                pts = probes(half_x, half_y, k)
                for px, py in pts:
                    ref = reference(px, py, half_x, half_y, r, k, density, cdf)
                    got = coverage(px, py, half_x, half_y, r, n, k, density, cdf)
                    worst = max(worst, abs(got - ref))
            row += f"{worst * 255.0:10.2f}"
        print(row)
    print()
    print("k=2.0 matches TGFX's existing GaussianBlur1D (radius = ceil(2*sigma)).")
    print("k=3.0 matches Evan Wallace's shader (clamp(+/-3.0*sigma)).")


if __name__ == "__main__":
    main()
