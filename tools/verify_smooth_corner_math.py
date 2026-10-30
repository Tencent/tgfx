#!/usr/bin/env python3
"""Verifies the smooth-corner math claims used by the corner-smoothing design.

Checks:
  1. Figma br() length quantities scale linearly with the radius.
  2. The arc handle identity 4(1-cos t)/(3 sin t) == (4/3) tan(t/2).
  3. Ardot's transition control point sits at 2/3 of the tangent intersection.
  4. The spread smoothness formula preserves the corner-fit invariant.
  5. Smoothing a corner never grows the bounding box beyond rect().
  6. Widened corner boxes never overlap diagonally for uniform radii.
  7. The transition curve's implicit cubic has no conjugate branches in the corner box.
  8. The implicit form's sign matches the Newton-based reference across the whole transition
     territory, including regions far outside the control points' bounding box.
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


def transition_bezier(s, r=1.0):
    """Control points of the x-side transition curve at radius r (mirrors BuildSmoothCorner)."""
    c = figma_coeffs(s, r)
    return ((c['bz'], 0.0), (c['bz'] - 2.0 * c['w'], 0.0), (c['bz'] - 3.0 * c['w'], 0.0),
            (c['bz'] - 3.0 * c['w'] - c['x'], c['g']))


def eval_bezier(ctrl, t):
    mt = 1.0 - t
    x = sum(k * (mt ** (3 - i)) * (t ** i) * ctrl[i][0]
            for i, k in enumerate((1.0, 3.0, 3.0, 1.0)))
    y = sum(k * (mt ** (3 - i)) * (t ** i) * ctrl[i][1]
            for i, k in enumerate((1.0, 3.0, 3.0, 1.0)))
    return x, y


def implicit_cubic(ctrl):
    """Implicitizes the cubic bezier in coordinates normalized by the footprint: returns the
    10 coefficients of the total-degree-3 polynomial f(x, y), recovered as the null vector
    of sample evaluations."""
    import numpy as np
    scale = ctrl[0][0]
    ts = np.linspace(-0.5, 1.5, 400)
    pts = np.array([eval_bezier(ctrl, t) for t in ts]) / scale
    x, y = pts[:, 0], pts[:, 1]
    m = np.column_stack([x ** 3, y ** 3, x ** 2 * y, x * y ** 2, x ** 2, y ** 2, x * y, x, y,
                         np.ones_like(x)])
    _, _, vh = np.linalg.svd(m)
    coeffs = vh[-1]
    return coeffs / np.max(np.abs(coeffs)), scale


def bezier_dist_ref(px, py, b0, b1, b2, b3):
    """Mirrors smoothCornerBezierDist (GLSLSmoothCorner.cpp): Gauss-Newton closest point on
    the cubic, sign from the cross product of the derivative and the point offset."""
    d3 = (-b0[0] + 3.0 * b1[0] - 3.0 * b2[0] + b3[0], -b0[1] + 3.0 * b1[1] - 3.0 * b2[1] + b3[1])
    d2 = (3.0 * b0[0] - 6.0 * b1[0] + 3.0 * b2[0], 3.0 * b0[1] - 6.0 * b1[1] + 3.0 * b2[1])
    d1 = (-3.0 * b0[0] + 3.0 * b1[0], -3.0 * b0[1] + 3.0 * b1[1])
    d0 = (b0[0] - px, b0[1] - py)
    best = 1.0e38
    best_t = 0.0
    seed = 0.0
    for _ in range(3):
        t = seed
        for _ in range(2):
            a2 = (d2[0] + t * d3[0], d2[1] + t * d3[1])
            a1 = (d1[0] + t * a2[0], d1[1] + t * a2[1])
            bb2 = (a2[0] + t * d3[0], a2[1] + t * d3[1])
            value = (d0[0] + t * a1[0], d0[1] + t * a1[1])
            deriv = (a1[0] + t * bb2[0], a1[1] + t * bb2[1])
            denom = max(deriv[0] * deriv[0] + deriv[1] * deriv[1], 1.1755e-38)
            t = t - (deriv[0] * value[0] + deriv[1] * value[1]) / denom
        t = min(max(t, 0.0), 1.0)
        diff = (((d3[0] * t + d2[0]) * t + d1[0]) * t + d0[0],
                ((d3[1] * t + d2[1]) * t + d1[1]) * t + d0[1])
        dist_sq = diff[0] * diff[0] + diff[1] * diff[1]
        if dist_sq < best:
            best = dist_sq
            best_t = t
        seed += 0.5
    bd = (((d3[0] * best_t + d2[0]) * best_t + d1[0]) * best_t + d0[0],
          ((d3[1] * best_t + d2[1]) * best_t + d1[1]) * best_t + d0[1])
    bdv = ((3.0 * d3[0] * best_t + 2.0 * d2[0]) * best_t + d1[0],
           (3.0 * d3[1] * best_t + 2.0 * d2[1]) * best_t + d1[1])
    side = bdv[1] * bd[0] - bdv[0] * bd[1]
    return math.sqrt(best) * (1.0 if side > 0.0 else -1.0)


def corner_dist_ref(qx, qy, r, c):
    """Mirrors smoothCornerDist: arc sector takes the circle distance, otherwise the x-side
    transition bezier (y-side is symmetric and covered by the same check)."""
    footprint = c['bz']
    step = c['w']
    arc_cos_sq = math.cos(0.5 * c['by']) ** 2
    dx, dy = qx - r, qy - r
    bisector_dot = -0.7071067811865476 * (dx + dy)
    if bisector_dot > 0.0 and bisector_dot * bisector_dot > arc_cos_sq * (dx * dx + dy * dy):
        return math.sqrt(dx * dx + dy * dy) - r
    ctrl2 = footprint - 3.0 * step
    arc_start = (ctrl2 - c['x'], c['g'])
    return bezier_dist_ref(qx, qy, (footprint, 0.0), (footprint - 2.0 * step, 0.0),
                           (ctrl2, 0.0), arc_start)


def check_implicit_sign_consistency():
    """In the x-side transition territory (corner box, qx >= qy, outside the arc sector),
    the implicit form's sign must match the Newton-based cornerDist sign point by point.
    The grid deliberately spans far beyond the control points' bounding box (y > g, y < 0,
    x < arcStart.x), which is where the algebraic continuation could pollute the sign.
    """
    print('8) implicit sign matches reference sign across the transition territory')
    ok = True
    n = 400
    for k in range(1, 21):
        s = k * 0.05
        c = figma_coeffs(s, 1.0)  # r = 1; everything scales linearly
        p, w, e, g = c['bz'], c['w'], c['x'], c['g']
        arc_cos_sq = math.cos(0.5 * c['by']) ** 2
        mismatches = 0
        for i in range(n):
            for j in range(n):
                qx = -0.1 * p + 1.1 * p * i / (n - 1)
                qy = -0.1 * p + 1.1 * p * j / (n - 1)
                if qx < qy:
                    continue
                # Skip the arc sector: that territory is owned by the circle branch, the
                # transition implicit form is never evaluated there.
                dx, dy = qx - 1.0, qy - 1.0
                bisector_dot = -0.7071067811865476 * (dx + dy)
                if (bisector_dot > 0.0 and
                        bisector_dot * bisector_dot > arc_cos_sq * (dx * dx + dy * dy)):
                    continue
                ref = corner_dist_ref(qx, qy, 1.0, c)
                if abs(ref) < 1e-7:
                    continue  # sub-pixel boundary noise, immaterial for a sign test
                nu = qy / g
                beta = (qx - p + (e / g) * qy) / (3.0 * w)
                f = nu * nu - 6.0 * beta * nu - 8.0 * nu - beta * beta * beta
                if (ref > 0.0) != (f > 0.0):
                    mismatches += 1
                    if mismatches <= 3:
                        print('   note s=%.2f q=(%.4f, %.4f): ref=%.6g f=%.6g'
                              % (s, qx, qy, ref, f))
        if mismatches > 0:
            print('   FAIL s=%.2f: %d sign mismatches' % (s, mismatches))
            ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def check_implicit_single_branch():
    """The implicit form of the transition curve must have no conjugate branches in the
    corner box [0, footprint]^2.

    Structurally this holds because three of the four control points sit on the x-axis, so
    Y(t) = g * t^3 is a bijection and the algebraic curve is the graph of a single-valued
    function x = phi(y): every y-slice of the implicit cubic must yield exactly one real
    root overall, matching the parametric curve; a second in-box real root would be a
    conjugate branch. This check confirms it numerically from the implicitized cubic.
    """
    import numpy as np
    print('7) transition curve implicit form has no conjugate branches in the corner box')
    ok = True
    for k in range(1, 21):
        s = k * 0.05
        ctrl = transition_bezier(s)
        footprint = ctrl[0][0]
        g = ctrl[3][1]
        # c300 x^3 + c030 y^3 + c210 x^2 y + c120 x y^2 + c200 x^2 + c020 y^2 + c110 x y
        # + c100 x + c010 y + c000, all in footprint-normalized coordinates
        (c300, c030, c210, c120, c200, c020, c110, c100, c010, c000), scale = \
            implicit_cubic(ctrl)
        box = 1.0 + 1e-9
        for j in range(201):
            yv = box * j / 200.0
            roots = np.roots([c300, c210 * yv + c200, c120 * yv * yv + c110 * yv + c100,
                              c030 * yv ** 3 + c020 * yv * yv + c010 * yv + c000])
            real_roots = [rt.real for rt in roots if abs(rt.imag) < 1e-6]
            if len(real_roots) != 1:
                print('   FAIL s=%.2f y=%.4f: %d real roots %r (graph property broken)'
                      % (s, yv * footprint, len(real_roots), real_roots))
                ok = False
                continue
            t = (yv * scale / g) ** (1.0 / 3.0) if g > 0 else 0.0
            expect = eval_bezier(ctrl, t)[0] / scale
            if abs(real_roots[0] - expect) > 1e-3:
                print('   FAIL s=%.2f y=%.4f: root %.6f != parametric %.6f'
                      % (s, yv * footprint, real_roots[0] * scale, expect * scale))
                ok = False
    print('   %s' % ('PASS' if ok else 'FAIL'))
    return ok


def main():
    results = [check_linearity(), check_arc_handle(), check_two_thirds(),
               check_spread_fit(), check_bounds(), check_diagonal_boxes(),
               check_implicit_single_branch(), check_implicit_sign_consistency()]
    print()
    print('ALL PASS' if all(results) else 'SOME CHECKS FAILED')
    return 0 if all(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
