// GlassRefractionShader fragment shader
// Processor layout: EllipseGeometryProcessor(common color) + GlassRefractionFragmentProcessor(
// GlassSDF or GlassUDF geometry child). The refraction body, the SDF/UDF geometry math, and the
// dispersion/lighting branches mirror the runtime emissions expression-for-expression; the
// geometry child selects GEOMETRY_KIND because it changes the sampler layout.
// Permutation dimensions (frag):
//   GEOMETRY_KIND (0~3): 0=SDF rounded rect, 1=SDF ellipse, 2=UDF, 3=UDF + edge light
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
#version 450

#ifndef GEOMETRY_KIND
#define GEOMETRY_KIND 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int StrokeEnabled;
  // Refraction optics (mirrors GlassOpticsP0..P3 of the runtime emission).
  vec4 GlassOpticsP0;
  vec4 GlassOpticsP1;
  vec4 GlassOpticsP2;
  vec4 GlassOpticsP3;
  // Geometry params (mirrors GlassShapeP0/P1; the UDF-only fields stay declared for the SDF
  // kinds, which never read them).
  vec4 GlassShapeP0;
  vec4 GlassShapeP1;
  vec4 GlassFineMaskUV;
  vec2 GlassEdgeSpan;
  vec4 GlassEdgeMaskUV;
  // Runtime branches replacing the runtime emission's static dispersion/lighting branches.
  int DispersionOn;
  int LightingOn;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
layout(location = 2) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
#if GEOMETRY_KIND >= 2
layout(set = 1, binding = 1) uniform sampler2D MaskSampler;
#endif
#if GEOMETRY_KIND == 3
layout(set = 1, binding = 2) uniform sampler2D EdgeMaskSampler;
#define XP_DST_TEX_BINDING 3
#elif GEOMETRY_KIND == 2
#define XP_DST_TEX_BINDING 2
#else
#define XP_DST_TEX_BINDING 1
#endif
#include "ellipse_coverage.inc"
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "clip_coverage.inc"

#if GEOMETRY_KIND <= 1

// The SDF functions and gradient math mirror GLSLGlassSDFGeometryFragmentProcessor::emitCode
// expression-for-expression. glassUV arrives with the refraction emission's orientation; the
// geometry emission flips it once (EmitGeometryCoordinates).
float glassShapeSDFRoundedRect(float px, float py, float hw, float hh, float r) {
  float dx = abs(px) - hw + r;
  float dy = abs(py) - hh + r;
  float outside = length(max(vec2(dx, dy), vec2(0.0)));
  return outside + min(max(dx, dy), 0.0) - r;
}

float glassShapeSDFEllipse(float px, float py, float hw, float hh) {
  float normalizedDist = length(vec2(px / hw, py / hh));
  float gradientLength = length(vec2(px / (hw * hw), py / (hh * hh)));
  if (gradientLength < 0.000001) {
    return -min(hw, hh);
  }
  return normalizedDist * (normalizedDist - 1.0) / gradientLength;
}

