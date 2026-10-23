#!/usr/bin/env python3
# Copyright (C) 2026 Tencent. All rights reserved.
# Measures distance to the CPU cubic contour. This is a numerical experiment,
# not a proof over unsampled parameters or a GPU floating-point conformance test.

import argparse
import json
import math
from pathlib import Path
import time

import numpy as np


def coefficients(s, dtype=np.float64):
    if dtype == np.float32:
        f = np.float32
        s = f(s)
        theta = f(math.pi) * f(0.25) * s
        sweep = f(math.pi / 2) * (f(1) - s)
        tangent = np.tan(theta)
        p = f(1) + s
        chord = np.sin(sweep * f(0.5)) * f(math.sqrt(2))
        e = np.tan(theta * f(0.5)) * np.cos(theta)
        g = e * tangent
        w = (p - chord - (f(1) + tangent) * e) / f(3)
        return np.array([p, w, e, g], dtype=dtype)
    theta = math.pi * s / 4
    e = math.tan(theta / 2) * math.cos(theta)
    g = e * math.tan(theta)
    w = (1 + s - math.sqrt(2) * math.sin(math.pi * (1 - s) / 4)
         - (1 + math.tan(theta)) * e) / 3
    return np.array([1 + s, w, e, g])


def build_corner(s, rx, ry, dtype=np.float64):
    # Float32 mode rounds CPU uniforms and reproduces shader control-point arithmetic.
    p, w, e, g = coefficients(s, dtype)
    rx, ry = dtype(rx), dtype(ry)
    fp, step = p * rx, w * rx
    ctrl2 = fp - dtype(3) * step
    ps = np.array([ctrl2 - e * rx, g * rx], dtype=dtype)
    ky = ry / rx
    incoming = np.array([[fp, 0], [fp - dtype(2) * step, 0],
                         [ctrl2, 0], [ps[0], ps[1] * ky]], dtype=dtype)
    outgoing = np.array([[ps[1], ps[0] * ky], [0, ctrl2 * ky],
                         [0, (fp - dtype(2) * step) * ky], [0, fp * ky]], dtype=dtype)
    arc = None
    if s < 1:
        theta = math.pi * s / 4
        handle = 4 / 3 * math.tan(math.pi * (1 - s) / 8)
        ts = np.array([-math.cos(theta), math.sin(theta)])
        te = np.array([-math.sin(theta), math.cos(theta)])
        start, end = incoming[-1].astype(float), outgoing[0].astype(float)
        arc = np.array([start, start + handle * np.array([rx, ry]) * ts,
                        end - handle * np.array([rx, ry]) * te, end], dtype=dtype)
    return incoming, arc, outgoing


def power_coefficients(curve):
    b0, b1, b2, b3 = curve
    return np.array([b0, 3 * (b1 - b0), 3 * (b0 - 2 * b1 + b2),
                     -b0 + 3 * b1 - 3 * b2 + b3])


def evaluate(curve, t):
    a0, a1, a2, a3 = power_coefficients(curve)
    t = np.asarray(t)[..., None]
    return ((a3 * t + a2) * t + a1) * t + a0


def derivative(curve, t):
    _, a1, a2, a3 = power_coefficients(curve)
    t = np.asarray(t)[..., None]
    return (3 * a3 * t + 2 * a2) * t + a1


def real_roots_batch(poly, lo, hi):
    # Batched companion eigenvalues enumerate all real roots, not just one seed.
    degree = poly.shape[1] - 1
    while degree and np.all(np.abs(poly[:, degree]) < 1e-26):
        degree -= 1
    if degree == 0:
        return np.empty((len(poly), 0))
    c = poly[:, :degree + 1]
    matrix = np.zeros((len(c), degree, degree))
    for i in range(1, degree):
        matrix[:, i, i - 1] = 1
    matrix[:, :, -1] = -c[:, :degree] / c[:, degree, None]
    roots = np.linalg.eigvals(matrix)
    valid = ((np.abs(roots.imag) <= 1e-7 * (1 + np.abs(roots.real)))
             & (roots.real >= lo - 1e-9) & (roots.real <= hi + 1e-9))
    return np.where(valid, np.clip(roots.real, lo, hi), np.nan)


