#!/usr/bin/env python3
# Copyright (C) 2026 Tencent. All rights reserved.
# Fill-path segment-selection error in the small-radius regime, where the safe
# distance R = m^2/(2M) can fall below the 0.5px AA band. Selection only: the
# distance to the chosen segment is evaluated exactly, so solver error is not
# measured here. This is a numerical experiment, not a proof over unsampled
# parameters.

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np

from sweep_newton_stroke_width import (build_corner, cubic_bounds, derivative, evaluate,
                                       ray_distance, reference_cubic, reference_ellipse)

BAND = 0.5  # Fill AA transition half-width in device px.


def select_segment(points, rx, ry, s, dtype):
    # True endpoint normals fs/fe as in the shader; S4 is left unassigned (-1),
    # outside the corner box the edge branch (3) is exact and not measured.
    q = points.astype(dtype)
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
    footprint = (1 + s) * np.array([rx, ry])
    selected[(points[:, 0] > footprint[0]) | (points[:, 1] > footprint[1])] = 3
    return selected


def part_distances(points, curves, rx, ry, s):
    # Exact distance to each candidate part of the outline, in float64.
    c1, arc, c3 = (None if c is None else c.astype(np.float64) for c in curves)
    d1, _ = reference_cubic(points, c1)
    d3, _ = reference_cubic(points, c3)
    d2 = reference_ellipse(points, rx, ry, s) if s < 1 else np.full(len(points), np.inf)
    rays = np.minimum(ray_distance(points, c1[0], 0), ray_distance(points, c3[-1], 1))
    parts = np.stack([d1, d2, d3], axis=1)
    return parts, rays


def sample_points(curves, rx, ry, s, density):
    ts = np.unique(np.concatenate([np.linspace(0, 1, density),
                                   np.geomspace(1e-6, 0.02, 6),
                                   1 - np.geomspace(1e-6, 0.02, 6)]))
    offsets = np.linspace(-(BAND + 0.06), BAND + 0.06, 25)
    probes = []
    for curve in curves:
        if curve is None:
            continue
        base = evaluate(curve, ts)
        tangent = derivative(curve, ts)
        normal = np.stack([tangent[:, 1], -tangent[:, 0]], axis=1)
        normal /= np.linalg.norm(normal, axis=1)[:, None]
        probes.append((base[:, None, :] + normal[:, None, :] * offsets[None, :, None]).reshape(-1, 2))
    if s < 1:
        theta = math.pi * s / 4
        psi = np.linspace(theta, math.pi / 2 - theta, density)
        base = np.stack([rx * (1 - np.sin(psi)), ry * (1 - np.cos(psi))], axis=1)
        gradient = np.stack([-np.sin(psi) / rx, -np.cos(psi) / ry], axis=1)
        gradient /= np.linalg.norm(gradient, axis=1)[:, None]
        probes.append((base[:, None, :] + gradient[:, None, :] * offsets[None, :, None]).reshape(-1, 2))
    hi = (1 + s) * np.array([rx, ry]) + BAND + 0.05
    nx = int(np.clip(math.ceil((hi[0] + BAND) / 0.025), 40, 420))
    ny = int(np.clip(math.ceil((hi[1] + BAND) / 0.025), 40, 420))
    xx, yy = np.meshgrid(np.linspace(-BAND - 0.05, hi[0], nx),
                         np.linspace(-BAND - 0.05, hi[1], ny))
    probes.append(np.stack([xx.ravel(), yy.ravel()], axis=1))
    points = np.concatenate(probes)
    return points[(points[:, 0] >= -BAND - 0.05) & (points[:, 1] >= -BAND - 0.05)]


def evaluate_config(s, rx, ry, density, dtype):
    precision = 'float32' if dtype == np.float32 else 'float64'
    curves = build_corner(s, rx, ry, dtype)
    probes_from = [None if c is None else c.astype(np.float64) for c in curves]
    points = sample_points(probes_from, rx, ry, s, density)
    parts, rays = part_distances(points, curves, rx, ry, s)
    reference = np.minimum(parts.min(axis=1), rays)
    selected = select_segment(points, rx, ry, s, dtype)
    band = reference <= BAND + 1e-12
    # Selected-segment fragments: exact distance to the selected segment minus the
    # true outline distance. S4 fragments: fill assigns coverage 0, equivalent to
    # declaring distance >= BAND, so the distance-equivalent error is BAND - d_ref.
    chosen = np.full(len(points), np.inf)
    for part in (0, 1, 2):
        mask = selected == part
        chosen[mask] = parts[mask, part]
    seg_error = np.where(band & np.isfinite(chosen), chosen - reference, 0.0)
    s4_mask = band & (selected == -1)
    s4_error = np.where(s4_mask, BAND - reference, 0.0)
    error = np.maximum(seg_error, s4_error)
    worst = int(np.argmax(error)) if band.any() else 0
    return dict(smoothness=s, rx=rx, ry=ry, m=min(rx, ry), ratio=max(rx, ry) / min(rx, ry),
                safe_radius=min(rx, ry) ** 2 / (2 * max(rx, ry)), precision=precision,
                samples=len(points), band_samples=int(band.sum()),
                s4_band_samples=int(s4_mask.sum()), max_error=float(error[worst]),
                worst_point=points[worst].tolist(), worst_dref=float(reference[worst]),
                worst_selected=int(selected[worst]),
                worst_true_part=int(np.argmin(np.concatenate([parts[worst], [rays[worst]]]))))