vec4 glassGeometry(vec2 glassUV) {
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassShapeP0.x;
  float halfH = GlassShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  float cornerRadius = GlassShapeP0.z;
#if GEOMETRY_KIND == 0
  float outerSDF = glassShapeSDFRoundedRect(px, py, halfW, halfH, cornerRadius);
#else
  float outerSDF = glassShapeSDFEllipse(px, py, halfW, halfH);
#endif
  vec4 result = vec4(0.0);
  if (outerSDF < 0.0) {
    float edgeDist = -outerSDF;
    float edgeBand = max(1.0, GlassShapeP1.w);
    float edgeWeight = 1.0 - smoothstep(0.0, edgeBand, edgeDist);
    float rd = max(GlassShapeP0.w, 0.0001);
    float heightT = clamp((rd - edgeDist) / rd, 0.0, 1.0);
    float offsetDist = min(GlassShapeP1.x * rd * pow(heightT, 3.5), 0.999 * rd);
    float effectiveSplay = GlassShapeP1.y;
#if GEOMETRY_KIND == 1
    vec2 sdfGradient = vec2(px / (halfW * halfW), py / (halfH * halfH));
    float gradientLength = length(sdfGradient);
    if (gradientLength > 0.000001) {
      vec2 gradientDir = -sdfGradient / gradientLength;
#else
    vec2 absP = abs(vec2(px, py));
    float refractionDistance = GlassShapeP0.w;
    float r_effective = min(min(halfW, halfH), max(cornerRadius, refractionDistance));
    vec2 cornerCenter = vec2(halfW - r_effective, halfH - r_effective);
    vec2 relToCorner = absP - cornerCenter;
    float compensation = max(-min(relToCorner.x, relToCorner.y), 0.0);
    vec2 grad = relToCorner + vec2(compensation);
    float qx = absP.x - cornerCenter.x;
    float qy = absP.y - cornerCenter.y;
    float straightHalfW = max(cornerCenter.x, 0.0001);
    float straightHalfH = max(cornerCenter.y, 0.0001);
    float cornerWeightX = smoothstep(0.0, straightHalfW, absP.x);
    float cornerWeightY = smoothstep(0.0, straightHalfH, absP.y);
    float cornerWeight = (qx > 0.0 && qy > 0.0)
                             ? 1.0
                             : ((qx > qy) ? cornerWeightY : cornerWeightX);
    effectiveSplay = min(cornerWeight + GlassShapeP1.y, 1.0);
    float gradientLength = length(grad);
    if (gradientLength > 0.000001) {
      vec2 gradientDir = -vec2(sign(px) * grad.x, sign(py) * grad.y) / gradientLength;
#endif
      float centerDistance = length(vec2(px, py));
      vec2 centerDir =
          centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
      vec2 refractDir = mix(gradientDir, centerDir, effectiveSplay);
      float refractLength = length(refractDir);
      refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
      result = vec4(refractDir, offsetDist, edgeWeight);
    }
  }
  return result;
}

#else

// The UDF gradient reconstruction mirrors GLSLGlassUDFGeometryFragmentProcessor::emitCode
// expression-for-expression: the fine mask packs the refraction height into RGB, the edge mask
// carries the edge light height in A, and the edge center difference spans the tent radius that
// produced the edge field.
vec4 glassGeometry(vec2 glassUV) {
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassShapeP0.x;
  float halfH = GlassShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  const vec3 UNPACK24 = vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0);
  vec2 maskUV = vec2(glassUV.x, 1.0 - glassUV.y);
  vec2 fineUV = maskUV * GlassFineMaskUV.xy + GlassFineMaskUV.zw;
  vec4 packedCenter = texture(MaskSampler, fineUV);
  float height = dot(packedCenter.rgb, UNPACK24);
  float gradientBase = GlassShapeP1.z * 3.0 + 1.0;
  vec2 gradientStep = gradientBase * GlassShapeP0.zw;
  vec2 gradientUVStep = gradientStep * vec2(0.5 / halfW, 0.5 / halfH) * GlassFineMaskUV.xy;
  vec4 packedRight = texture(MaskSampler, fineUV + vec2(gradientUVStep.x, 0.0));
  vec4 packedUp = texture(MaskSampler, fineUV - vec2(0.0, gradientUVStep.y));
  float deltaRight = dot(packedRight.rgb, UNPACK24) - height;
  float deltaUp = dot(packedUp.rgb, UNPACK24) - height;
  vec2 gradient = vec2(deltaRight, deltaUp) / gradientStep;
  float gradientLength = length(gradient);
  float gradientSignal = max(abs(deltaRight), abs(deltaUp));
  float gradientWeight = smoothstep(0.001, 0.005, gradientSignal);
  float edgeWeight = 0.0;
#if GEOMETRY_KIND == 3
  vec2 edgeUV = maskUV * GlassEdgeMaskUV.xy + GlassEdgeMaskUV.zw;
  float edgeHeight = texture(EdgeMaskSampler, edgeUV).a;
  vec2 edgeUVStep = GlassEdgeSpan *
                    vec2(0.25 / max(halfW, 0.0001), 0.25 / max(halfH, 0.0001)) *
                    GlassEdgeMaskUV.xy;
  vec4 packedEdgeRight = texture(EdgeMaskSampler, edgeUV + vec2(edgeUVStep.x, 0.0));
  vec4 packedEdgeLeft = texture(EdgeMaskSampler, edgeUV - vec2(edgeUVStep.x, 0.0));
  vec4 packedEdgeUp = texture(EdgeMaskSampler, edgeUV - vec2(0.0, edgeUVStep.y));
  vec4 packedEdgeDown = texture(EdgeMaskSampler, edgeUV + vec2(0.0, edgeUVStep.y));
  vec2 edgeGradient =
      vec2((packedEdgeRight.a - packedEdgeLeft.a) / max(GlassEdgeSpan.x, 0.0001),
           (packedEdgeUp.a - packedEdgeDown.a) / max(GlassEdgeSpan.y, 0.0001));
  float edgeGradientLength = max(length(edgeGradient), 0.0001);
  float edgeDistance = max((edgeHeight - 0.5) / edgeGradientLength, 0.0);
  float edgeBand = max(1.0, GlassShapeP1.w);
  edgeWeight = 1.0 - smoothstep(0.0, edgeBand, edgeDistance);
#endif
  vec4 result = vec4(0.0);
  if (gradientLength > 0.000001 && gradientWeight > 0.000001) {
    vec2 gradientDir = gradient / gradientLength;
    float centerDistance = length(vec2(px, py));
    vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
    vec2 refractDir = mix(gradientDir, centerDir, GlassShapeP1.y);
    float refractLength = length(refractDir);
    refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
    float depthScale = smoothstep(0.0, 0.1, GlassShapeP1.z);
    float distance = min(halfW, halfH) * GlassShapeP1.x * GlassShapeP1.z * depthScale *
                     gradientWeight;
    float proximity = (1.0 - height * height) * (1.0 - height * height) * 1.2;
    result = vec4(refractDir, distance * proximity, edgeWeight);
  }
  return result;
}

