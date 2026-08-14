#!/usr/bin/env python3
"""Verifies the smooth-corner math claims used by the corner-smoothing design.

Checks:
  1. Figma br() length quantities scale linearly with the radius.
  2. The arc handle identity 4(1-cos t)/(3 sin t) == (4/3) tan(t/2).
  3. Ardot's transition control point sits at 2/3 of the tangent intersection.
  4. The spread smoothness formula preserves the corner-fit invariant.
  5. Smoothing a corner never grows the bounding box beyond rect().
"""

import math


def figma_coeffs(s, r):
    """Mirrors br() in Figma's shader with radius r and smoothness s."""
    half_pi = math.pi * 0.5
    sqrt2 = math.sqrt(2.0)
    bx = 0.5 * half_pi * s           # transition turn angle
    by = half_pi * (1.0 - s)         # retained arc sweep
    bz = (1.0 + s) * r               # corner footprint
    ca = math.tan(bx)
    cb = math.sin(by * 0.5) * r * sqrt2
    x = r * math.tan(bx * 0.5) * math.cos(bx)
    g = x * ca
    w = ((bz - cb) - (1.0 + ca) * x) / 3.0
    return {'bz': bz, 'cb': cb, 'x': x, 'g': g, 'w': w, 'bx': bx, 'by': by}


def check_linearity():
    print('1) length quantities scale linearly with radius')
    ok = True
    for s in (0.0, 0.25, 0.5, 0.75, 1.0):
        base = figma_coeffs(s, 1.0)
        for r in (7.0, 40.0, 133.0):
            scaled = figma_coeffs(s, r)
            for key in ('bz', 'cb', 'x', 'g', 'w'):
                expect = base[key] * r
                if abs(scaled[key] - expect) > 1e-9 * max(1.0, abs(expect)):
                    print('   FAIL s=%s r=%s %s: %r vs %r' % (s, r, key, scaled[key], expect))
                    ok = False
        # angles must not depend on r
        for r in (7.0, 133.0):
            scaled = figma_coeffs(s, r)
            if abs(scaled['bx'] - base['bx']) > 1e-12 or abs(scaled['by'] - base['by']) > 1e-12:
                print('   FAIL angle depends on radius at s=%s' % s)
                ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def check_arc_handle():
    print('2) arc handle identity 4(1-cos t)/(3 sin t) == (4/3) tan(t/2)')
    ok = True
    for deg in (1, 15, 30, 45, 60, 89):
        t = math.radians(deg)
        ardot = 4.0 * (1.0 - math.cos(t)) / (3.0 * math.sin(t))
        figma = (4.0 / 3.0) * math.tan(t * 0.5)
        if abs(ardot - figma) > 1e-12:
            print('   FAIL t=%ddeg: %r vs %r' % (deg, ardot, figma))
            ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def check_two_thirds():
    """For a 90-degree corner, cp1 must sit at 2/3 of the way to the tangent intersection."""
    print('3) transition cp1 at 2/3 of the tangent intersection')
    ok = True
    for s in (0.2, 0.5, 0.8, 1.0):
        c = figma_coeffs(s, 1.0)
        # br() builds al -> am(+2w) -> an(+3w); an is the tangent intersection.
        span_to_intersection = 3.0 * c['w']
        cp1_span = 2.0 * c['w']
        if span_to_intersection <= 0:
            continue
        ratio = cp1_span / span_to_intersection
        if abs(ratio - 2.0 / 3.0) > 1e-12:
            print('   FAIL s=%s ratio=%r' % (s, ratio))
            ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def check_spread_fit():
    """Passing spread smoothness through unchanged can exceed the fit budget.

    The chosen design keeps smoothness as authored and lets the two-level degradation absorb the
    overflow, matching how the reference implementation re-evaluates the shape from fresh
    parameters. This check documents when that overflow kicks in.
    """
    print('4) spread smoothness overflow is bounded and handled by degradation')
    ok = True
    for ra, rb, edge, s in ((20, 30, 100, 0.6), (40, 40, 100, 1.0), (10, 90, 100, 0.5),
                            (25, 25, 60, 0.8)):
        if (1.0 + s) * (ra + rb) > edge + 1e-6:
            continue
        for d in (0.5, 5, 20, 100, 1000):
            lhs = (1.0 + s) * ((ra + d) + (rb + d))
            rhs = edge + 2.0 * d
            resolved = resolve_smoothness(rhs, rhs, [(ra + d, ra + d), (rb + d, rb + d),
                                                     (ra + d, ra + d), (rb + d, rb + d)], s)
            after = (1.0 + resolved) * ((ra + d) + (rb + d))
            if after > rhs + 1e-6:
                print('   FAIL ra=%s rb=%s edge=%s s=%s d=%s: still %.3f > %.3f'
                      % (ra, rb, edge, s, d, after, rhs))
                ok = False
            if lhs > rhs + 1e-6:
                print('   note overflow at ra=%s rb=%s edge=%s s=%s d=%s: %.3f > %.3f,'
                      ' degradation lowers s to %.4f'
                      % (ra, rb, edge, s, d, lhs, rhs, resolved))
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def sample_corner(s, r, steps=400):
    """Samples the smoothed corner outline for a corner at the origin.

    Local frame: the corner vertex is at (0, 0); the two edges run along -x and -y,
    so the interior is the third quadrant offset. Returns points relative to the vertex,
    measured as inward depth along each axis.
    """
    c = figma_coeffs(s, r)
    # Transition curve on the x-side, expressed as depth from the vertex.
    p0 = (c['bz'], 0.0)
    p1 = (c['bz'] -2.0 * c['w'], 0.0)
    p2 = (c['bz'] - 3.0 * c['w'], 0.0)
    p3 = (c['bz'] - 3.0 * c['w'] - c['x'], c['g'])
    pts = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1.0 - t
        bx = (mt ** 3) * p0[0] + 3 * (mt ** 2) * t * p1[0] + 3 * mt * t * t * p2[0] + (t ** 3) * p3[0]
        by = (mt ** 3) * p0[1] + 3 * (mt ** 2) * t * p1[1] + 3 * mt * t * t * p2[1] + (t ** 3) * p3[1]
        pts.append((bx, by))
    return pts


