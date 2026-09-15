#!/usr/bin/env python3
"""Three-way scene diff (audit F02 / tasks 5-6).

Compares the raw RGBA scene outputs produced by ThreeWayDiffDriver across three paths:
A (merge-base baseline), B (branch, TGFX_AOT_DISABLE=1), C (branch, AOT enabled).

Per scene and per pair it reports:
  - maxChannelDiff: largest per-channel absolute difference (0-255)
  - pixel counts differing at all and by more than 2
  - max/mean premultiplied-channel difference bucketed by the reference side's alpha
    (exactly 0, 1-31, 32-127, 128-255), so low-alpha unpremultiply amplification is visible
    instead of being averaged away

The script only measures; it does not decide tolerances. Read the per-scene buckets before
classifying a scene as quantization-equivalent, quality-acceptable, or a semantic error.
"""
import argparse
import json
import pathlib
import sys

ALPHA_BUCKETS = [("alpha=0", lambda a: a == 0),
                 ("alpha 1-31", lambda a: 1 <= a < 32),
                 ("alpha 32-127", lambda a: 32 <= a < 128),
                 ("alpha 128-255", lambda a: a >= 128)]


def load_manifest(directory):
    manifest_path = directory / "manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"missing manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    scenes = sorted(p.stem for p in directory.glob("*.rgba"))
    if scenes != sorted(manifest["scenes"]):
        raise SystemExit(f"scene files do not match manifest in {directory}")
    return manifest, scenes


def load_scene(directory, name, size):
    data = (directory / f"{name}.rgba").read_bytes()
    expected = size * size * 4
    if len(data) != expected:
        raise SystemExit(f"{name}: expected {expected} bytes, got {len(data)}")
    return data


def compare(reference, candidate, size):
    max_channel = 0
    diff_any = 0
    diff_gt2 = 0
    buckets = {label: [0, 0, 0] for label, _ in ALPHA_BUCKETS}  # pixels, max, sum
    for offset in range(0, len(reference), 4):
        alpha = reference[offset + 3]
        pixel_diff = 0
        for channel in range(4):
            value = abs(reference[offset + channel] - candidate[offset + channel])
            if value > max_channel:
                max_channel = value
            if value > pixel_diff:
                pixel_diff = value
        if pixel_diff > 0:
            diff_any += 1
        if pixel_diff > 2:
            diff_gt2 += 1
        for label, contains in ALPHA_BUCKETS:
            if contains(alpha):
                entry = buckets[label]
                entry[0] += 1
                entry[1] = max(entry[1], pixel_diff)
                entry[2] += pixel_diff
                break
    return {
        "maxChannelDiff": max_channel,
        "pixelsDiffering": diff_any,
        "pixelsDifferingByMoreThan2": diff_gt2,
        "buckets": {
            label: {"pixels": count, "maxDiff": peak, "meanDiff": (total / count if count else 0.0)}
            for label, (count, peak, total) in buckets.items()
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=pathlib.Path, help="path A: merge-base baseline output")
    parser.add_argument("runtime", type=pathlib.Path, help="path B: branch TGFX_AOT_DISABLE output")
    parser.add_argument("aot", type=pathlib.Path, help="path C: branch AOT-enabled output")
    parser.add_argument("--out", type=pathlib.Path, default=None, help="write results.json here")
    args = parser.parse_args()

    manifests = {}
    scenes = None
    size = None
    for label, directory in (("A", args.baseline), ("B", args.runtime), ("C", args.aot)):
        manifest, names = load_manifest(directory)
        manifests[label] = manifest
        if scenes is None:
            scenes = names
            size = manifest["size"]
        elif names != scenes or manifest["revision"] != manifests["A"]["revision"]:
            raise SystemExit(f"manifest mismatch for {label}: scenes or revision differ from A")

    results = {"revision": manifests["A"]["revision"], "size": size, "pairs": {}}
    pairs = (("A_vs_B", args.baseline, args.runtime, "main vs branch-runtime"),
             ("B_vs_C", args.runtime, args.aot, "branch-runtime vs branch-AOT"),
             ("A_vs_C", args.baseline, args.aot, "main vs branch-AOT"))
    exit_code = 0
    for pair_name, reference_dir, candidate_dir, meaning in pairs:
        pair_results = {}
        for name in scenes:
            reference = load_scene(reference_dir, name, size)
            candidate = load_scene(candidate_dir, name, size)
            stats = compare(reference, candidate, size)
            pair_results[name] = stats
            bucket_note = " ".join(
                f"{label}:max={stats['buckets'][label]['maxDiff']}" for label, _ in ALPHA_BUCKETS)
            print(f"[{pair_name}] {name}: maxChannelDiff={stats['maxChannelDiff']} "
                  f"differing={stats['pixelsDiffering']}/{size * size} "
                  f">2={stats['pixelsDifferingByMoreThan2']} | {bucket_note}")
        results["pairs"][pair_name] = {"meaning": meaning, "scenes": pair_results}

    if args.out is not None:
        args.out.write_text(json.dumps(results, indent=2) + "\n")
        print(f"results written to {args.out}")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