def reference_cubic(points, curve):
    # Normalize before forming the quintic to avoid device-size conditioning.
    scale = max(float(np.ptp(curve, axis=0).max()), 1e-15)
    c = (curve - curve[0]) / scale
    q = (points - curve[0]) / scale
    a = power_coefficients(c)
    residual = np.broadcast_to(a, (len(q), 4, 2)).copy()
    residual[:, 0] -= q
    deriv = np.array([a[1], 2 * a[2], 3 * a[3]])
    poly = np.zeros((len(q), 6))
    for i in range(4):
        for j in range(3):
            poly[:, i + j] += np.sum(residual[:, i] * deriv[j], axis=1)
    roots = real_roots_batch(poly, 0, 1)
    candidates = np.concatenate([np.zeros((len(q), 1)), np.ones((len(q), 1)), roots], axis=1)
    for _ in range(3):
        t = candidates[:, 2:]
        f = np.zeros_like(t)
        fp = np.zeros_like(t)
        for j in range(5, -1, -1):
            f = f * t + poly[:, j, None]
        for j in range(5, 0, -1):
            fp = fp * t + j * poly[:, j, None]
        update = np.divide(f, fp, out=np.zeros_like(f), where=np.abs(fp) > 1e-25)
        candidates[:, 2:] = np.clip(t - update, 0, 1)
    safe_t = np.nan_to_num(candidates)
    values = evaluate(c, safe_t) - q[:, None, :]
    d2 = np.sum(values * values, axis=2)
    d2[~np.isfinite(candidates)] = np.inf
    index = d2.argmin(axis=1)
    return np.sqrt(d2[np.arange(len(q)), index]) * scale, candidates[np.arange(len(q)), index]


def reference_ellipse(points, rx, ry, s):
    theta = math.pi * s / 4
    lo, hi = math.tan(theta / 2), math.tan((math.pi / 2 - theta) / 2)
    a = rx * (points[:, 0] - rx)
    b = ry * (points[:, 1] - ry)
    delta = rx * rx - ry * ry
    if rx == ry:
        psi = np.arctan2(rx - points[:, 0], ry - points[:, 1])
        psi = np.clip(psi, theta, math.pi / 2 - theta)
        candidates = np.stack([np.full(len(points), theta),
                               np.full(len(points), math.pi / 2 - theta), psi], axis=1)
    else:
        poly = np.stack([a, 2 * (delta - b), np.zeros(len(a)),
                         -2 * (delta + b), -a], axis=1)
        # a=0 reduces the degree for those queries; solve these few rows separately.
        regular = np.abs(a) > 1e-20
        roots = np.full((len(points), 4), np.nan)
        if regular.any():
            roots[regular] = real_roots_batch(poly[regular], lo, hi)
        for i in np.flatnonzero(~regular):
            row = poly[i]
            degree = 4
            while degree and abs(row[degree]) < 1e-20:
                degree -= 1
            if degree:
                rr = np.polynomial.polynomial.polyroots(row[:degree + 1])
                rr = rr[(np.abs(rr.imag) < 1e-8) & (rr.real >= lo) & (rr.real <= hi)].real
                roots[i, :len(rr)] = rr
        candidates = np.concatenate([np.full((len(points), 1), theta),
                                     np.full((len(points), 1), math.pi / 2 - theta),
                                     2 * np.arctan(roots)], axis=1)
    x = rx * (1 - np.sin(candidates))
    y = ry * (1 - np.cos(candidates))
    d2 = (x - points[:, 0, None]) ** 2 + (y - points[:, 1, None]) ** 2
    return np.sqrt(np.nanmin(d2, axis=1))


def ray_distance(points, origin, axis):
    delta = points - origin
    return np.hypot(np.minimum(delta[:, axis], 0), delta[:, 1 - axis])


def references(points, curves, rx, ry, s, include_ellipse=True):
    values, params = [], []
    for curve in curves:
        if curve is None:
            values.append(np.linalg.norm(points - curves[0][-1], axis=1))
            params.append(np.zeros(len(points)))
        else:
            distance, t = reference_cubic(points, curve)
            values.append(distance)
            params.append(t)
    values.extend([ray_distance(points, curves[0][0], 0),
                   ray_distance(points, curves[2][-1], 1)])
    cpu_parts = np.stack(values, axis=1)
    ideal_parts = cpu_parts.copy()
    if s < 1 and include_ellipse:
        ideal_parts[:, 1] = reference_ellipse(points, rx, ry, s)
    return cpu_parts, ideal_parts, params


