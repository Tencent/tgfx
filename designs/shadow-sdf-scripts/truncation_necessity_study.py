#!/usr/bin/env python3
"""Is the truncated/renormalized CDF actually necessary, or would the plain
Gaussian CDF (as Evan Wallace uses) do?

The inner integral is a definite integral, so a plain CDF difference does give
its exact value for an untruncated Gaussian. The question is whether an
untruncated Gaussian is the right kernel here, given that the draw region is
finite.

Part 1: what does coverage look like right at the draw boundary? TGFX's
filterBounds outsets by 2*sigma. Whatever coverage remains at that distance gets
clipped away by the quad edge, which shows up as a hard cut.

Part 2: algebraic simplification. The truncated CDF as first written does a
subtract and a divide on top of the erf call. Check whether it collapses into
the same shape (and cost) as Wallace's `0.5 + 0.5*erf(...)`.

Coverage reported in 8-bit levels (1/255 ~= 3.92e-3 is the visibility floor).
"""

import math

SQRT1_2 = 1.0 / math.sqrt(2.0)


def phi(u):
    """Plain standard-normal CDF (sigma = 1). What Wallace's shader uses."""
    return 0.5 * (1.0 + math.erf(u * SQRT1_2))


def phi_truncated(u, k=2.0):
    """Gaussian truncated to [-k, k] and renormalized over that window."""
    if u <= -k:
        return 0.0
    if u >= k:
        return 1.0
    low = phi(-k)
    return (phi(u) - low) / (1.0 - 2.0 * low)


def brute_force_coverage(d, sigma=1.0, reach=12.0, n=400001):
    """Coverage at distance d outside a straight edge, by direct numerical
    convolution rather than the closed form.

    The integration variable q is the absolute coordinate of the sampled point,
    with the origin on the shape boundary. The shape occupies q <= 0, so the
    limits come from the indicator function's support and do NOT involve d;
    d appears only inside the kernel argument g(d - q). Integrating to d instead
    of to 0 would model a shape whose boundary follows the query point, and
    yields a constant 0.5 for every d (see part0)."""
    lo = min(d - reach * sigma, -reach * sigma)
    hi = 0.0
    if hi <= lo:
        return 0.0
    step = (hi - lo) / n
    total = 0.0
    norm = 1.0 / (sigma * math.sqrt(2.0 * math.pi))
    for i in range(n):
        q = lo + (i + 0.5) * step
        t = (d - q) / sigma
        total += math.exp(-t * t / 2.0) * norm * step
    return total


def brute_force_shifted(d, sigma=1.0, reach=12.0, n=400001):
    """Same quantity after substituting t = d - q, which moves the origin to the
    kernel centre. Now d does appear as a limit: integral of g(t) over
    t in [d, +inf). Confirms the two forms agree."""
    lo = d
    hi = d + reach * sigma
    step = (hi - lo) / n
    total = 0.0
    norm = 1.0 / (sigma * math.sqrt(2.0 * math.pi))
    for i in range(n):
        t = (lo + (i + 0.5) * step) / sigma
        total += math.exp(-t * t / 2.0) * norm * step
    return total


def wrong_limits(d, sigma=1.0, reach=12.0, n=400001):
    """Integrating q from -inf to d instead of to 0. Shown only to demonstrate
    that it is d-independent and therefore not a coverage."""
    lo = min(d - reach * sigma, -reach * sigma)
    hi = d
    if hi <= lo:
        return 0.0
    step = (hi - lo) / n
    total = 0.0
    norm = 1.0 / (sigma * math.sqrt(2.0 * math.pi))
    for i in range(n):
        q = lo + (i + 0.5) * step
        t = (d - q) / sigma
        total += math.exp(-t * t / 2.0) * norm * step
    return total


