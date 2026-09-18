#!/usr/bin/env python3
"""How different will the fast path look from the existing filter path?

Study B in erf_and_kernel_study.py compared the closed form against the
existing discrete filter reading an ideal hard edge, and found large gaps at
small sigma. But the real shadow source is not an ideal hard edge:
  - spread != 0: SpreadUtils rasterizes with antiAlias, giving a ~1px linear ramp
  - spread == 0: OpaqueContext records a hard-edged silhouette
and the filter runs twice (horizontal then vertical) on that raster.

This script re-runs the comparison with a 1px linear ramp source, which is the
spread != 0 case, to see how much of the gap is an artifact of the ideal-edge
assumption. It also reports where the gap comes from: tap quantization
(integer offsets) versus source representation.

Errors in 8-bit levels (1/255).
"""

import math

TRUNC = 2.0

NORM = math.sqrt(2.0 * math.pi) * (0.5 * (1.0 + math.erf(TRUNC / math.sqrt(2.0)))
                                   - 0.5 * (1.0 + math.erf(-TRUNC / math.sqrt(2.0))))


def truncated_cdf(u):
    if u <= -TRUNC:
        return 0.0
    if u >= TRUNC:
        return 1.0
    root2 = math.sqrt(2.0)
    lo = 0.5 * (1.0 + math.erf(-TRUNC / root2))
    cur = 0.5 * (1.0 + math.erf(u / root2))
    return (cur - lo) * math.sqrt(2.0 * math.pi) / NORM


def source_hard(t):
    """Ideal hard edge: interior where t <= 0."""
    return 1.0 if t <= 0.0 else 0.0


def source_aa_ramp(t):
    """1px linear ramp centered on the boundary, matching the rasterizer AA."""
    return min(max(0.5 - t, 0.0), 1.0)


def discrete_filter(x, sigma, source):
    """The existing filter: integer-offset taps, truncated at ceil(2 sigma),
    exp weights, renormalized."""
    radius = int(math.ceil(2.0 * sigma))
    total = 0.0
    acc = 0.0
    for i in range(-radius, radius + 1):
        w = math.exp(-float(i * i) / (2.0 * sigma * sigma))
        total += w
        acc += source(x + i) * w
    return acc / total


def closed_form(x, sigma):
    """Continuous closed form of the same truncated kernel against a hard edge."""
    return truncated_cdf(-x / sigma)


def compare(sigma, source):
    worst = 0.0
    total = 0.0
    steps = 1201
    for i in range(steps):
        x = (-3.0 + 6.0 * i / (steps - 1)) * sigma
        err = abs(closed_form(x, sigma) - discrete_filter(x, sigma, source))
        worst = max(worst, err)
        total += err
    return worst * 255.0, total / steps * 255.0


def main():
    print("closed form vs existing discrete filter, 1D step edge")
    print("errors in 8-bit levels (1/255)")
    print()
    header = (f"{'sigma':>8}"
              f"{'hard max':>12}{'hard mean':>12}"
              f"{'AA max':>12}{'AA mean':>12}")
    print(header)
    print("-" * len(header))
    for sigma in [0.5, 1.0, 2.0, 3.0, 5.0, 10.0, 20.0, 40.0]:
        h_max, h_mean = compare(sigma, source_hard)
        a_max, a_mean = compare(sigma, source_aa_ramp)
        print(f"{sigma:8.1f}{h_max:12.2f}{h_mean:12.2f}{a_max:12.2f}{a_mean:12.2f}")
    print()
    print("Note: the closed form is the exact integral of the truncated kernel.")
    print("Where the columns disagree, the discrete filter is the approximation,")
    print("not the closed form: it point-samples the source at integer offsets,")
    print("which under-resolves the kernel when sigma is small.")


if __name__ == "__main__":
    main()