def gauss_newton(points, curve, iterations, dtype, include_endpoints=False):
    points, curve = points.astype(dtype), curve.astype(dtype)
    b0, b1, b2, b3 = curve
    a0 = b0
    a1 = -dtype(3) * b0 + dtype(3) * b1
    a2 = dtype(3) * b0 - dtype(6) * b1 + dtype(3) * b2
    a3 = -b0 + dtype(3) * b1 - dtype(3) * b2 + b3
    best = np.full(len(points), dtype(1e38), dtype=dtype)
    if include_endpoints:
        best = np.minimum(np.sum((points - curve[0]) ** 2, axis=1),
                          np.sum((points - curve[-1]) ** 2, axis=1))
    for seed in (0, 0.5, 1):
        t = np.full(len(points), seed, dtype=dtype)
        d0 = a0 - points
        for _ in range(iterations):
            h2 = a2 + t[:, None] * a3
            h1 = a1 + t[:, None] * h2
            b2 = h2 + t[:, None] * a3
            value = d0 + t[:, None] * h1
            deriv = h1 + t[:, None] * b2
            denominator = np.maximum(np.sum(deriv * deriv, axis=1), dtype(1.1755e-38))
            t -= np.sum(deriv * value, axis=1) / denominator
        t = np.clip(t, 0, 1)
        diff = ((a3 * t[:, None] + a2) * t[:, None] + a1) * t[:, None] + d0
        distance_squared = np.sum(diff * diff, axis=1)
        best = np.where(distance_squared < best, distance_squared, best)
    return np.sqrt(best).astype(float)


def choose_parts(points, rx, ry, s, mode, dtype):
    q = points.astype(dtype)
    if mode == 'current':
        qc = q.copy()
        qc[:, 1] /= dtype(ry) / dtype(rx)
        d = qc - dtype(rx)
        bis = np.sum(d * dtype(-0.70710678), axis=1)
        sweep = dtype(math.pi / 2) * (dtype(1) - dtype(s))
        arc_cos = np.cos(sweep * dtype(0.5))
        arc_cos = arc_cos * arc_cos
        arc = (bis > 0) & (bis * bis > arc_cos * np.sum(d * d, axis=1))
        selected = np.where(qc[:, 0] >= qc[:, 1], 0, 2)
        selected[arc] = 1
    else:
        theta = math.pi * s / 4
        u, v = dtype(math.sin(theta)), dtype(math.cos(theta))
        ps = np.array([rx * (1 - u), ry * (1 - v)], dtype=dtype)
        pe = np.array([rx * (1 - v), ry * (1 - u)], dtype=dtype)
        fs = dtype(rx) * v * (q[:, 0] - ps[0]) - dtype(ry) * u * (q[:, 1] - ps[1])
        fe = dtype(rx) * u * (q[:, 0] - pe[0]) - dtype(ry) * v * (q[:, 1] - pe[1])
        selected = np.where(fs >= 0, 0, np.where(fe <= 0, 2, 1))
        selected[(fs > 0) & (fe < 0)] = -1
        if s == 1:
            selected = np.where(fs >= 0, 0, 2)
    footprint = dtype(1 + s) * np.array([rx, ry], dtype=dtype)
    outside_box = (q[:, 0] > footprint[0]) | (q[:, 1] > footprint[1])
    selected[outside_box] = 3
    return selected


def approximate(points, rx, ry, s, mode, iterations, dtype):
    curves = build_corner(s, rx, ry, dtype)
    selected = choose_parts(points, rx, ry, s, mode, dtype)
    result = np.full(len(points), np.nan)
    for part in (0, 2):
        mask = selected == part
        if mask.any():
            result[mask] = gauss_newton(points[mask], curves[part], iterations, dtype,
                                              include_endpoints=mode == 'cpu-cubic')
    mask = selected == 1
    if mask.any():
        q = points[mask].astype(dtype)
        if mode == 'cpu-cubic':
            result[mask] = gauss_newton(points[mask], curves[1], iterations, dtype, include_endpoints=True)
        elif rx == ry:
            result[mask] = np.abs(np.sqrt(np.sum((q - dtype(rx)) ** 2, axis=1)) - dtype(rx))
        else:
            radii = np.array([rx, ry], dtype=dtype)
            offset = (q - radii) / radii
            test = np.sum(offset * offset, axis=1) - dtype(1)
            gradient = dtype(2) * offset / radii
            denominator = np.maximum(np.sum(gradient * gradient, axis=1), dtype(1.1755e-38))
            result[mask] = np.abs(test / np.sqrt(denominator))
    mask = selected == 3
    result[mask] = np.abs(np.minimum(points[mask, 0], points[mask, 1]))
    return result, selected


