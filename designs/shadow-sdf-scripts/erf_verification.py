#!/usr/bin/env python3
"""Verify the erf approximation used by the shadow shader, and check whether a
cheaper alternative exists on the bounded domain the shader actually needs.

Part 1 verifies Abramowitz & Stegun 7.1.26 against its published error bound
(|error| <= 1.5e-7 for x >= 0).

Part 2 asks a question the published bound does not answer: the shader only ever
evaluates erf on a bounded interval, because coordinates are normalized by sigma
and the kernel is truncated at +/-2 sigma. So the argument of erf is
u/sqrt(2) with u in [-2, 2], i.e. |z| <= sqrt(2). On a bounded interval a plain
minimax polynomial can be both cheaper (no exp call) and more accurate. This
part fits one and compares.

Errors are absolute; the 8-bit visual tolerance is 1/255 ~= 3.9e-3.
"""

import math

TRUNC = 2.0
Z_MAX = TRUNC / math.sqrt(2.0)  # largest |argument| of erf the shader reaches


def erf_as_7_1_26(x):
    """Abramowitz & Stegun 7.1.26. Published bound: |error| <= 1.5e-7, x >= 0.
    Extended to negative x by oddness."""
    ax = abs(x)
    t = 1.0 / (1.0 + 0.3275911 * ax)
    poly = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 +
                t * (-1.453152027 + t * 1.061405429))))
    positive = 1.0 - poly * math.exp(-ax * ax)
    return positive if x >= 0.0 else -positive


def max_error(fn, lo, hi, steps=200001):
    worst = 0.0
    at = lo
    for i in range(steps):
        x = lo + (hi - lo) * i / (steps - 1)
        err = abs(fn(x) - math.erf(x))
        if err > worst:
            worst = err
            at = x
    return worst, at


def part1():
    print("Part 1: verify A&S 7.1.26 against its published bound")
    print()
    print("  published: |error| <= 1.5e-7 for x >= 0")
    print()
    for lo, hi, label in [
        (0.0, Z_MAX, f"[0, {Z_MAX:.4f}]  <- domain the shader uses"),
        (0.0, 3.0, "[0, 3]"),
        (0.0, 6.0, "[0, 6]"),
        (0.0, 30.0, "[0, 30]"),
    ]:
        worst, at = max_error(erf_as_7_1_26, lo, hi)
        verdict = "within bound" if worst <= 1.5e-7 else "EXCEEDS BOUND"
        print(f"  {label:42} max err {worst:.3e} at x={at:.4f}  ({verdict})")
    # Oddness must hold exactly, since the shader relies on it for negative u.
    asym = max(abs(erf_as_7_1_26(-x) + erf_as_7_1_26(x))
               for x in [i * 0.01 for i in range(1, 301)])
    print(f"  oddness residual max |f(-x)+f(x)|: {asym:.3e}")
    print()


def chebyshev_fit(fn, lo, hi, degree):
    """Least-squares fit in the Chebyshev basis on [lo, hi], evaluated at
    Chebyshev nodes. Close to minimax for smooth functions, and enough to
    judge whether this route is viable."""
    n = degree + 1
    nodes = [math.cos(math.pi * (k + 0.5) / n) for k in range(n)]  # in [-1,1]
    mid, half = 0.5 * (lo + hi), 0.5 * (hi - lo)
    # Chebyshev coefficients via the discrete cosine transform.
    coeffs = []
    for j in range(n):
        s = 0.0
        for k, t in enumerate(nodes):
            s += fn(mid + half * t) * math.cos(math.pi * j * (k + 0.5) / n)
        coeffs.append((2.0 / n) * s)
    coeffs[0] *= 0.5

    def evaluate(x):
        t = (x - mid) / half
        b0, b1 = 0.0, 0.0
        for c in reversed(coeffs[1:]):
            b0, b1 = 2.0 * t * b0 - b1 + c, b0
        return t * b0 - b1 + coeffs[0]

    return evaluate, coeffs


def part2():
    print("Part 2: is a plain polynomial on the bounded domain better?")
    print()
    print(f"  target: erf(z) for z in [0, {Z_MAX:.6f}] (= 2/sqrt(2))")
    print("  a polynomial here needs no exp call at all")
    print()
    header = f"{'degree':>8}{'max err':>14}{'ops (mul+add)':>16}"
    print(header)
    print("-" * len(header))
    for degree in [5, 6, 7, 8, 9, 11]:
        fit, _ = chebyshev_fit(math.erf, 0.0, Z_MAX, degree)
        worst, _ = max_error(fit, 0.0, Z_MAX, steps=20001)
        # Horner on an odd function: erf(z) = z * P(z^2), so degree d in z
        # costs about (d-1)/2 fused steps. Report the plain Horner count.
        print(f"{degree:8d}{worst:14.3e}{degree:16d}")
    print()
    print("  For comparison, A&S 7.1.26 on the same interval:")
    worst, _ = max_error(erf_as_7_1_26, 0.0, Z_MAX)
    print(f"    max err {worst:.3e}, cost 1 exp + 1 divide + 5 mul/add")
    print()
    print("  8-bit tolerance for reference: 1/255 = 3.92e-03")


if __name__ == "__main__":
    part1()
    part2()
