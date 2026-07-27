// NoiseShader fragment shader
// Perlin noise implementation with two LUT textures (permutations + gradient vectors).
// MAX_OCTAVES is a FIXED compile-time constant (max supported octaves), NOT a permutation dimension.
// The loop bound is MAX_OCTAVES; the actual octave count comes from the NumOctaves uniform at
// runtime and the loop breaks early once reached. Keeping octaves as a uniform (not a variant)
// avoids multiplying the fragment domain by 8.
// Permutation dimensions (frag):
//   NOISE_TYPE (0~1): 0=FractalNoise, 1=Turbulence
//   STITCH_TILES (bool): Whether tile stitching is enabled
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
//   HAS_COVERAGE (0~2): 0=none, 1=AARectEffect, 2=AARectEffect+mask
#version 450

#ifndef NOISE_TYPE
#define NOISE_TYPE 0
#endif
#ifndef STITCH_TILES
#define STITCH_TILES 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

// Fixed maximum octaves; runtime NumOctaves uniform controls the actual loop count.
#define MAX_OCTAVES 8

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec2 baseFrequency;
  float numOctaves;
  int padding_;
#if STITCH_TILES
  vec2 stitchData;
#endif
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

// LUT textures: permutations (256x1 A8) and noise gradients (256x4 RGBA).
layout(set = 1, binding = 0) uniform sampler2D PermutationsSampler;
layout(set = 1, binding = 1) uniform sampler2D NoiseSampler;

#if HAS_COVERAGE == 2
layout(set = 1, binding = 2) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 3
#else
#define XP_DST_TEX_BINDING 2
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

// Channel Y coordinates for the noise texture (256x4, each row is one channel).
// Texel center Y: (row + 0.5) / 4.0 → row 0=0.125, 1=0.375, 2=0.625, 3=0.875
const float chanCoord[4] = float[4](0.125, 0.375, 0.625, 0.875);

layout(location = 0) out vec4 fragColor;

void main() {
  // Sub-lattice bias prevents integer baseFrequency from collapsing to flat grey.
  highp vec2 noiseVec = TransformedCoords_0 * baseFrequency + vec2(0.0078125);
  vec4 color = vec4(0.0);
  float ratio = 1.0;
#if STITCH_TILES
  highp vec2 stitchScale = stitchData;
#endif

  int octaveCount = int(numOctaves);

  for (int octave = 0; octave < MAX_OCTAVES; ++octave) {
    if (octave >= octaveCount) {
      break;
    }

    highp vec4 floorVal;
    floorVal.xy = floor(noiseVec);
    floorVal.zw = floorVal.xy + vec2(1.0);
    highp vec2 fractVal = fract(noiseVec);

    // Hermite interpolation.
    vec2 noiseSmooth = smoothstep(0.0, 1.0, fractVal);

#if STITCH_TILES
    floorVal -= step(stitchScale.xyxy, floorVal) * stitchScale.xyxy;
#endif

    // Wrap floorVal into [0, 256) for LUT indexing.
    floorVal = mod(floorVal, 256.0);

    // Look up permutation values. Permutations texture is 256x1 A8.
    // Texel center: (i + 0.5) / 256.0
    highp float permX = texture(PermutationsSampler, vec2((floorVal.x + 0.5) / 256.0, 0.5)).r;
    highp float permZ = texture(PermutationsSampler, vec2((floorVal.z + 0.5) / 256.0, 0.5)).r;

    // Recover [0,255] index from [0,1] texture value.
    highp vec2 latticeIdx = floor(vec2(permX, permZ) * 255.0 + 0.5);

    // bcoords: (latticeIdx + floorY) mod 256, used to index into noise texture.
    highp vec4 bcoords = mod(latticeIdx.xyxy + floorVal.yyww, 256.0);

    vec4 noiseResult;

    // Channel 0 (R)
    {
      vec4 lattice = texture(NoiseSampler, vec2((bcoords.x + 0.5) / 256.0, chanCoord[0]));
      float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);
      lattice = texture(NoiseSampler, vec2((bcoords.y + 0.5) / 256.0, chanCoord[0]));
      float v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
                    fractVal - vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(NoiseSampler, vec2((bcoords.w + 0.5) / 256.0, chanCoord[0]));
      v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(1.0, 1.0));
      lattice = texture(NoiseSampler, vec2((bcoords.z + 0.5) / 256.0, chanCoord[0]));
      u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.x = mix(a, b, noiseSmooth.y);
    }

    // Channel 1 (G)
    {
      vec4 lattice = texture(NoiseSampler, vec2((bcoords.x + 0.5) / 256.0, chanCoord[1]));
      float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);
      lattice = texture(NoiseSampler, vec2((bcoords.y + 0.5) / 256.0, chanCoord[1]));
      float v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
                    fractVal - vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(NoiseSampler, vec2((bcoords.w + 0.5) / 256.0, chanCoord[1]));
      v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(1.0, 1.0));
      lattice = texture(NoiseSampler, vec2((bcoords.z + 0.5) / 256.0, chanCoord[1]));
      u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.y = mix(a, b, noiseSmooth.y);
    }

    // Channel 2 (B)
    {
      vec4 lattice = texture(NoiseSampler, vec2((bcoords.x + 0.5) / 256.0, chanCoord[2]));
      float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);
      lattice = texture(NoiseSampler, vec2((bcoords.y + 0.5) / 256.0, chanCoord[2]));
      float v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
                    fractVal - vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(NoiseSampler, vec2((bcoords.w + 0.5) / 256.0, chanCoord[2]));
      v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(1.0, 1.0));
      lattice = texture(NoiseSampler, vec2((bcoords.z + 0.5) / 256.0, chanCoord[2]));
      u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.z = mix(a, b, noiseSmooth.y);
    }

    // Channel 3 (A)
    {
      vec4 lattice = texture(NoiseSampler, vec2((bcoords.x + 0.5) / 256.0, chanCoord[3]));
      float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);
      lattice = texture(NoiseSampler, vec2((bcoords.y + 0.5) / 256.0, chanCoord[3]));
      float v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
                    fractVal - vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(NoiseSampler, vec2((bcoords.w + 0.5) / 256.0, chanCoord[3]));
      v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(1.0, 1.0));
      lattice = texture(NoiseSampler, vec2((bcoords.z + 0.5) / 256.0, chanCoord[3]));
      u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.w = mix(a, b, noiseSmooth.y);
    }

    // Accumulate this octave.
#if NOISE_TYPE == 0
    color += noiseResult * ratio;
#else
    color += abs(noiseResult) * ratio;
#endif

    noiseVec *= 2.0;
    ratio *= 0.5;
#if STITCH_TILES
    stitchScale *= 2.0;
#endif
  }

  // FractalNoise: map from [-1,1] to [0,1].
#if NOISE_TYPE == 0
  color = color * 0.5 + 0.5;
#endif

  // Clamp each channel to [0,1], then output premultiplied RGBA.
  color = clamp(color, 0.0, 1.0);
  vec4 noiseOutput = vec4(color.rgb * color.aaa, color.a);

#define TGFX_COVERAGE_SRC_COLOR noiseOutput
#include "coverage_output.inc"
#include "xp_output.inc"
}
