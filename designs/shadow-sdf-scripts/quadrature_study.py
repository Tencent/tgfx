#!/usr/bin/env python3
"""Numerical study for the analytic shadow coverage shader.

Goal: decide the outer-quadrature sample count N for the rounded-rect closed-form
shadow, under the SAME kernel the existing GaussianBlur1D filter uses:
a Gaussian truncated to +/-2 sigma and renormalized (see
GLSLGaussianBlur1DFragmentProcessor.cpp: radius = ceil(2*sigma), weights
exp(-i^2/2sigma^2), divided by their sum).

All lengths are expressed in sigma units, so sigma == 1 and the kernel support
is +/-2. Errors are reported in 8-bit levels (1/255) because that is the
visual tolerance of the final render target.
"""

import math

TRUNC = 2.0  # kernel truncated at +/- 2 sigma, matching the existing filter

# Normalization constant of the truncated kernel: integral of exp(-t^2/2) over
# [-TRUNC, TRUNC].
NORM = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
                                   - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0))))


def kernel(t):
    """Truncated, normalized Gaussian density."""
    if abs(t) > TRUNC:
        return 0.0
    return math.exp(-t * t / 2.0) / NORM


def cdf(u):
    """CDF of the truncated, normalized Gaussian. Closed form via erf."""
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    lo = 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0)))
    cur = 0.5 * (1.0 + math.erf(u / math.sqrt(2.0)))
    return (cur - lo) * math.sqrt(2.0 * math.pi) / NORM


def half_width(y, half_x, half_y, r):
    """Horizontal half-width of the rounded rect at row y, or None outside."""
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


def row_span(x, y, half_x, half_y, r):
    """Inner closed-form integral: horizontal blurred coverage of one row."""
    w = half_width(y, half_x, half_y, r)
    if w is None or w <= 0.0:
        return 0.0
    return cdf(x + w) - cdf(x - w)


def integration_range(coord_along, half_along):
    """Outer integration interval: kernel support intersected with the shape."""
    lo = coord_along - half_along
    hi = coord_along + half_along
    start = min(max(-TRUNC, lo), hi)
    end = min(max(TRUNC, lo), hi)
    return start, end


def coverage_quadrature(px, py, half_x, half_y, r, n):
    """N-point midpoint quadrature with kernel weights, renormalized by the
    true kernel mass of the interval (the scheme from the design doc)."""
    # Integrate along the axis whose coordinate component is smaller, so the
    # interval is more likely to be a full window.
    if abs(px) > abs(py):
        along_coord, along_half = py, half_y
        swap = False
    else:
        along_coord, along_half = px, half_x
        swap = True
    start, end = integration_range(along_coord, along_half)
    if end <= start:
        return 0.0
    step = (end - start) / n
    accum = 0.0
    weight_sum = 0.0
    for i in range(n):
        s = start + (i + 0.5) * step
        w = kernel(s)
        if swap:
            span = row_span(py, px - s, half_y, half_x, r)
        else:
            span = row_span(px, py - s, half_x, half_y, r)
        accum += span * w
        weight_sum += w
    if weight_sum <= 0.0:
        return 0.0
    return accum * (cdf(end) - cdf(start)) / weight_sum


def coverage_reference(px, py, half_x, half_y, r, n=4000):
    """High-resolution reference for the same double integral."""
    start, end = integration_range(py, half_y)
    if end <= start:
        return 0.0
    step = (end - start) / n
    total = 0.0
    for i in range(n):
        s = start + (i + 0.5) * step
        total += row_span(px, py - s, half_x, half_y, r) * kernel(s) * step
    return total


def sample_points(half_x, half_y):
    """Probe grid covering the shape plus the full kernel reach outside it."""
    pts = []
    span_x = half_x + TRUNC + 0.5
    span_y = half_y + TRUNC + 0.5
    steps = 21
    for i in range(steps):
        for j in range(steps):
            px = -span_x + 2.0 * span_x * i / (steps - 1)
            py = -span_y + 2.0 * span_y * j / (steps - 1)
            pts.append((px, py))
    return pts


def main():
    # Shapes in sigma units: from "blur much larger than shape" to "blur tiny".
    shapes = [
        (0.5, 0.5, 0.5),
        (1.0, 1.0, 0.5),
        (2.0, 2.0, 1.0),
        (3.0, 3.0, 3.0),   # circle
        (5.0, 5.0, 2.0),
        (10.0, 4.0, 2.0),  # wide
        (4.0, 10.0, 2.0),  # tall
        (20.0, 20.0, 5.0),
        (20.0, 1.0, 0.5),  # extreme aspect
        (6.0, 6.0, 0.0),   # sharp corners
    ]
    candidates = [4, 6, 8, 12, 16]

    print(f"kernel: Gaussian truncated at +/-{TRUNC:.0f} sigma, renormalized")
    print("errors in 8-bit levels (1/255), over a 21x21 probe grid per shape")
    print()
    header = "  halfX  halfY      r " + "".join(f"{f'N={n}':>10}" for n in candidates)
    print(header)
    print("-" * len(header))

    worst = {n: 0.0 for n in candidates}
    for half_x, half_y, r in shapes:
        pts = sample_points(half_x, half_y)
        refs = [coverage_reference(px, py, half_x, half_y, r) for px, py in pts]
        row = f"{half_x:7.1f}{half_y:7.1f}{r:7.1f}"
        for n in candidates:
            err = 0.0
            for (px, py), ref in zip(pts, refs):
                got = coverage_quadrature(px, py, half_x, half_y, r, n)
                err = max(err, abs(got - ref))
            levels = err * 255.0
            worst[n] = max(worst[n], levels)
            row += f"{levels:10.2f}"
        print(row)

    print("-" * len(header))
    row = "  worst case          "
    for n in candidates:
        row += f"{worst[n]:10.2f}"
    print(row)


if __name__ == "__main__":
    main()
