// HairlineQuadShader fragment shader
// Processor layout: HairlineQuadGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based anti-aliasing is enabled
// Uses Loop-Blinn quadratic curve anti-aliasing: u^2 - v = 0 defines the curve.
#version 450

#ifndef HAS_AA
#define HAS_AA 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
  float Coverage_P0;
};

layout(location = 0) in vec4 vHairQuadEdge;

layout(location = 0) out vec4 fragColor;

void main() {
  highp vec2 duvdx = dFdx(vHairQuadEdge.xy);
  highp vec2 duvdy = dFdy(vHairQuadEdge.xy);
  highp vec2 gF = vec2(2.0 * vHairQuadEdge.x * duvdx.x - duvdx.y,
                        2.0 * vHairQuadEdge.x * duvdy.x - duvdy.y);
  highp float edgeAlpha = vHairQuadEdge.x * vHairQuadEdge.x - vHairQuadEdge.y;
  edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));
  edgeAlpha = max(1.0 - edgeAlpha, 0.0);
#if !HAS_AA
  edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
#endif

  vec4 outputColor = Color_P0;
  vec4 outputCoverage = vec4(Coverage_P0 * edgeAlpha);

  fragColor = outputColor * outputCoverage;
}