def main():
    parser = argparse.ArgumentParser(description='Fill selection error sweep in the small-radius regime')
    parser.add_argument('--output', required=True)
    parser.add_argument('--density', type=int, default=257)
    args = parser.parse_args()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    smoothness = sorted(set(np.concatenate([np.geomspace(1e-8, 1e-4, 5),
                                            np.geomspace(1e-4, 0.05, 8),
                                            np.linspace(0.1, 0.9, 9),
                                            [0.95, 0.97, 0.99, 0.995, 0.999, 1.0]]).tolist()))
    min_radii = [0.5, 0.75, 1.0, 1.5, 2.0, 4.0]
    ratios = [1, 1.25, 1.5, 2, 3, 4, 6, 8]
    shapes = []
    for m in min_radii:
        for ratio in ratios:
            shapes.append((m, m * ratio))
            if ratio != 1:
                shapes.append((m * ratio, m))
    configs = [(s, rx, ry) for s in smoothness for rx, ry in shapes]
    metadata = dict(kind='configuration', band=BAND, tolerance=0.25, density=args.density,
                    configs=len(configs), numpy=np.__version__,
                    metric='selected-segment exact distance minus true outline distance, within the '
                           'fill AA band (d_ref <= 0.5px); S4 fragments count as BAND - d_ref',
                    reference='true arc (circle or ellipse) plus C1/C3 cubics and adjacent rays, float64',
                    note='Selection-only experiment; solver error is measured in the stroke sweep.')
    start = time.monotonic()
    records = []
    with output.open('w') as stream:
        stream.write(json.dumps(metadata) + '\n')
        for i, (s, rx, ry) in enumerate(configs):
            row = evaluate_config(s, rx, ry, args.density, np.float64)
            stream.write(json.dumps(row, allow_nan=False) + '\n')
            records.append(row)
            if (i + 1) % 200 == 0 or i + 1 == len(configs):
                print(json.dumps(dict(completed=i + 1, total=len(configs),
                                      elapsed_seconds=round(time.monotonic() - start, 2))), flush=True)
    # Convergence self-check: rerun the worst config of each (m, ratio) cell at
    # double density and under float32 selection/contour arithmetic.
    cells = {}
    for row in records:
        key = (row['m'], round(row['ratio'], 8))
        if key not in cells or row['max_error'] > cells[key]['max_error']:
            cells[key] = row
    checks = []
    for key, row in sorted(cells.items()):
        dense = evaluate_config(row['smoothness'], row['rx'], row['ry'], args.density * 2, np.float64)
        f32 = evaluate_config(row['smoothness'], row['rx'], row['ry'], args.density, np.float32)
        checks.append(dict(m=key[0], ratio=key[1],
                           error_dense=dense['max_error'], density_change=abs(dense['max_error'] - row['max_error']),
                           error_float32=f32['max_error'],
                           base_worst=dict(smoothness=row['smoothness'], rx=row['rx'], ry=row['ry'])))
    summary = dict(cells=[dict(m=k[0], ratio=k[1], **{f: v for f, v in cells[k].items()
                                                      if f not in ('m', 'ratio')})
                           for k in sorted(cells)],
                   convergence=checks,
                   max_density_change=max(c['density_change'] for c in checks),
                   max_float32_error=max(c['error_float32'] for c in checks))
    output.with_suffix('.summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False))
    print(json.dumps(dict(output=str(output), configs=len(configs),
                          max_error=max(r['max_error'] for r in records),
                          max_density_change=summary['max_density_change'],
                          max_float32_error=summary['max_float32_error'],
                          elapsed_seconds=round(time.monotonic() - start, 2))), flush=True)


if __name__ == '__main__':
    main()
