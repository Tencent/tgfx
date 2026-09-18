#!/usr/bin/env python3
"""Re-derive the quadrature-count dispatch predicate from the measured error surface.

The predicate currently in the design doc is `q1 > 0.75 or q2 > 2` with

  q1 = max(rx / halfX, ry / halfY)
  q2 = max(rx / ry, ry / rx)

q1 carries the half sizes, but a spot check suggested the half sizes barely move
the error once the corner semi-axes are fixed. This script measures the error
surface directly over the normalized corner semi-axes (rx, ry) and reports:

  A) how much the half sizes move the error with the corner held fixed,
  B) the N=4 / N=8 error surfaces over the (rx, ry) grid,
  C) candidate predicates scored the same way as quadrature_dispatch_search.py:
     every shape routed to N=4 must stay at or below the worst error that a
     fixed N=8 already produces somewhere in the space.

Everything runs in the sigma-normalized space (sigma = 1 on both axes).
Reference = high-resolution midpoint quadrature of the same reduced form.
All errors in 8-bit levels (1/255).
"""

import math

from quadrature_ratio_threshold_study import coverage, probe_points

REFERENCE_N = 400
PROBE_STEPS = 41
CORNER_PROBE_STEP = 0.2
CORNERS = (0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 10.0)
HALVES = ((10.0, 10.0), (4.0, 4.0), (10.0, 4.0))


def corner_window_error(half_x, half_y, rx, ry, n_quad):
    """Worst error over a fixed-spacing window around one corner arc.

    The window size follows the corner and the kernel reach only, so shapes of
    different half sizes are sampled at the same density and stay comparable.
    """
    x0, x1 = half_x - rx - 2.2, half_x + 2.2
    y0, y1 = half_y - ry - 2.2, half_y + 2.2
    steps_x = int((x1 - x0) / CORNER_PROBE_STEP) + 1
    steps_y = int((y1 - y0) / CORNER_PROBE_STEP) + 1
    worst = 0.0
    for i in range(steps_x):
        px = x0 + i * CORNER_PROBE_STEP
        for j in range(steps_y):
            py = y0 + j * CORNER_PROBE_STEP
            ref = coverage(px, py, half_x, half_y, rx, ry, REFERENCE_N)
            e = abs(coverage(px, py, half_x, half_y, rx, ry, n_quad) - ref) * 255.0
            if e > worst:
                worst = e
    return worst


def worst_error(half_x, half_y, rx, ry, counts):
    pts = probe_points(half_x, half_y, PROBE_STEPS)
    errs = {n: 0.0 for n in counts}
    for px, py in pts:
        ref = coverage(px, py, half_x, half_y, rx, ry, REFERENCE_N)
        for n in counts:
            e = abs(coverage(px, py, half_x, half_y, rx, ry, n) - ref) * 255.0
            if e > errs[n]:
                errs[n] = e
    return errs


def stage_a():
    print("A) corner fixed, half size varied -- does the half size move the error?")
    print(f"   worst N=4 error over a fixed-spacing ({CORNER_PROBE_STEP}) window"
          " around one corner arc")
    header = f"{'corner':>16s}" + "".join(f"{'half ' + str(int(h)):>11s}"
                                          for h in (2, 4, 8, 16, 32))
    print(header)
    print("-" * len(header))
    for rx, ry in ((0.5, 0.5), (1.0, 1.0), (2.0, 2.0), (2.0, 1.0), (2.0, 0.5)):
        cells = []
        for h in (2.0, 4.0, 8.0, 16.0, 32.0):
            if rx > h or ry > h:
                cells.append(f"{'-':>11s}")
                continue
            cells.append(f"{corner_window_error(h, h, rx, ry, 4):11.2f}")
        print(f"     ({rx:4.2f},{ry:4.2f})" + "".join(cells))
    print()


def scan():
    records = []
    for half_x, half_y in HALVES:
        for rx in CORNERS:
            if rx > half_x:
                continue
            for ry in CORNERS:
                if ry > half_y:
                    continue
                errs = worst_error(half_x, half_y, rx, ry, (4, 8))
                records.append((half_x, half_y, rx, ry, errs[4], errs[8]))
    return records


