#!/usr/bin/env python3
"""Two follow-up studies for the analytic shadow coverage shader.

Study A: erf approximations usable in GLSL (no built-in erf outside MSL).
  Compares candidate approximations against math.erf, and propagates the error
  into a CDF difference (which is what the shader actually consumes).

Study B: continuous closed form vs the discrete convolution the existing
  GaussianBlur1D filter performs. The existing filter sums integer-offset taps
  weighted by exp(-i^2/2sigma^2) and renormalizes, so at small sigma it is not
  the same operator as the continuous integral. This quantifies how different
  the fast path will look from the fallback path.

Errors are reported in 8-bit levels (1/255).
"""

import math

TRUNC = 2.0

NORM = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
                                   - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0))))


def erf_winitzki(x):
    """Single-expression approximation, needs one exp. Claimed max err ~1.3e-3."""
    pi = math.pi
    a = 8.0 * (pi - 3.0) / (3.0 * pi * (4.0 - pi))
    xx = x * x
    val = 1.0 - math.exp(-xx * (4.0 / pi + a * xx) / (1.0 + a * xx))
    return math.copysign(math.sqrt(val), x)


def erf_abramowitz_7_1_26(x):
    """Abramowitz & Stegun 7.1.26: one exp plus a degree-5 polynomial in t.
    Claimed max err ~1.5e-7."""
    a1, a2, a3 = 0.254829592, -0.284496736, 1.421413741
    a4, a5, p = -1.453152027, 1.061405429, 0.3275911
    sign = 1.0 if x >= 0.0 else -1.0
    ax = abs(x)
    t = 1.0 / (1.0 + p * ax)
    poly = t * (a1 + t * (a2 + t * (a3 + t * (a4 + t * a5))))
    return sign * (1.0 - poly * math.exp(-ax * ax))


def erf_hastings_tanh(x):
    """Cheap tanh-based approximation, no exp on most GPUs (tanh may lower to
    exp anyway). Included only as a lower bound on cost."""
    return math.tanh(1.1283791671 * x + 0.0889946 * x * x * x)


APPROXIMATIONS = [
    ("winitzki", erf_winitzki),
    ("A&S 7.1.26", erf_abramowitz_7_1_26),
    ("tanh", erf_hastings_tanh),
]


def truncated_cdf(u, erf_fn):
    """CDF of the truncated normalized Gaussian, built from an erf function."""
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    root2 = math.sqrt(2.0)
    lo = 0.5 * (1.0 + erf_fn(-TRUNC / root2))
    cur = 0.5 * (1.0 + erf_fn(u / root2))
    return (cur - lo) * math.sqrt(2.0 * math.pi) / NORM


def study_a():
    print("Study A: erf approximations")
    print()
    print("  raw erf error and resulting error of a CDF difference")
    print("  (CDF difference = what one row_span evaluation computes)")
    print()
    header = f"{'approximation':>14}{'max |erf err|':>16}{'CDF diff err':>16}"
    print(header)
    print("-" * len(header))

    steps = 2001
    for name, fn in APPROXIMATIONS:
        raw = 0.0
        for i in range(steps):
            x = -4.0 + 8.0 * i / (steps - 1)
            raw = max(raw, abs(fn(x) - math.erf(x)))
        # Worst CDF difference error over pairs of arguments in the support.
        span = 0.0
        grid = [-TRUNC + 2.0 * TRUNC * i / 200 for i in range(201)]
        exact = [truncated_cdf(u, math.erf) for u in grid]
        approx = [truncated_cdf(u, fn) for u in grid]
        for i in range(len(grid)):
            for j in range(len(grid)):
                d_exact = exact[i] - exact[j]
                d_approx = approx[i] - approx[j]
                span = max(span, abs(d_approx - d_exact))
        print(f"{name:>14}{raw:16.2e}{span * 255.0:14.3f} lv")
    print()


def discrete_edge(x, sigma):
    """Existing filter's operator applied to a 1D step edge (interior on the
    left). Discrete taps at integer offsets, truncated and renormalized, and
    each tap reads a hard-edged mask."""
    radius = int(math.ceil(2.0 * sigma))
    total = 0.0
    acc = 0.0
    for i in range(-radius, radius + 1):
        w = math.exp(-float(i * i) / (2.0 * sigma * sigma))
        total += w
        # Sample position x + i; mask is 1 where position <= 0.
        if x + i <= 0.0:
            acc += w
    return acc / total


def continuous_edge(x, sigma):
    """Closed-form operator applied to the same 1D step edge."""
    u = -x / sigma
    return truncated_cdf(u, math.erf)


def study_b():
    print("Study B: continuous closed form vs the existing discrete filter")
    print()
    print("  1D step edge, error in 8-bit levels over x in [-3sigma, 3sigma]")
    print()
    header = f"{'sigma':>8}{'max err':>12}{'mean err':>12}"
    print(header)
    print("-" * len(header))
    for sigma in [1.0, 2.0, 3.0, 5.0, 10.0, 20.0, 40.0]:
        worst = 0.0
        total = 0.0
        steps = 601
        for i in range(steps):
            x = (-3.0 + 6.0 * i / (steps - 1)) * sigma
            err = abs(continuous_edge(x, sigma) - discrete_edge(x, sigma))
            worst = max(worst, err)
            total += err
        print(f"{sigma:8.1f}{worst * 255.0:10.2f} lv{total / steps * 255.0:10.2f} lv")
    print()


if __name__ == "__main__":
    study_a()
    study_b()