#endif

layout(location = 0) out vec4 fragColor;

void main() {
  // The refraction body mirrors GLSLGlassRefractionFragmentProcessor::emitCode
  // expression-for-expression; the static dispersion/lighting branches of the runtime emission
  // ride the DispersionOn/LightingOn uniforms here with identical math.
  vec2 sourceUV = TransformedCoords_0 * GlassOpticsP0.xy;
  vec2 glassUV = TransformedCoords_0 * GlassOpticsP2.xy + GlassOpticsP3.xy;

  vec4 geometry = glassGeometry(glassUV);
  vec2 refractDir = geometry.xy;
  float offsetDist = geometry.z;
  float edgeWeight = geometry.w;
  vec2 displacement = refractDir * offsetDist;
  displacement = clamp(displacement, vec2(-GlassOpticsP0.w), vec2(GlassOpticsP0.w));
  vec2 uvOffset = vec2(displacement.x * GlassOpticsP1.x, -displacement.y * GlassOpticsP1.y);

  vec3 finalColor;
  float srcAlpha;
  if (DispersionOn != 0) {
    vec2 uvR = sourceUV + uvOffset * (1.0 + GlassOpticsP0.z);
    vec2 uvG = sourceUV + uvOffset;
    vec2 uvB = sourceUV + uvOffset * (1.0 - GlassOpticsP0.z);
    vec4 srcG = texture(TextureSampler_0, uvG);
    finalColor.r = texture(TextureSampler_0, uvR).r;
    finalColor.g = srcG.g;
    finalColor.b = texture(TextureSampler_0, uvB).b;
    srcAlpha = srcG.a;
  } else {
    vec4 srcColor = texture(TextureSampler_0, sourceUV + uvOffset);
    finalColor = srcColor.rgb;
    srcAlpha = srcColor.a;
  }

  if (LightingOn != 0) {
    if (edgeWeight > 0.0) {
      float NdotL = dot(-refractDir, GlassOpticsP1.zw);
      float diffuse = smoothstep(0.35, 1.0, NdotL) * edgeWeight * GlassOpticsP2.z;
      float rim = smoothstep(0.35, 1.0, -NdotL) * edgeWeight * GlassOpticsP2.z * 0.6;
      finalColor += vec3(diffuse + rim);
    }
  }
  vec4 outputColor = vec4(finalColor, srcAlpha);

  // The ellipse edge antialiasing enters as the initial coverage, mirroring
  // EllipseFillShader: coverage_output.inc multiplies the clip term on top and hands
  // xp_output.inc the uncovered color plus the total coverage.
  highp float edgeAlpha = ellipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, StrokeEnabled);

#define TGFX_COVERAGE_SRC_COLOR outputColor
#define TGFX_INITIAL_COVERAGE vec4(edgeAlpha)
#include "coverage_output.inc"
#include "xp_output.inc"
}
