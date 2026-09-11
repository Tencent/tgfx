#!/usr/bin/env python3
"""Compare the erf approximation Evan Wallace uses against A&S 7.1.26.

Wallace's shader (madebyevan.com/shaders/fast-rounded-rectangle-shadows/) does
contain an erf approximation:

    vec4 erf(vec4 x) {
      vec4 s = sign(x), a = abs(x);
      x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
      x *= x;
      return s - s / (x * x);
    }

Expanding the Horner form gives 1 + 0.278393a + 0.230389a^2 + 0.078108a^4,
then erf ~= sign(x) * (1 - 1/denom^4). That is Abramowitz & Stegun 7.1.27
    erf x = 1 - 1/[1 + a1 x + a2 x^2 + a3 x^3 + a4 x^4]^4,  |eps| <= 5e-4
    a1=.278393  a2=.230389  a3=.000972  a4=.078108
with the a3 term (0.000972, the smallest coefficient) dropped.

So the two shaders use different formulas from the same A&S family:
  7.1.26 -> needs exp, error 1.5e-7
  7.1.27 -> no exp at all, error 5e-4
This script quantifies both, including the a3-dropped variant Wallace ships,
and propagates the error into the quantity the shader consumes (a CDF
difference), reported in 8-bit levels.
"""

import math

TRUNC = 2.0
NORM = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
                                   - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0))))


def erf_as_7_1_26(x):
    """A&S 7.1.26: needs one exp. Published |eps| <= 1.5e-7."""
    ax = abs(x)
    t = 1.0 / (1.0 + 0.3275911 * ax)
    poly = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 +
                t * (-1.453152027 + t * 1.061405429))))
    positive = 1.0 - poly * math.exp(-ax * ax)
    return positive if x >= 0.0 else -positive


def erf_as_7_1_27(x):
    """A&S 7.1.27 as published, all four coefficients. No exp.
    Published |eps| <= 5e-4."""
    ax = abs(x)
    d = 1.0 + ax * (0.278393 + ax * (0.230389 + ax * (0.000972 + ax * 0.078108)))
    d2 = d * d
    positive = 1.0 - 1.0 / (d2 * d2)
    return positive if x >= 0.0 else -positive


def erf_wallace(x):
    """Exactly what Wallace's shader computes: 7.1.27 with the a3 term dropped."""
    a = abs(x)
    d = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a
    d2 = d * d
    positive = 1.0 - 1.0 / (d2 * d2)
    return positive if x >= 0.0 else -positive


CANDIDATES = [
    ("A&S 7.1.26", erf_as_7_1_26, "1 exp + 1 div + 5 mul/add"),
    ("A&S 7.1.27", erf_as_7_1_27, "1 div + 4 mul/add, no exp"),
    ("Wallace (7.1.27, a3=0)", erf_wallace, "1 div + 3 mul/add, no exp"),
]


def truncated_cdf(u, erf_fn):
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    root2 = math.sqrt(2.0)
    lo = 0.5 * (1.0 + erf_fn(-TRUNC / root2))
    cur = 0.5 * (1.0 + erf_fn(u / root2))
    return (cur - lo) * math.sqrt(2.0 * math.pi) / NORM


def plain_cdf(u, erf_fn):
    """Untruncated normal CDF, which is what Wallace's own shader uses."""
    return 0.5 + 0.5 * erf_fn(u / math.sqrt(2.0))


def main():
    print("erf approximations found in the two reference implementations")
    print("errors in 8-bit levels (1/255) where noted; tolerance 1/255 = 3.92e-03")
    print()

    # Raw erf error over the full range, and over the bounded range a
    # sigma-normalized shader actually reaches (|z| <= 2/sqrt(2)).
    z_max = TRUNC / math.sqrt(2.0)
    header = (f"{'approximation':>24}{'max|err| all':>15}{'max|err| |z|<=1.41':>20}"
              f"{'cost':>28}")
    print(header)
    print("-" * len(header))
    for name, fn, cost in CANDIDATES:
        wide = 0.0
        near = 0.0
        steps = 100001
        for i in range(steps):
            x = -6.0 + 12.0 * i / (steps - 1)
            e = abs(fn(x) - math.erf(x))
            wide = max(wide, e)
            if abs(x) <= z_max:
                near = max(near, e)
        print(f"{name:>24}{wide:15.3e}{near:20.3e}{cost:>28}")
    print()

    # Error of the consumed quantity: a difference of two CDF values.
    print("propagated into a CDF difference (what one rowSpan evaluation is):")
    print()
    header2 = f"{'approximation':>24}{'truncated CDF':>18}{'plain CDF':>14}"
    print(header2)
    print("-" * len(header2))
    grid = [-TRUNC + 2.0 * TRUNC * i / 200 for i in range(201)]
    for name, fn, _ in CANDIDATES:
        worst_t = 0.0
        worst_p = 0.0
        exact_t = [truncated_cdf(u, math.erf) for u in grid]
        appr_t = [truncated_cdf(u, fn) for u in grid]
        exact_p = [plain_cdf(u, math.erf) for u in grid]
        appr_p = [plain_cdf(u, fn) for u in grid]
        for i in range(len(grid)):
            for j in range(len(grid)):
                worst_t = max(worst_t, abs((appr_t[i] - appr_t[j])
                                           - (exact_t[i] - exact_t[j])))
                worst_p = max(worst_p, abs((appr_p[i] - appr_p[j])
                                           - (exact_p[i] - exact_p[j])))
        print(f"{name:>24}{worst_t * 255.0:15.3f} lv{worst_p * 255.0:11.3f} lv")
    print()
    print("Note: the truncated CDF divides by the window mass (~0.9545), which")
    print("amplifies the raw erf error slightly compared to the plain CDF.")


if __name__ == "__main__":
    main()
