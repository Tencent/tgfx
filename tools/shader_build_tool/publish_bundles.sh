#!/bin/bash
# Regenerates the shader bundles published under resources/shaders and replaces them there.
#
# resources/shaders holds the bundles that builds which cannot generate their own consume: a cross
# build on a Linux host (shaderc and SPIRV-Cross are not vendored for Linux, so the tool cannot be
# built there) and any Web build configured with TGFX_WEB_PREBUILT_BUNDLES. Everything else
# generates its bundle during the build. Only opengles and webgpu are published; the .bin files are
# stored in Git LFS and the .manifest next to each is plain text.
#
# What this does, in this order, and why it stops where it does:
#   1. generates both bundles into a scratch directory, never into resources/shaders;
#   2. verifies that output against the current sources (header, identity hash, pool completeness,
#      manifest, and the source digest, the same check CI runs on the published copies);
#   3. leaves resources/shaders untouched and says so when the output is identical to what is there;
#   4. otherwise copies each changed file next to its destination under a temporary name, checks the
#      copy byte for byte, and only then renames them into place (a rename within one directory is
#      atomic, so no reader sees a partial file and a failure before this step changes nothing);
#   5. verifies resources/shaders again as the last word.
# The renames are not atomic as a group. An interruption between them leaves a .bin whose manifest
# is the previous one, which verification reports (the manifest records the hash of its .bin), so
# running this script again, or verify_published_bundles.sh, always shows the state.
#
# Usage: publish_bundles.sh <path/to/shader_build_tool> [--cache-dir <dir>]
#
# The tool must have been built with WebGPU support (-DTGFX_BUILD_WEBGPU_BUNDLE=ON). --cache-dir is
# passed to the tool so repeated runs only recompile stages whose input changed.
#
# The bytes are reproducible on one machine. Whether they are across machines (compiler, tint and
# zstd versions) has not been established, so a clean diff after publishing from another machine
# means the bundle was regenerated, not that anything is wrong; verification judges freshness by the
# source digest in the manifest, not by the bytes.
#
# Nothing is committed or pushed; reviewing and committing the result is up to the caller.

set -u

BACKENDS=opengles,webgpu

usage() {
  echo "usage: $0 <shader_build_tool> [--cache-dir <dir>]" >&2
}

if [ $# -lt 1 ]; then
  usage
  exit 2
fi
TOOL=$1
shift
CACHE_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --cache-dir)
      if [ $# -lt 2 ]; then
        usage
        exit 2
      fi
      CACHE_ARGS=(--cache-dir "$2")
      shift 2
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done
if [ ! -x "$TOOL" ]; then
  echo "error: $TOOL is not an executable shader_build_tool" >&2
  exit 2
fi

ROOT=${TGFX_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
SHADER_DIR=$ROOT/src/gpu/shaders/glsl
DEST=$ROOT/resources/shaders
if [ ! -d "$SHADER_DIR" ] || [ ! -d "$DEST" ]; then
  echo "error: $ROOT is not a tgfx checkout (missing src/gpu/shaders/glsl or resources/shaders)" >&2
  exit 2
fi

WORK=$(mktemp -d)
STAGED=()
cleanup() {
  for f in ${STAGED[@]+"${STAGED[@]}"}; do
    rm -f "$f"
  done
  rm -rf "$WORK"
}
trap cleanup EXIT

IFS=, read -r -a BACKEND_LIST <<<"$BACKENDS"

echo "[publish] generating $BACKENDS from $SHADER_DIR"
mkdir -p "$WORK/out"
if ! "$TOOL" --shader-dir "$SHADER_DIR" --out-dir "$WORK/out" --backends "$BACKENDS" --compress \
  ${CACHE_ARGS[@]+"${CACHE_ARGS[@]}"} >"$WORK/generate.log" 2>&1; then
  echo "[publish] FAILED: the tool could not generate the bundles; resources/shaders is unchanged" >&2
  tail -n 15 "$WORK/generate.log" >&2
  if grep -q "WebGPU translation is unavailable" "$WORK/generate.log"; then
    echo "[publish] this tool was built without WebGPU support; use one built with" \
      "-DTGFX_BUILD_WEBGPU_BUNDLE=ON" >&2
  fi
  exit 1
fi

if ! "$TOOL" --verify-bundle "$WORK/out" --shader-dir "$SHADER_DIR" \
  --require-backends "$BACKENDS" >"$WORK/verify_new.log" 2>&1; then
  echo "[publish] FAILED: the freshly generated bundles do not verify; resources/shaders is" \
    "unchanged" >&2
  cat "$WORK/verify_new.log" >&2
  exit 1
fi

CHANGED=()
for backend in "${BACKEND_LIST[@]}"; do
  for ext in bin manifest; do
    name=shader_bundle.$backend.$ext
    if [ ! -f "$WORK/out/$name" ]; then
      echo "[publish] FAILED: the tool did not write $name; resources/shaders is unchanged" >&2
      exit 1
    fi
    if ! cmp -s "$WORK/out/$name" "$DEST/$name"; then
      CHANGED+=("$name")
    fi
  done
done

verify_published() {
  "$TOOL" --verify-bundle "$DEST" --shader-dir "$SHADER_DIR" --require-backends "$BACKENDS" \
    >"$WORK/verify_published.log" 2>&1
}

if [ ${#CHANGED[@]} -eq 0 ]; then
  if ! verify_published; then
    echo "[publish] FAILED: the published bundles equal the generated ones yet do not verify" >&2
    cat "$WORK/verify_published.log" >&2
    exit 1
  fi
  echo "[publish] resources/shaders is already up to date; nothing was changed"
  exit 0
fi

# Stage every changed file beside its destination first, so the failure that is most likely (no
# space, no permission) happens before anything has been replaced.
for name in "${CHANGED[@]}"; do
  staged="$DEST/.$name.publish.$$"
  STAGED+=("$staged")
  if ! cp "$WORK/out/$name" "$staged" || ! cmp -s "$WORK/out/$name" "$staged"; then
    echo "[publish] FAILED: cannot stage $name in $DEST; resources/shaders is unchanged" >&2
    exit 1
  fi
done

# Bundles before manifests: a manifest never points at a bundle that is not there yet.
ORDERED=()
for name in "${CHANGED[@]}"; do
  case "$name" in *.bin) ORDERED+=("$name") ;; esac
done
for name in "${CHANGED[@]}"; do
  case "$name" in *.manifest) ORDERED+=("$name") ;; esac
done
for name in "${ORDERED[@]}"; do
  if ! mv -f "$DEST/.$name.publish.$$" "$DEST/$name"; then
    echo "[publish] FAILED while replacing $name; resources/shaders may now mix old and new files." \
      "Run this script again." >&2
    exit 1
  fi
done
STAGED=()

if ! verify_published; then
  echo "[publish] FAILED: the published bundles do not verify after replacing them" >&2
  cat "$WORK/verify_published.log" >&2
  exit 1
fi

echo "[publish] replaced ${#CHANGED[@]} file(s) in resources/shaders:"
for name in "${CHANGED[@]}"; do
  printf '[publish]   %-40s %s bytes\n' "$name" "$(wc -c <"$DEST/$name" | tr -d ' ')"
done
echo "[publish] verified. Review with git status and commit them (the .bin files go to Git LFS)"
exit 0
