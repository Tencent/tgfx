// GlassUDFTentBlurShader fragment shader
// The tent weights are computed analytically (linear kernel), mirroring the runtime emission
// expression-for-expression so AOT and JIT renders stay bit-identical. The loop upper bound is the
// fixed compile-time constant 64; the actual radius comes from the GlassUDFRadius uniform (the
// component picked by the Field uniform) and the loop breaks early via the weight test.
// Permutation dimensions (frag):
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
//   The tiled-child tap path is a runtime uniform branch (TiledChild), mirroring GaussianBlur1D.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#define HAS_RUNTIME_DEVICE_MASK 1

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec2 GlassUDFRadius;
  vec2 GlassUDFStep;
  // Always declared: plain-child taps clamp to Subset (no-op at full bounds); a tiled child uses it
  // as the tiling domain.
  vec4 Subset;
  // The tiled-child fields are always declared: a plain child never reads them.
  int ShaderModeX;
  int ShaderModeY;
  vec4 Clamp;
  vec2 Dimension;
  int Field;
  int InputIsPacked;
  int TiledChild;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

// Always bound: an absent device mask is padded with the shared dummy texture and HasDeviceMask
// is 0.
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 2
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "tiled_sample.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec2 offset = GlassUDFStep;
  float radius = (Field == 1) ? GlassUDFRadius.y : GlassUDFRadius.x;
  const vec3 UNPACK24 = vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0);
  bool decodePacked = (Field == 0) && (InputIsPacked != 0);

  float sum = 0.0;
  float total = 0.0;
  int k = 1;
  float s = 1.0;
  for (int j = 0; j <= 64; ++j) {
    float offsetValue;
    float weight;
    if (j == 0) {
      offsetValue = 0.0;
      weight = radius;
    } else {
      float w1 = max(0.0, radius - float(k));
      float w2 = max(0.0, radius - float(k + 1));
      weight = w1 + w2;
      offsetValue = (weight > 0.0) ? s * (float(k) + w2 / weight) : 0.0;
    }
    if (weight <= 0.0) {
      break;
    }
    total += weight;
    vec2 sampleCoord = TransformedCoords_0 + offset * offsetValue;
    vec4 texColor = vec4(0.0);
    if (TiledChild != 0) {
      vec2 tapInCoord;
      vec2 tapSubsetCoord;
      vec2 tapClampedCoord;
      vec2 tapCoord = tiledMapCoord(sampleCoord, false, tapInCoord, tapSubsetCoord, tapClampedCoord);
      texColor = texture(TextureSampler_0, tapCoord);
      // RepeatLinearNone(3) seam blend, ported from the runtime GLSLTiledTextureEffect emission and
      // shared with gaussian_blur_1d.frag.
      bool repeatX = ShaderModeX == 3 && tapSubsetCoord.x != tapClampedCoord.x;
      bool repeatY = ShaderModeY == 3 && tapSubsetCoord.y != tapClampedCoord.y;
      if (repeatX || repeatY) {
        float errX = tapSubsetCoord.x - tapClampedCoord.x;
        float errY = tapSubsetCoord.y - tapClampedCoord.y;
        float repeatCoordX = errX > 0.0 ? Clamp.x : Clamp.z;
        float repeatCoordY = errY > 0.0 ? Clamp.y : Clamp.w;
        if (repeatX && repeatY) {
          vec4 repeatReadX =
              texture(TextureSampler_0, vec2(repeatCoordX, tapClampedCoord.y) * Dimension);
          vec4 repeatReadY =
              texture(TextureSampler_0, vec2(tapClampedCoord.x, repeatCoordY) * Dimension);
          vec4 repeatReadXY = texture(TextureSampler_0, vec2(repeatCoordX, repeatCoordY) * Dimension);
          texColor = mix(mix(texColor, repeatReadX, abs(errX)),
                         mix(repeatReadY, repeatReadXY, abs(errX)), abs(errY));
        } else if (repeatX) {
          vec4 repeatReadX =
              texture(TextureSampler_0, vec2(repeatCoordX, tapClampedCoord.y) * Dimension);
          texColor = mix(texColor, repeatReadX, errX);
        } else {
          vec4 repeatReadY =
              texture(TextureSampler_0, vec2(tapClampedCoord.x, repeatCoordY) * Dimension);
          texColor = mix(texColor, repeatReadY, errY);
        }
      }
      texColor = tiledApplyBorder(texColor, tapInCoord, tapSubsetCoord, tapClampedCoord);
    } else {
      sampleCoord = clamp(sampleCoord, Subset.xy, Subset.zw);
      texColor = texture(TextureSampler_0, sampleCoord);
    }
    float value = decodePacked ? dot(texColor.rgb, UNPACK24) : texColor.a;
    sum += value * weight;
    if (j > 0) {
      if (s < 0.0) {
        k += 2;
      }
      s = -s;
    }
  }
  sum /= total;

  vec4 fieldColor = vec4(0.0);
  if (Field == 1) {
    fieldColor = vec4(0.0, 0.0, 0.0, clamp(sum, 0.0, 1.0));
  } else {
    // Encode the fine field into RGB with true 24-bit precision; the fract chain pre-compensates
    // the 8-bit rounding of the following channel. The upper clamp is required because fract(1.0)
    // == 0.0 and interior UDF samples are exactly 1.0.
    float fineValue = clamp(sum, 0.0, 1.0 - 1.0 / 16581375.0);
    vec3 enc = fract(vec3(1.0, 255.0, 65025.0) * fineValue);
    enc.x -= enc.y / 255.0;
    enc.y -= enc.z / 255.0;
    fieldColor = vec4(enc, 0.0);
  }

#define TGFX_COVERAGE_SRC_COLOR fieldColor
#include "coverage_output.inc"
#include "xp_output.inc"
}