def part0():
    print("Part 0: confirm the closed form and the choice of integration limits")
    print()
    print("  Straight edge, shape on the q <= 0 side, kernel centred at d.")
    print("  q is an absolute coordinate, so the limits follow the shape's")
    print("  support, not the query point:")
    print("    coverage(d) = int_{-inf}^{0} g(d-q) dq = int_{d}^{inf} g(t) dt = Phi(-d)")
    print()
    header = (f"{'d':>7}{'q -> 0 (correct)':>20}{'t from d (same)':>18}"
              f"{'Phi(-d)':>13}{'q -> d (wrong)':>17}")
    print(header)
    print("-" * len(header))
    for d in [0.0, 1.0, 2.0, 3.0]:
        a = brute_force_coverage(d)
        b = brute_force_shifted(d)
        c = phi(-d)
        w = wrong_limits(d)
        print(f"{d:6.1f}s{a:20.10f}{b:18.10f}{c:13.10f}{w:17.10f}")
    print()
    print("  The last column is 0.5 for every d: integrating to d measures the")
    print("  mass on one side of the kernel centre, which by symmetry is always")
    print("  half, and says nothing about where the shape is.")
    print()


def part1():
    print("Part 1: leftover coverage at the draw boundary")
    print()
    print("  The quad ends at distance k*sigma from the edge (TGFX's")
    print("  filterBounds outsets by 2*sigma). Whatever coverage remains there")
    print("  is clipped to zero by the quad edge, i.e. a hard step.")
    print()
    header = f"{'distance':>12}{'plain CDF':>14}{'in 8-bit lv':>14}{'truncated k=2':>16}"
    print(header)
    print("-" * len(header))
    for d in [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0]:
        p = phi(-d)          # coverage d*sigma outside the edge
        t = phi_truncated(-d)
        print(f"{d:10.1f}s{p:14.6f}{p * 255.0:14.2f}{t:16.6f}")
    print()
    print("  At the 2-sigma quad edge the plain Gaussian still carries")
    print(f"  {phi(-2.0) * 255.0:.2f}/255 of coverage -> clipped to 0 by the quad,")
    print("  i.e. a visible hard step. The truncated kernel reaches exactly 0")
    print("  there, so nothing is clipped.")
    print()
    print("  To hide the step with a plain Gaussian, the quad must reach where")
    print("  coverage falls below ~0.5/255:")
    for d in [2.0, 2.5, 3.0, 3.5]:
        lv = phi(-d) * 255.0
        verdict = "visible" if lv > 0.5 else "below 1/2 level"
        print(f"    {d:.1f} sigma -> {lv:6.2f} lv   ({verdict})")
    print()


def part2():
    print("Part 2: does the truncated CDF simplify?")
    print()
    k = 2.0
    low = phi(-k)
    mass = 1.0 - 2.0 * low
    print(f"  LOW  = Phi(-2)        = {low:.8f}")
    print(f"  MASS = Phi(2)-Phi(-2) = {mass:.8f}")
    print()
    print("  Expand (0.5*(1+erf(u/sqrt2)) - LOW)/MASS")
    print("       = (0.5 - LOW)/MASS + (0.5/MASS)*erf(u/sqrt2)")
    a = (0.5 - low) / mass
    b = 0.5 / mass
    print(f"    offset (0.5-LOW)/MASS = {a:.10f}")
    print(f"    scale       0.5/MASS  = {b:.10f}")
    print()
    print("  The offset is exactly 0.5, because MASS = 1 - 2*LOW implies")
    print("  0.5 - LOW = MASS/2. So the whole thing is one multiply-add:")
    print(f"    truncatedCDF(u) = clamp(0.5 + {b:.8f} * erf(u/sqrt2), 0, 1)")
    print("  which is the same cost as Wallace's 0.5 + 0.5*erf(...).")
    print()

    # Verify the collapsed form against the branchy original.
    worst = 0.0
    for i in range(200001):
        u = -4.0 + 8.0 * i / 200000
        original = phi_truncated(u, k)
        collapsed = min(max(0.5 + b * math.erf(u * SQRT1_2), 0.0), 1.0)
        worst = max(worst, abs(original - collapsed))
    print(f"  max |collapsed - original| over u in [-4,4]: {worst:.3e}")

    # Endpoints must be exact, otherwise the boundary is not truly zero.
    for u in (-2.0, 2.0):
        collapsed = min(max(0.5 + b * math.erf(u * SQRT1_2), 0.0), 1.0)
        print(f"  collapsed({u:+.1f}) = {collapsed:.10f}")
    print()
    print("  Note the clamp is still required: past +/-2 the erf keeps")
    print(f"  approaching +/-1, giving {0.5 + b:.5f} / {0.5 - b:.5f} unclamped.")


if __name__ == "__main__":
    part0()
    part1()
    part2()
