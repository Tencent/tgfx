#!/bin/bash
# Verifies the shader bundles published in resources/shaders against the current shader sources.
#
# resources/shaders holds prebuilt copies that web builds consume (an Emscripten build cannot run
# shader_build_tool), so a shader change that is not followed by republishing leaves web builds on
# stale shaders without any other check noticing. This fails when a required bundle is missing,
# its manifest is missing or belongs to another file, or the sources it was built from differ from
# the ones in the checkout.
#
# Usage: verify_published_bundles.sh <path/to/shader_build_tool>
#
# The tool does not need to be built with WebGPU support: verification never compiles shaders.

set -u

if [ $# -ne 1 ]; then
  echo "usage: $0 <shader_build_tool>" >&2
  exit 2
fi
TOOL=$1

# The bundles web builds read: opengles for the WebGL build, webgpu for the WebGPU build. Keep this
# in step with the WEB branch of CMakeLists.txt (TGFX_PREBUILT_BUNDLE_DIR). Other backends are not
# published; native builds generate their bundle at build time and embed it.
REQUIRED_BACKENDS=opengles,webgpu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)

"$TOOL" --verify-bundle "$ROOT/resources/shaders" \
  --shader-dir "$ROOT/src/gpu/shaders/glsl" \
  --require-backends "$REQUIRED_BACKENDS"
status=$?

if [ $status -ne 0 ]; then
  cat >&2 <<EOF

The published shader bundles in resources/shaders are stale, missing or inconsistent.
Regenerate them with a shader_build_tool that was built with WebGPU support and commit the result
(the .bin files are stored in Git LFS):

  shader_build_tool --shader-dir src/gpu/shaders/glsl --out-dir resources/shaders \\
      --backends $REQUIRED_BACKENDS --compress

Then run this script again.
EOF
fi
exit $status
