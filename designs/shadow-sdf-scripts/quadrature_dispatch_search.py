#!/usr/bin/env python3
"""Search for a CPU-side predicate that could dispatch the quadrature count.

The idea under test: use N=8 only when the shape is "risky" and N=4 otherwise,
so the common case gets the cheaper shader. Two candidate quantities describe
the risk, both measured in the sigma-normalized space the shader works in:

  q1 = max(rx / halfX, ry / halfY)   how fully rounded the shape is
  q2 = max(rx / ry, ry / rx)         how much the two corner semi-axes differ

A predicate is only usable if every shape it routes to N=4 stays at or below
the error the current fixed N=8 already produces somewhere in the space. That
bound is what makes the dispatch a no-regression change, so it is the pass
criterion here rather than an absolute level count.

Reference = high-resolution midpoint quadrature of the same reduced form.
All errors in 8-bit levels (1/255).
"""

import math

from quadrature_ratio_threshold_study import coverage, probe_points

REFERENCE_N = 300
PROBE_STEPS = 7


def shape_space():
    """Normalized shapes covering aspect, corner size and corner ratio."""
    halves = (2.0, 4.0, 6.0, 10.0)
    fractions = (0.0, 0.25, 0.5, 0.75, 1.0)
    out = []
    for half_x in halves:
        for half_y in halves:
            for fx in fractions:
                for fy in fractions:
                    rx, ry = fx * half_x, fy * half_y
                    # A corner needs both semi-axes or neither.
                    if (rx > 0.0) != (ry > 0.0):
                        continue
                    out.append((half_x, half_y, rx, ry))
    return out


def measure(shape, counts):
    half_x, half_y, rx, ry = shape
    pts = probe_points(half_x, half_y, PROBE_STEPS)
    errs = {n: 0.0 for n in counts}
    for px, py in pts:
        ref = coverage(px, py, half_x, half_y, rx, ry, REFERENCE_N)
        for n in counts:
            e = abs(coverage(px, py, half_x, half_y, rx, ry, n) - ref) * 255.0
            if e > errs[n]:
                errs[n] = e
    return errs


def risk_quantities(shape):
    half_x, half_y, rx, ry = shape
    if rx <= 0.0 or ry <= 0.0:
        return 0.0, 1.0  # sharp corner: no arc, rowSpan is flat along the axis
    q1 = max(rx / half_x, ry / half_y)
    q2 = max(rx / ry, ry / rx)
    return q1, q2


def main():
    shapes = shape_space()
    records = []
    for shape in shapes:
        errs = measure(shape, (4, 8))
        q1, q2 = risk_quantities(shape)
        records.append((shape, q1, q2, errs[4], errs[8]))

    fixed8_worst = max(r[4] for r in records)
    fixed4_worst = max(r[3] for r in records)
    print(f"shapes measured: {len(records)}")
    print(f"fixed N=8 worst over the whole space: {fixed8_worst:.2f} levels")
    print(f"fixed N=4 worst over the whole space: {fixed4_worst:.2f} levels")
    print(f"pass criterion for a dispatch: N=4 region stays <= {fixed8_worst:.2f}")
    print()

    thresholds1 = (0.25, 0.5, 0.75, 0.9, 1.0)
    thresholds2 = (1.0, 1.25, 1.5, 2.0, 3.0)

    print("AND form: use N=8 only when q1 > t1 AND q2 > t2")
    header = f"{'t1':>6s}{'t2':>6s}{'N=4 share':>11s}{'N=4 worst':>11s}{'verdict':>10s}"
    print(header)
    print("-" * len(header))
    for t1 in thresholds1:
        for t2 in thresholds2:
            low = [r for r in records if not (r[1] > t1 and r[2] > t2)]
            worst = max((r[3] for r in low), default=0.0)
            share = 100.0 * len(low) / len(records)
            verdict = "ok" if worst <= fixed8_worst else "fails"
            print(f"{t1:6.2f}{t2:6.2f}{share:10.1f}%{worst:11.2f}{verdict:>10s}")

    print()
    print("OR form: use N=8 when q1 > t1 OR q2 > t2")
    print(header)
    print("-" * len(header))
    for t1 in thresholds1:
        for t2 in thresholds2:
            low = [r for r in records if not (r[1] > t1 or r[2] > t2)]
            worst = max((r[3] for r in low), default=0.0)
            share = 100.0 * len(low) / len(records)
            verdict = "ok" if worst <= fixed8_worst else "fails"
            print(f"{t1:6.2f}{t2:6.2f}{share:10.1f}%{worst:11.2f}{verdict:>10s}")

    print()
    print("Shapes where N=4 exceeds the fixed-N=8 bound, sorted by error:")
    bad = sorted((r for r in records if r[3] > fixed8_worst), key=lambda r: -r[3])
    print(f"{'half':>14s}{'corner':>16s}{'q1':>7s}{'q2':>7s}{'N=4':>8s}{'N=8':>8s}")
    for shape, q1, q2, e4, e8 in bad[:15]:
        hx, hy, rx, ry = shape
        print(f"  ({hx:4.1f},{hy:4.1f}) ({rx:6.2f},{ry:6.2f}){q1:7.2f}{q2:7.2f}{e4:8.2f}{e8:8.2f}")
    print(f"  ... {len(bad)} of {len(records)} shapes exceed the bound at N=4")


if __name__ == "__main__":
    main()