def sample_points(curves, rx, ry, s, half, aa, density):
    # Normal probes target both sides of each CPU curve, including all joins.
    t = np.unique(np.concatenate([np.linspace(0, 1, density),
                                 np.geomspace(1e-6, 0.1, 8),
                                 1 - np.geomspace(1e-6, 0.1, 8)]))
    distances = np.unique(np.concatenate([
        np.array([0, half / 2, half, max(0, half - aa), half + aa]),
        np.linspace(max(0, half - aa), half + aa, 5)]))
    distances = np.unique(np.concatenate([-distances, distances]))
    probes = []
    for curve in curves:
        if curve is None:
            continue
        b, tangent = evaluate(curve, t), derivative(curve, t)
        normal = np.stack([tangent[:, 1], -tangent[:, 0]], axis=1)
        normal /= np.linalg.norm(normal, axis=1)[:, None]
        probes.append((b[:, None, :] + normal[:, None, :] * distances[None, :, None]).reshape(-1, 2))
    # The grid also finds false visible bands away from the reference stroke.
    extent = np.array([rx, ry]) * (1 + s)
    low = -(half + aa)
    grid_n = max(9, density // 3)
    xx, yy = np.meshgrid(np.linspace(low, extent[0] + half + aa, grid_n),
                         np.linspace(low, extent[1] + half + aa, grid_n))
    probes.append(np.stack([xx.ravel(), yy.ravel()], axis=1))
    points = np.concatenate(probes)
    return points[(points[:, 0] >= low) & (points[:, 1] >= low)]


def evaluate_case(s, rx, ry, width, density, modes=('current', 'normals', 'cpu-cubic')):
    half, aa = width / 2, 0.5
    curves = build_corner(s, rx, ry)
    points = sample_points(curves, rx, ry, s, half, aa, density)
    cpu, ideal, _ = references(points, curves, rx, ry, s,
                               include_ellipse=any(mode != 'cpu-cubic' for mode in modes))
    ref, ideal_ref = cpu.min(axis=1), ideal.min(axis=1)
    records = []
    rho = min(rx, ry) ** 2 / (2 * max(rx, ry))
    for mode in modes:
        for iterations in (2, 3):
            for precision, dtype in [('float64', np.float64), ('float32', np.float32)]:
                value, selected = approximate(points, rx, ry, s, mode, iterations, dtype)
                true_band = np.abs(ref - half) <= aa + 1e-8
                computed_band = np.isfinite(value) & (np.abs(value - half) <= aa + 1e-8)
                coverage_ref = np.clip(half + aa - ref, 0, 1)
                coverage_value = np.clip(half + aa - value, 0, 1)
                false_visible = np.isfinite(value) & (coverage_ref == 0) & (coverage_value > 0)
                false_hole = np.isfinite(value) & (coverage_ref == 1) & (coverage_value < 1)
                unsupported_visible = ~np.isfinite(value) & (coverage_ref > 0)
                relevant = true_band | computed_band | false_visible | false_hole | unsupported_visible
                valid = relevant & np.isfinite(value)
                errors = np.abs(value - ref)
                worst = int(np.argmax(np.where(valid, errors, -1)))
                model_parts = cpu if mode == 'cpu-cubic' else ideal
                model_ref = ref if mode == 'cpu-cubic' else ideal_ref
                selected_exact = np.full(len(points), np.nan)
                for part in (0, 1, 2):
                    mask = selected == part
                    selected_exact[mask] = model_parts[mask, part]
                selected_exact[selected == 3] = value[selected == 3]
                def maximum(values):
                    return float(np.max(values[valid])) if valid.any() else None
                records.append(dict(
                    smoothness=s, rx=rx, ry=ry, width=width, half_width=half,
                    aspect=min(rx, ry)/max(rx, ry),
                    relative_band=(half + aa) * max(rx, ry) / min(rx, ry) ** 2,
                    safe_selection=half + aa < rho, mode=mode, iterations=iterations,
                    precision=precision, samples=len(points), relevant_samples=int(relevant.sum()),
                    unsupported_relevant=int((relevant & ~np.isfinite(value)).sum()),
                    max_error=maximum(errors),
                    max_true_band_error=float(np.max(errors[true_band & np.isfinite(value)]))
                        if (true_band & np.isfinite(value)).any() else None,
                    max_geometry_error=maximum(np.abs(model_ref-ref)),
                    max_dispatch_error=maximum(np.abs(selected_exact-model_ref)),
                    max_solver_error=maximum(np.abs(value-selected_exact)),
                    max_coverage_error=maximum(np.abs(coverage_value-coverage_ref)),
                    false_visible=int(false_visible.sum()), false_holes=int(false_hole.sum()),
                    worst_point=points[worst].tolist(), worst_cpu=float(ref[worst]),
                    worst_approx=float(value[worst]) if np.isfinite(value[worst]) else None,
                    worst_selected=int(selected[worst]), worst_cpu_part=int(cpu[worst].argmin())))
    return records


def cubic_bounds(point, curve, tolerance):
    # Independent reference check. Each Bezier subdivision lies within a known
    # Hausdorff distance of its chord; bounds enclose the true minimum distance.
    def bound(c):
        a, b = c[0], c[-1]
        v = b - a
        length2 = np.dot(v, v)
        t = np.clip(np.dot(point - a, v) / length2, 0, 1) if length2 else 0
        distance = np.linalg.norm(point - a - t * v)
        if length2:
            ctrl_t = np.clip((c - a) @ v / length2, 0, 1)
            flat = np.linalg.norm(c - (a + ctrl_t[:, None] * v), axis=1).max()
        else:
            flat = np.linalg.norm(c - a, axis=1).max()
        return max(0, distance - flat), distance + flat
    pending = [curve]
    upper = min(np.linalg.norm(point - curve[0]), np.linalg.norm(point - curve[-1]))
    lower = upper
    count = 0
    while pending:
        c = pending.pop()
        lo, hi = bound(c)
        upper = min(upper, hi)
        if lo >= upper:
            continue
        if hi - lo <= tolerance:
            lower = min(lower, lo)
            continue
        p01, p12, p23 = (c[0]+c[1])/2, (c[1]+c[2])/2, (c[2]+c[3])/2
        p012, p123 = (p01+p12)/2, (p12+p23)/2
        mid = (p012+p123)/2
        pending.extend([np.array([c[0],p01,p012,mid]), np.array([mid,p123,p23,c[3]])])
        count += 1
        if count > 1000000:
            raise RuntimeError('Reference subdivision did not converge')
    return min(lower, upper), upper


def summarize(records):
    result = []
    for mode in ('current', 'normals', 'cpu-cubic'):
        for iterations in (2, 3):
            for precision in ('float64', 'float32'):
                for circular in (True, False):
                    rows = [r for r in records if r['mode']==mode and r['iterations']==iterations
                            and r['precision']==precision and (r['rx']==r['ry'])==circular]
                    if not rows:
                        continue
                    valid = [r for r in rows if r['max_error'] is not None]
                    worst = max(valid, key=lambda r:r['max_error']) if valid else None
                    result.append(dict(mode=mode,iterations=iterations,precision=precision,
                                       circular=circular,cases=len(rows),
                                       failing_cases=sum(bool(r['max_error'] is None or r['max_error']>0.25
                                                              or r['unsupported_relevant']) for r in rows),
                                       worst=worst))
    return result


def matrix_report(inputs, output, validate):
    groups = {}
    density_maxima = {}
    extra_maxima = {}
    metadata = []
    queries = 0
    cases = set()
    for name in inputs:
        path = Path(name)
        with path.open() as stream:
            config = json.loads(next(stream))
            metadata.append(dict(path=str(path), configuration=config))
            for line in stream:
                row = json.loads(line)
                if row.get('mode') != 'cpu-cubic':
                    continue
                if row['iterations'] == 3 and row['precision'] == 'float64':
                    queries += row['samples']
                if row['precision'] != 'float32':
                    continue
                ratio = round(max(row['rx'], row['ry']) / min(row['rx'], row['ry']), 8)
                if row['relative_band'] > 0.490000001 or max(row['rx'], row['ry']) > 16384:
                    continue
                key = (ratio, row['width'], row['iterations'])
                if path.stem.startswith('matrix-final-') and row['max_error'] is not None:
                    density_key = (config['density'], key)
                    density_maxima[density_key] = max(density_maxima.get(density_key, 0), row['max_error'])
                if path.stem.startswith('matrix-convergence-') and row['max_error'] is not None:
                    density_key = (config['density'], key)
                    extra_maxima[density_key] = max(extra_maxima.get(density_key, 0), row['max_error'])
                shape = (row['smoothness'], row['rx'], row['ry'], row['width'])
                cases.add(shape)
                group = groups.setdefault(key, dict(worst=None, cases=set(), failed=set(), unsupported=0))
                group['cases'].add(shape)
                group['unsupported'] += row['unsupported_relevant']
                if row['max_error'] is None or row['max_error'] > 0.25 or row['unsupported_relevant']:
                    group['failed'].add(shape)
                if row['max_error'] is not None and (group['worst'] is None or
                                                    row['max_error'] > group['worst']['max_error']):
                    group['worst'] = dict(row, source=str(path), density=config['density'])
    report_rows = []
    checks = []
    tested_s = sorted({shape[0] for shape in cases})
    tested_ratios = sorted({key[0] for key in groups})
    tested_widths = sorted({key[1] for key in groups})
    for ratio, width in sorted({key[:2] for key in groups}):
        row = dict(axis_ratio=ratio, width=width)
        for count in (2, 3):
            group = groups[(ratio, width, count)]
            worst = group['worst']
            row[str(count)] = dict(maximum=worst, tested_cases=len(group['cases']),
                                   failed_cases=len(group['failed']), unsupported=group['unsupported'])
            if validate and worst is not None:
                point = np.array(worst['worst_point'])
                curves = build_corner(worst['smoothness'], worst['rx'], worst['ry'])
                for tolerance in (1e-5, 5e-6):
                    bounds = [cubic_bounds(point, c, tolerance) for c in curves if c is not None]
                    rays = [ray_distance(point[None, :], curves[0][0], 0)[0],
                            ray_distance(point[None, :], curves[2][-1], 1)[0]]
                    low = float(min([b[0] for b in bounds] + rays))
                    high = float(min([b[1] for b in bounds] + rays))
                    checks.append(dict(axis_ratio=ratio, width=width, iterations=count,
                                       tolerance=tolerance, lower=low, upper=high,
                                       root_distance=worst['worst_cpu'],
                                       passed=bool(low-1e-8 <= worst['worst_cpu'] <= high+1e-8)))
        report_rows.append(row)
    conclusions = []
    for width in (50, 100):
        selected_rows = [r for r in report_rows if r['width'] == width]
        for count in ('2', '3'):
            if selected_rows:
                failed = sum(r[count]['failed_cases'] > 0 for r in selected_rows)
                maximum = max(r[count]['maximum']['max_error'] for r in selected_rows)
                conclusions.append(f'- W={width}px、{count}次迭代：{len(selected_rows)}个半径比例组中，'
                                   f'{failed}组出现超差或未支持样本；采样最大误差{maximum:.6f}px。')
    lines = ['# 描边距离误差总表', '', *conclusions, '',
             '参考：统一 CPU 三次贝塞尔轮廓及相邻直线射线；真实连接点法线选段；'
             '三个初值分别执行 2/3 次 Gauss-Newton，独立比较两个端点。', '',
             '统计口径：float32 数值模型；设备空间；AA 额外宽度 0.5px；'
             '(W/2+0.5)M/m²≤0.49，M≤16384px；每行跨平滑度、绝对半径和长轴方向分别取最大值。'
             '两种迭代次数分别搜索最差配置，不共用最差 s。', '',
             '判断“最大描边宽度”是否可用，必须同时检查该宽度及所有更细的已测描边；'
             '仅上限那一行通过，不代表整个宽度范围通过。', '',
             '误差包括参考与计算 AA 范围，以及错误显示或缺失覆盖的样本。'
             '不影响覆盖率的深处距离误差不计入；不同宽度的统计区域不同，最大值不必随宽度单调增加。'
             's=0 走普通圆角路径，不在平滑曲线实验中；表内通过仅指已测样本，'
             '不构成连续参数域或实际 GPU 的全域保证。', '',
             '| M/m | W（px） | 2次最大误差（px） | 2次最差 s | 2次最差 (rx,ry)（px） | '
             '3次最大误差（px） | 3次最差 s | 3次最差 (rx,ry)（px） |',
             '|---:|---:|---:|---:|---|---:|---:|---|']
    def cell(data):
        r = data['maximum']
        if r is None:
            return ['无有效值', '—', '—']
        value = f"{r['max_error']:.6f}"
        if r['max_error'] > 0.25 or data['unsupported']:
            value = '**' + value + '**'
        return [value, f"{r['smoothness']:.8g}", f"({r['rx']:.6f}, {r['ry']:.6f})"]
    for row in report_rows:
        lines.append('| ' + ' | '.join([f"{row['axis_ratio']:g}", f"{row['width']:g}"] +
                                      cell(row['2']) + cell(row['3'])) + ' |')
    lines += ['', '加粗值表示采样误差超过0.25px或存在未支持的可见样本。', '',
              '长短轴比：' + '、'.join(f'{v:g}' for v in tested_ratios) + '；完整宽度：' +
              '、'.join(f'{v:g}' for v in tested_widths) + 'px。',
              f'平滑度样本范围：{tested_s[0]:.8g}～{tested_s[-1]:.8g}，'
              f'合并 {len(tested_s)} 个不同取值，含定向加密。', '',
              f'合并查询点记录：{queries:,}；不同形状与宽度配置：{len(cases):,}。'
              '查询点记录含加密重测，不等同于独立均匀网格点数。', '',
              '## 数值复核', '']
    if checks:
        fine = [c for c in checks if c['tolerance'] == 5e-6]
        lines.append(f"参考区间检查 {len(checks)} 项；失败 {sum(not c['passed'] for c in checks)} 项；"
                     f"细容差下最大区间宽度 {max(c['upper']-c['lower'] for c in fine):.9g}px。")
    density_checks = []
    for key in groups:
        if (513, key) in density_maxima and (1025, key) in density_maxima:
            first, second = density_maxima[(513, key)], density_maxima[(1025, key)]
            density_checks.append(dict(axis_ratio=key[0], width=key[1], iterations=key[2],
                                       error_513=first, error_1025=second, change=abs(second-first)))
    if density_checks:
        for count in (2, 3):
            subset = [c for c in density_checks if c['iterations'] == count]
            if subset:
                lines.append(f"{count}次迭代逐行极值的513→1025密度检查：{len(subset)}行，"
                             f"最大变化{max(c['change'] for c in subset):.6f}px。"
                             '有限采样可能漏掉极值，未将读数稳定等同于全域证明。')
    extra_checks = []
    for key in groups:
        if (1025, key) in extra_maxima and (2049, key) in extra_maxima:
            first, second = extra_maxima[(1025, key)], extra_maxima[(2049, key)]
            extra_checks.append(dict(axis_ratio=key[0], width=key[1], iterations=key[2],
                                     error_1025=first, error_2049=second, change=abs(second-first)))
    for count in (2, 3):
        subset = [c for c in extra_checks if c['iterations'] == count]
        if subset:
            lines.append(f"{count}次迭代对变化较大或新增极值追加1025→2049检查：{len(subset)}行，"
                         f"最大变化{max(c['change'] for c in subset):.6f}px。")
    lines.append('仍随采样变化的极值只作为已发现误差的记录；超差样本足以否定通过条件，'
                 '不能将有限采样最大值当成连续域的精确上限。')
    lines += ['', '## 结果来源', '']
    lines.extend('- ' + m['path'] + f"（每段均匀参数样本 {m['configuration']['density']}）"
                 for m in metadata)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n')
    output.with_suffix('.json').write_text(json.dumps(dict(inputs=metadata, queries=queries,
                                                         distinct_cases=len(cases), rows=report_rows,
                                                         density_checks=density_checks, extra_density_checks=extra_checks),
                                                    indent=2, allow_nan=False))
    if validate:
        output.with_suffix('.validation.json').write_text(json.dumps(checks, indent=2))
    print(json.dumps(dict(output=str(output), rows=len(report_rows), reference_checks=len(checks),
                         reference_failures=sum(not c['passed'] for c in checks))), flush=True)


def main():
    parser = argparse.ArgumentParser(description='CPU contour stroke-distance experiment')
    parser.add_argument('--output', required=True)
    parser.add_argument('--matrix-inputs', nargs='+', help='Combine experiment JSONL files into one matrix report')
    parser.add_argument('--density', type=int, default=33)
    parser.add_argument('--modes', default='current,normals,cpu-cubic')
    parser.add_argument('--smoothness', default='0.001,0.01,0.05,0.1,0.2,0.4,0.6,0.8,0.95,0.99,1')
    parser.add_argument('--aspects', default='1,0.75,0.5,0.25')
    parser.add_argument('--widths', default='1,4,16,32,48,64,96')
    parser.add_argument('--relative-bands', default='0.1,0.25,0.4,0.49,0.6')
    parser.add_argument('--scales', default='', help='Additional fixed maximum radii')
    parser.add_argument('--cases-file', help='Replay parameter rows from JSONL, deduplicated')
    parser.add_argument('--validate', action='store_true')
    args = parser.parse_args()
    if args.matrix_inputs:
        matrix_report(args.matrix_inputs, Path(args.output), args.validate)
        return
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    def numbers(value):
        return [float(x) for x in value.split(',') if x]
    if args.cases_file:
        rows = [json.loads(line) for line in Path(args.cases_file).read_text().splitlines()]
        cases = sorted({(r['smoothness'],r['rx'],r['ry'],r['width']) for r in rows if 'rx' in r})
    else:
        cases = []
        for s in numbers(args.smoothness):
            if not 0 < s <= 1:
                raise ValueError('smoothness must be in (0,1]; s=0 uses the ordinary RRect path')
            for aspect in numbers(args.aspects):
                for width in numbers(args.widths):
                    scales = [(width/2+0.5)/(ratio*aspect*aspect) for ratio in numbers(args.relative_bands)]
                    scales += numbers(args.scales)
                    for radius in sorted(set(scales)):
                        cases.append((s,radius,radius*aspect,width))
                        if aspect != 1:
                            cases.append((s,radius*aspect,radius,width))
    metadata = dict(kind='configuration',reference='CPU float64 cubic contour plus adjacent rays',
                    distance_metric='absolute unsigned distance error in union of true and computed AA bands plus coverage mismatches',
                    aa=0.5,tolerance=0.25,density=args.density,cases=len(cases),
                    numpy=np.__version__,experiment_version=4, modes=args.modes.split(','),
                    endpoint_candidates='Explicit for cpu-cubic; unchanged final-clamp seeds for current/normals',
                    float32_note='Approximate production float32 model; NumPy transcendental functions; no FMA; not actual GPU or C++ libm execution',
                    normals_note='Exact ellipse endpoint normals; S4 is unsupported, not assigned by a guessed line',
                    arc_solver='current/normals: exact circle or Sampson ellipse; cpu-cubic: Gauss-Newton on CPU arc cubic')
    start = time.monotonic()
    records = []
    with output.open('w') as stream:
        stream.write(json.dumps(metadata)+'\n')
        for i,(s,rx,ry,width) in enumerate(cases):
            rows = evaluate_case(s,rx,ry,width,args.density,args.modes.split(','))
            for row in rows:
                stream.write(json.dumps(row,allow_nan=False)+'\n')
            stream.flush()
            records.extend(rows)
            if (i+1)%20==0 or i+1==len(cases):
                print(json.dumps(dict(completed=i+1,total=len(cases),elapsed_seconds=round(time.monotonic()-start,2))),flush=True)
    summary = summarize(records)
    output.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False))
    if args.validate:
        validations = []
        for item in summary:
            row = item['worst']
            if row is None:
                continue
            point = np.array(row['worst_point'])
            curves = build_corner(row['smoothness'],row['rx'],row['ry'])
            for tolerance in (1e-5,5e-6):
                bounds = [cubic_bounds(point,c,tolerance) for c in curves if c is not None]
                rays = [ray_distance(point[None,:],curves[0][0],0)[0],
                        ray_distance(point[None,:],curves[2][-1],1)[0]]
                lo = min([b[0] for b in bounds]+rays)
                hi = min([b[1] for b in bounds]+rays)
                validations.append(dict(case=row,tolerance=tolerance,lower=lo,upper=hi,
                                        reference_within_bounds=bool(lo-1e-8<=row['worst_cpu']<=hi+1e-8)))
        output.with_suffix('.validation.json').write_text(json.dumps(validations,indent=2))
    print(json.dumps(dict(output=str(output),records=len(records),elapsed_seconds=round(time.monotonic()-start,2))),flush=True)


if __name__ == '__main__':
    main()
