#!/bin/bash
# Verifies the shader bundles published in resources/shaders against the current shader sources.
#
# resources/shaders holds the bundles that builds which cannot generate their own consume: a cross
# build on a Linux host (the tool's dependencies are not vendored for Linux) and a Web build
# configured with TGFX_WEB_PREBUILT_BUNDLES. A shader change or an ABI bump that is not followed by
# republishing leaves those builds on stale shaders without any other check noticing. This fails
# when a required bundle is missing, its manifest is missing or belongs to another file, its
# toolchain ABI is not the runtime's, or the sources it was built from differ from the ones in the
# checkout.
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

# The bundles that are published: opengles for the GLES-family builds (Android, iOS, OHOS, WebGL)
# and webgpu for the WebGPU build. Keep this in step with BACKENDS in publish_bundles.sh. Other
# backends are not published; every build that can generate its bundle does so at build time.
REQUIRED_BACKENDS=opengles,webgpu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)

"$TOOL" --verify-bundle "$ROOT/resources/shaders" \
  --shader-dir "$ROOT/src/gpu/shaders/glsl" \
  --require-backends "$REQUIRED_BACKENDS"
status=$?

if [ $status -ne 0 ]; then
  cat >&2 <<EOF

The published shader bundles in resources/shaders are stale, missing or inconsistent.
Republish them with a shader_build_tool that was built with WebGPU support
(-DTGFX_BUILD_WEBGPU_BUNDLE=ON), then commit the result (the .bin files are stored in Git LFS):

  tools/shader_build_tool/publish_bundles.sh <path/to/shader_build_tool>

or, from a build directory configured that way:

  ninja tgfx_publish_shader_bundles
EOF
fi
exit $status