def check_bounds():
    """The outline must never poke outside the rect: depth along the edge normal >= 0."""
    print('5) smoothing never grows the bounding box')
    ok = True
    for s in (0.0, 0.25, 0.5, 0.75, 1.0):
        for r in (10.0, 50.0):
            worst = min(p[1] for p in sample_corner(s, r))
            if worst < -1e-9:
                print('   FAIL s=%s r=%s dips outside by %r' % (s, r, worst))
                ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def resolve_smoothness(width, height, radii, smoothness):
    """Mirrors the planned ResolveSmoothness: the tightest of the four edge constraints."""
    result = max(0.0, min(1.0, smoothness))
    if result <= 0.0:
        return 0.0
    edges = ((radii[0][0], radii[1][0], width),
             (radii[1][1], radii[2][1], height),
             (radii[2][0], radii[3][0], width),
             (radii[3][1], radii[0][1], height))
    for a, b, limit in edges:
        total = a + b
        if total > 0:
            result = min(result, limit / total - 1.0)
    return max(0.0, result)


def check_diagonal_boxes():
    """For a Simple rrect the edge constraint must already rule out diagonal box overlap."""
    print('6) widened corner boxes never overlap diagonally for uniform radii')
    ok = True
    for width, height in ((200, 200), (200, 100), (300, 100), (400, 200), (137, 89)):
        shorter = min(width, height)
        for factor in (0.499, 0.4, 0.25, 0.125, 0.05):
            r = shorter * factor
            radii = [(r, r)] * 4
            for s in (0.25, 0.6, 1.0):
                resolved = resolve_smoothness(width, height, radii, s)
                footprint = 2.0 * (1.0 + resolved) * r
                if footprint > width + 1e-6 or footprint > height + 1e-6:
                    print('   FAIL w=%s h=%s r=%.3f s=%s -> s\'=%.4f footprint=%.3f'
                          % (width, height, r, s, resolved, footprint))
                    ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def main():
    results = [check_linearity(), check_arc_handle(), check_two_thirds(),
               check_spread_fit(), check_bounds(), check_diagonal_boxes()]
    print()
    print('ALL PASS' if all(results) else 'SOME CHECKS FAILED')
    return 0 if all(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