def stage_b(records):
    print("B) error surface over the normalized corner semi-axes")
    for half_x, half_y in HALVES:
        for n, col in ((4, 4), (8, 5)):
            print(f"   half=({half_x:.0f},{half_y:.0f})  N={n}   rows: rx, cols: ry")
            head = f"{'rx\\ry':>8s}" + "".join(f"{c:8.2f}" for c in CORNERS
                                              if c <= half_y)
            print(head)
            for rx in CORNERS:
                if rx > half_x:
                    continue
                cells = []
                for ry in CORNERS:
                    if ry > half_y:
                        continue
                    hit = [r for r in records
                           if r[0] == half_x and r[1] == half_y
                           and r[2] == rx and r[3] == ry]
                    cells.append(f"{hit[0][col]:8.2f}" if hit else f"{'-':>8s}")
                print(f"{rx:8.2f}" + "".join(cells))
            print()


def predicates():
    """(name, function of (half_x, half_y, rx, ry) -> True when N=8 is needed)."""
    out = [("current: q1>0.75 or q2>2",
            lambda hx, hy, rx, ry: max(rx / hx, ry / hy) > 0.75
            or max(rx / ry, ry / rx) > 2.0)]
    for t in (1.5, 2.0, 2.5, 3.0):
        out.append((f"ratio only: q2>{t}",
                    lambda hx, hy, rx, ry, t=t: max(rx / ry, ry / rx) > t))
    for t in (0.5, 1.0, 1.5, 2.0, 3.0):
        out.append((f"small semi-axis: min(rx,ry)<{t}",
                    lambda hx, hy, rx, ry, t=t: min(rx, ry) < t))
    for t2 in (1.5, 2.0, 3.0):
        for t3 in (0.5, 1.0, 2.0):
            out.append((f"q2>{t2} or min(rx,ry)<{t3}",
                        lambda hx, hy, rx, ry, t2=t2, t3=t3:
                        max(rx / ry, ry / rx) > t2 or min(rx, ry) < t3))
    return out


def stage_c(records):
    fixed8 = max(r[5] for r in records)
    fixed4 = max(r[4] for r in records)
    print("C) dispatch predicates")
    print(f"   shapes measured: {len(records)}")
    print(f"   fixed N=8 worst over the space: {fixed8:.2f}")
    print(f"   fixed N=4 worst over the space: {fixed4:.2f}")
    print(f"   pass criterion: the N=4 bucket must stay <= {fixed8:.2f}")
    print()
    header = f"{'predicate':>34s}{'N=4 share':>11s}{'N=4 worst':>11s}{'verdict':>9s}"
    print(header)
    print("-" * len(header))
    for name, fn in predicates():
        low = [r for r in records if not fn(r[0], r[1], r[2], r[3])]
        worst = max((r[4] for r in low), default=0.0)
        share = 100.0 * len(low) / len(records)
        verdict = "ok" if worst <= fixed8 else "fails"
        print(f"{name:>34s}{share:10.1f}%{worst:11.2f}{verdict:>9s}")
    print()
    print("   shapes whose N=4 error exceeds the fixed-N=8 bound:")
    bad = sorted((r for r in records if r[4] > fixed8), key=lambda r: -r[4])
    print(f"{'half':>14s}{'corner':>16s}{'q1':>7s}{'q2':>7s}{'N=4':>8s}{'N=8':>8s}")
    for hx, hy, rx, ry, e4, e8 in bad[:20]:
        q1 = max(rx / hx, ry / hy)
        q2 = max(rx / ry, ry / rx)
        print(f"  ({hx:4.1f},{hy:4.1f}) ({rx:6.2f},{ry:6.2f}){q1:7.2f}{q2:7.2f}"
              f"{e4:8.2f}{e8:8.2f}")
    print(f"  ... {len(bad)} of {len(records)} shapes exceed the bound at N=4")


def main():
    print("Quadrature dispatch rescan")
    print(f"kernel truncated at +/-2 sigma; reference N={REFERENCE_N};"
          f" {PROBE_STEPS}x{PROBE_STEPS} probe grid; errors in 8-bit levels")
    print()
    stage_a()
    records = scan()
    stage_b(records)
    stage_c(records)


if __name__ == "__main__":
    main()
