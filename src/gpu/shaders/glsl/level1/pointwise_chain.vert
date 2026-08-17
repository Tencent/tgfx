// PointwiseChainShader vertex shader
// Feeds one TransformedCoords varying per texture leaf of the pointwise DAG. The leaf count is the
// TEXTURE_COUNT permutation (0 -> 0 leaves, 1 -> 1, 2 -> 2, 3 -> 4) because the varying layout is
// part of the vertex/fragment interface.
// Permutation dimensions (vert): GP_LAYOUT, HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, TEXTURE_COUNT.
// GP_LAYOUT: 0 = rect family (DefaultGP and QuadPerEdgeAAGP share it — the position always goes
// through the Matrix uniform, which DefaultGP fills with the view matrix and QuadPerEdgeAAGP with
// identity, bit-exact), 1 = EllipseGeometryProcessor (device-space vertices, carries the ellipse
// edge varyings whose per-pixel coverage is evaluated in the fragment stage).
#version 450

#ifndef GP_LAYOUT
#define GP_LAYOUT 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_UV_COORD
#define HAS_UV_COORD 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif

#if TEXTURE_COUNT == 0
#define NTEX 0
#else
#define NTEX 4
#endif

#if GP_LAYOUT == 1
// EllipseGP attribute order: position, [color], ellipseOffset, ellipseRadii. The color varying
// occupies location 2 when present.
#if HAS_COLOR
#define CHAIN_ELLIPSE_COLOR 1
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inEllipseOffset;
layout(location = 3) in vec4 inEllipseRadii;
#define CHAIN_TEX_LOC_BASE 3
#else
layout(location = 1) in vec2 inEllipseOffset;
layout(location = 2) in vec4 inEllipseRadii;
#define CHAIN_TEX_LOC_BASE 2
#endif
#else
#define CHAIN_TEX_LOC_BASE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_LAYOUT == 0
  mat3 Matrix;
#endif
  // The chain processor always exposes one own coord transform ahead of the leaf transforms:
  // index 0 is the gradient coordinate mapping (identity for chains without a gradient source),
  // and texture leaf k reads CoordTransformMatrix_{k+1}.
  mat3 CoordTransformMatrix_0;
#if NTEX >= 1
  mat3 CoordTransformMatrix_1;
#endif
#if NTEX >= 2
  mat3 CoordTransformMatrix_2;
#endif
#if NTEX >= 4
  mat3 CoordTransformMatrix_3;
  mat3 CoordTransformMatrix_4;
  // The mask-slot phantom child contributes a transform like every child; it is never read.
  mat3 CoordTransformMatrix_5;
#endif
#if GP_LAYOUT == 0 && HAS_UV_COORD
  // Per-target coordinate source: bit k set picks the uvCoord attribute for leaf k, bit 4 for the
  // gradient; a clear bit sources from aPosition. Atlas text sources the atlas leaf from
  // maskCoord (riding the uvCoord slot) but gradients and image leaves from position; every
  // other GP writes all-ones, which reproduces the previous shared coordSource behavior.
  int CoordSourceMask;
#endif
};

#if GP_LAYOUT == 0
layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
#endif
#if HAS_UV_COORD
// uvCoord follows the coverage slot when present (quads, meshes); atlas text buffers carry no
// coverage, so maskCoord lands directly after the position.
layout(location = 1 + HAS_COVERAGE) in vec2 uvCoord;
#endif
#if HAS_COLOR
layout(location = 1 + HAS_COVERAGE + HAS_UV_COORD) in vec4 inColor;
#endif
#else
layout(location = 0) in vec2 inPosition;
#endif

#if GP_LAYOUT == 1
layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out vec4 vEllipseRadii;
#if HAS_COLOR
layout(location = 2) out vec4 vColor;
#endif
#else
#if HAS_COVERAGE
layout(location = NTEX) out float vCoverage;
#endif
#if HAS_COLOR
// HAS_COLOR implies HAS_COVERAGE (ShouldCompile), so the color varying always follows it.
layout(location = NTEX + 1) out vec4 vColor;
#endif
#endif

#if NTEX >= 1
layout(location = CHAIN_TEX_LOC_BASE + 0) out vec3 TransformedCoords_0;
#endif
#if NTEX >= 2
layout(location = CHAIN_TEX_LOC_BASE + 1) out vec3 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = CHAIN_TEX_LOC_BASE + 2) out vec3 TransformedCoords_2;
layout(location = CHAIN_TEX_LOC_BASE + 3) out vec3 TransformedCoords_3;
#endif
// Gradient coordinate varying at a fixed location beyond every layout's varyings (ellipse with
// color and 4 texture coords tops out at location 6). Always emitted; the fragment stage reads
// it only when a chain slot is OP_GRADIENT.
layout(location = 7) out vec3 GradientCoords;

void main() {
#if GP_LAYOUT == 0 && HAS_UV_COORD
#define CHAIN_LEAF_SOURCE(k) ((((CoordSourceMask >> (k)) & 1) != 0) ? uvCoord : aPosition)
#define CHAIN_GRADIENT_SOURCE ((((CoordSourceMask >> 4) & 1) != 0) ? uvCoord : aPosition)
#elif GP_LAYOUT == 0
#define CHAIN_LEAF_SOURCE(k) aPosition
#define CHAIN_GRADIENT_SOURCE aPosition
#else
#define CHAIN_LEAF_SOURCE(k) inPosition
#define CHAIN_GRADIENT_SOURCE inPosition
#endif
#if GP_LAYOUT == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
#if HAS_COLOR
  vColor = inColor;
#endif
#else
  // EllipseGeometryProcessor vertices are pre-transformed to device space.
  highp vec2 position = inPosition;
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
#if HAS_COLOR
  vColor = inColor;
#endif
#endif
#if NTEX >= 1
  TransformedCoords_0 = (CoordTransformMatrix_1 * vec3(CHAIN_LEAF_SOURCE(0), 1.0)).xyz;
#endif
#if NTEX >= 2
  TransformedCoords_1 = (CoordTransformMatrix_2 * vec3(CHAIN_LEAF_SOURCE(1), 1.0)).xyz;
#endif
#if NTEX >= 4
  TransformedCoords_2 = (CoordTransformMatrix_3 * vec3(CHAIN_LEAF_SOURCE(2), 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_4 * vec3(CHAIN_LEAF_SOURCE(3), 1.0)).xyz;
#endif
  GradientCoords = (CoordTransformMatrix_0 * vec3(CHAIN_GRADIENT_SOURCE, 1.0)).xyz;
#if GP_LAYOUT == 0 && HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
