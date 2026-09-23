#!/usr/bin/env python3
"""Convergence order of the outer midpoint quadrature.

The design doc quotes the classical composite midpoint bound
    |error| <= (b - a)^3 / (24 N^2) * max|F''|
which requires the integrand F to be twice continuously differentiable. This
script checks whether that premise holds for the rounded-rect integrand
    F(s) = rowSpan(px, py - s) * kernel(s)
and measures the observed convergence order in each case.

Method: self-convergence. If the error behaves as C * N^-p, then
    |Q(N) - Q(2N)| = C * N^-p * (1 - 2^-p)  ~  N^-p
so the slope of log|Q(N) - Q(2N)| against log N gives -p directly, without
needing a reference value (a reference computed by the same quadrature would
carry its own error of the very kind being measured).

Controlled comparison: the integration axis is fixed to y in all cases, so the
only variable is the smoothness of F on the integration window. Four cases put
the non-smooth points of F in different places:

  A  window clamped by the kernel, strictly inside the corner arc  -> F smooth
  B  window clamped by the shape, endpoints on the extreme rows    -> F' blows up
  C  window clamped by the kernel, straddling the flat/arc junction-> F'' jumps
  D  same singular geometry as B but with a flat region present     -> F' blows up
  E  sharp corners, half-width constant over the window            -> exact

All lengths are in sigma units (sigma == 1), so the kernel support is +/-2,
matching the +/-2 sigma truncation of the existing GaussianBlur1D filter.
"""

import math

TRUNC = 2.0

NORM = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
                                   - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0))))


def kernel(t):
    """Truncated, normalized Gaussian density."""
    if abs(t) > TRUNC:
        return 0.0
    return math.exp(-t * t / 2.0) / NORM


def cdf(u):
    """CDF of the truncated, normalized Gaussian."""
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


def coverage_along_y(px, py, half_x, half_y, r, n):
    """The design doc's scheme with the integration axis fixed to y."""
    start, end = integration_range(py, half_y)
    if end <= start:
        return 0.0
    step = (end - start) / n
    accum = 0.0
    weight_sum = 0.0
    for i in range(n):
        s = start + (i + 0.5) * step
        w = kernel(s)
        accum += row_span(px, py - s, half_x, half_y, r) * w
        weight_sum += w
    if weight_sum <= 0.0:
        return 0.0
    return accum * (cdf(end) - cdf(start)) / weight_sum


def second_derivative(px, py, half_x, half_y, r, s, h):
    """Central second difference of F(s) = rowSpan(px, py - s) * kernel(s)."""
    def f(t):
        return row_span(px, py - t, half_x, half_y, r) * kernel(t)
    return (f(s - h) - 2.0 * f(s) + f(s + h)) / (h * h)


def probe_second_derivative(px, py, half_x, half_y, r):
    """Largest |F''| seen as the evaluation point approaches the window end.

    Returns the values at shrinking distances from the endpoint, so a divergent
    sequence is visible directly rather than inferred.
    """
    start, end = integration_range(py, half_y)
    out = []
    for dist in (1e-2, 1e-3, 1e-4, 1e-5):
        s = start + dist
        out.append(abs(second_derivative(px, py, half_x, half_y, r, s, dist * 0.1)))
    return start, end, out


def convergence_order(px, py, half_x, half_y, r, counts):
    """Successive differences and the fitted order p from log-log slope."""
    values = {n: coverage_along_y(px, py, half_x, half_y, r, n) for n in counts}
    diffs = []
    for n in counts[:-1]:
        diffs.append((n, abs(values[n] - values[2 * n])))
    fitted = []
    for (n1, d1), (n2, d2) in zip(diffs, diffs[1:]):
        if d1 > NOISE_FLOOR and d2 > NOISE_FLOOR:
            fitted.append((n1, math.log(d1 / d2) / math.log(n2 / n1)))
    return values, diffs, fitted


# The probe point sits on the vertical edge (px == half_x) in every case: with
# px deep inside, cdf(px +/- w) saturates, rowSpan is 1 on every row and the
# quadrature is exact regardless of smoothness, which would make the comparison
# blind to the effect being measured.
CASES = [
    ("A  smooth (kernel-clamped, inside arc)", 5.0, 0.0, 5.0, 5.0, 5.0),
    ("B  sqrt endpoint (shape-clamped)", 5.0, 0.0, 5.0, 1.0, 1.0),
    ("C  F'' jump inside (flat/arc junction)", 5.0, 3.0, 5.0, 6.0, 2.0),
    ("D  sqrt endpoint, flat region present", 5.0, 0.0, 5.0, 1.5, 0.5),
    ("E  sharp corners (half-width constant)", 5.0, 0.0, 5.0, 6.0, 0.0),
]

NOISE_FLOOR = 1e-12

COUNTS = [4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]


def main():
    print(f"kernel: Gaussian truncated at +/-{TRUNC:.0f} sigma, renormalized")
    print("integration axis fixed to y in every case (controlled comparison)")
    print()

    print("=== 1) Is max|F''| on the window bounded? ===")
    print("|F''| evaluated at shrinking distance from the window start")
    print()
    header = f"{'case':42}{'window':>18}" + "".join(
        f"{f'd=1e-{k}':>12}" for k in (2, 3, 4, 5))
    print(header)
    print("-" * len(header))
    for name, px, py, half_x, half_y, r in CASES:
        start, end, vals = probe_second_derivative(px, py, half_x, half_y, r)
        row = f"{name:42}{f'[{start:.2f}, {end:.2f}]':>18}"
        for v in vals:
            row += f"{v:12.3e}"
        print(row)
    print()

    print("=== 2) Observed convergence order ===")
    print("d(N) = |Q(N) - Q(2N)| in 8-bit levels; p fitted from adjacent d(N)")
    print()
    header = (f"{'case':42}" + "".join(f"{f'd({n})':>11}" for n in (4, 16, 64, 256))
              + f"{'p (large N)':>13}")
    print(header)
    print("-" * len(header))
    for name, px, py, half_x, half_y, r in CASES:
        values, diffs, fitted = convergence_order(px, py, half_x, half_y, r, COUNTS)
        table = dict(diffs)
        row = f"{name:42}"
        for n in (4, 16, 64, 256):
            row += f"{table[n] * 255.0:11.2e}"
        row += f"{fitted[-1][1]:13.2f}" if fitted else f"{'exact':>13}"
        print(row)
    print()

    print("=== 3) Fitted order across the whole N sweep ===")
    print()
    for name, px, py, half_x, half_y, r in CASES:
        values, diffs, fitted = convergence_order(px, py, half_x, half_y, r, COUNTS)
        if not fitted:
            print(f"{name}: integrand constant over the window, error is exactly 0")
            continue
        pairs = " ".join(f"{n}->{2 * n}:{p:.2f}" for n, p in fitted)
        print(f"{name}\n    {pairs}")


if __name__ == "__main__":
    main()
