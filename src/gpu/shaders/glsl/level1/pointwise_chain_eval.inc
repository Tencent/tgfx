// Shared chain-slot evaluation, included exactly once. Slot uniform fields are std140 arrays read
// by runtime index (constant-memory reads), so the op bodies exist in the binary exactly once:
// the slot loop in main() cannot be unrolled because SlotCount is runtime data. Leaf texture
// sampling happens in main() with statically bound samplers (dynamic sampler indexing is not
// portable); the OP_TEXTURE branch of evalChainSlot only applies the leaf modulation flags to the
// prefetched sample.
//
// Op values mirror AOTChainOp and are part of the precompiled kernel ABI. The ColorSpaceXform
// parameters are shared chain-wide (ChainCSFlags & friends): the matcher guarantees at most one
// slot per chain uses them.

#ifndef TGFX_CHAIN_OP_CONSTANTS
#define TGFX_CHAIN_OP_CONSTANTS
#define OP_COLOR_MATRIX 0
#define OP_LUMA 1
#define OP_ALPHA_THRESHOLD 2
#define OP_COLOR_SPACE_XFORM 3
#define OP_NONE 4
#define OP_TEXTURE 5
#define OP_CONST_COLOR 6
#define OP_BLEND 7
#define OP_AARECT_COVERAGE 8
#define OP_GRADIENT 9

#define CS_UNPREMUL 1
#define CS_LINEARIZE 2
#define CS_GAMUT 4
#define CS_ENCODE 8
#define CS_PREMUL 16
#define CS_SRC_OOTF 32
#define CS_DST_OOTF 64
#endif

#include "gradient_layout.inc"
#include "gradient_colorize.inc"

#include "transfer_function.inc"

// ColorSpaceXform transfer functions bound to the shared chain-wide parameter block.
float chainSrcTF(float x) {
  return transferTF(x, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType);
}

float chainDstTF(float x) {
  return transferTF(x, ChainDstTF0, ChainDstTF1, ChainDstTFType);
}

// Evaluates slot `slot`, reading its fields from the uniform arrays. OP_TEXTURE slots read the
// prefetched sample from chainLeafTex (fetched in main with statically bound samplers) and apply
// the leaf modulation flags; every other op shares this single implementation so the op library
// exists exactly once in the binary.
vec4 evalChainSlot(int slot) {
  ivec4 Packed = SlotPacked[slot];
  if (Packed.x == OP_NONE) {
    return vec4(0.0);
  }
  if (Packed.x == OP_TEXTURE) {
    vec4 texColor = chainLeafTex[slot];
    if ((Packed.w & 2) != 0) {
      // Alpha-only leaf (R8 on Metal returns (r,0,0,1)): splat .r into all channels so the alpha
      // channel carries the mask value, matching the runtime TextureEffect alpha-only readback.
      texColor = vec4(texColor.r);
    }
    if ((Packed.w & 4) != 0) {
      // Coverage-root leaf: modulated by the coverage unit's alpha, matching the runtime
      // coverage-FP readback (tex * coverageIn.a).
      texColor *= TGFX_CHAIN_UNIT_COVERAGE.a;
    }
    if ((Packed.w & 1) != 0) {
      // Color-source leaves are modulated by the geometry color's alpha, matching the runtime
      // paint-alpha wrap. Mask and blend-operand leaves sample raw, as the runtime emits them.
      texColor *= TGFX_CHAIN_GEOM_COLOR.a;
    }
    return texColor;
  }
  vec4 in0 = Packed.y == -1   ? TGFX_CHAIN_GEOM_COLOR
             : Packed.y == -3 ? TGFX_CHAIN_UNIT_COVERAGE
             : Packed.y == -4 ? vec4(1.0)
             : Packed.y == -5 ? vec4(TGFX_CHAIN_GEOM_COLOR.rgb, 1.0)
             : Packed.y < 0   ? vec4(0.0)
                              : chainResults[Packed.y];
  if (Packed.x == OP_CONST_COLOR) {
    vec4 ConstColor = SlotConstColor[slot];
    if (Packed.w == 1) {
      return ConstColor * in0;
    }
    if (Packed.w == 2) {
      return ConstColor * in0.a;
    }
    return ConstColor;
  }
  if (Packed.x == OP_BLEND) {
    vec4 in1 = Packed.z == -1   ? TGFX_CHAIN_GEOM_COLOR
               : Packed.z == -3 ? TGFX_CHAIN_UNIT_COVERAGE
               : Packed.z == -4 ? vec4(1.0)
               : Packed.z == -5 ? vec4(TGFX_CHAIN_GEOM_COLOR.rgb, 1.0)
               : Packed.z < 0   ? vec4(0.0)
                                : chainResults[Packed.z];
    vec4 blendResult = xpBlendColors(in0, in1, Packed.w & 0xFF);
    // Bit 8 of the selector: the two-child xfer epilogue re-multiplies by the input color's
    // alpha, which at the color root is the geometry color's alpha.
    if ((Packed.w & 0x100) != 0) {
      blendResult *= TGFX_CHAIN_GEOM_COLOR.a;
    }
    return blendResult;
  }
  vec4 color = in0;
  if (Packed.x == OP_COLOR_MATRIX) {
    // Unpremultiply, apply matrix + vector, clamp, re-premultiply.
    color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
    color = clamp(SlotColorMatrix[slot] * color + SlotColorVector[slot], 0.0, 1.0);
    color.rgb *= color.a;
    return color;
  }
  if (Packed.x == OP_LUMA) {
    return vec4(dot(color.rgb, SlotLumaThresh[slot].rgb));
  }
  if (Packed.x == OP_ALPHA_THRESHOLD) {
    vec4 result = vec4(0.0);
    if (color.a > 0.0) {
      result.rgb = color.rgb / color.a;
      result.a = step(SlotLumaThresh[slot].a, color.a);
      result = clamp(result, 0.0, 1.0);
    }
    return result;
  }
  if (Packed.x == OP_AARECT_COVERAGE) {
    // Analytic AA-rect coverage (identical math to GLSLAARectEffect): falloff from the fragment's
    // distance to the rect edges, evaluated in destination device coordinates via gl_FragCoord.
    highp vec4 dists4 =
        clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - CoverageRect), 0.0, 1.0);
    highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
    return in0 * (dists2.x * dists2.y);
  }
  if (Packed.x == OP_GRADIENT) {
    // Analytic gradient source. The layout math and border clamp are verbatim from the dedicated
    // gradient kernels; the colorizer kind selects between the single-interval mix, the
    // dual-interval scale/bias pair and the unrolled-binary tree, each byte-matching its
    // dedicated kernel. The epilogue premultiplies and modulates by the input alpha, matching
    // the runtime ClampedGradientEffect emission.
    highp vec2 gradientCoord = GradientCoords.xy / GradientCoords.z;
    float gradT = gradientLayoutT(GradientLayoutType, GradientBias, GradientScale, gradientCoord);
    vec4 gradColor;
    if (gradT <= 0.0) {
      gradColor = GradientLeftBorder;
    } else if (gradT >= 1.0) {
      gradColor = GradientRightBorder;
    } else if (GradientColorizerKind == 0) {
      gradColor = mix(GradientStart, GradientEnd, gradT);
    } else if (GradientColorizerKind == 1) {
      vec4 gradScale = GradientScale01;
      vec4 gradBias = GradientBias01;
      if (gradT >= GradientThreshold) {
        gradScale = GradientScale23;
        gradBias = GradientBias23;
      }
      gradColor = vec4(gradT * gradScale + gradBias);
    } else if (GradientColorizerKind == 3) {
      // LUT colorizer, matching the dedicated texture_gradient kernel: sample the baked gradient
      // texture at the computed t. The LUT child occupies a leaf slot for sampler binding only;
      // GradientLUTLeaf selects it with static indices. Unreachable when NTEX == 0 (a LUT chain
      // always carries the child), but the branch must still compile for those variants.
      gradColor = vec4(0.0);
#if NTEX > 0
      highp vec2 lutCoord = vec2(gradT, 0.5);
      if (GradientLUTLeaf == 0) {
        gradColor = texture(TextureSampler_0, lutCoord);
      }
#if NTEX > 1
      if (GradientLUTLeaf == 1) {
        gradColor = texture(TextureSampler_1, lutCoord);
      }
#endif
#endif
    } else {
      gradColor = gradientColorizeUnrolled(
          gradT, GradientIntervalCount, GradientThresholds1_7, GradientThresholds9_13,
          GradientScale0_1, GradientBias0_1, GradientScale2_3, GradientBias2_3, GradientScale4_5,
          GradientBias4_5, GradientScale6_7, GradientBias6_7, GradientScale8_9, GradientBias8_9,
          GradientScale10_11, GradientBias10_11, GradientScale12_13, GradientBias12_13,
          GradientScale14_15, GradientBias14_15);
    }
    gradColor.rgb *= gradColor.a;
    return gradColor * in0.a;
  }
  // OP_COLOR_SPACE_XFORM
  if ((ChainCSFlags & CS_UNPREMUL) != 0) {
    float a = color.a;
    color = (a > 0.0) ? vec4(color.rgb / a, a) : vec4(0.0);
  }
  if ((ChainCSFlags & CS_LINEARIZE) != 0) {
    color.r = chainSrcTF(color.r);
    color.g = chainSrcTF(color.g);
    color.b = chainSrcTF(color.b);
  }
  if ((ChainCSFlags & CS_SRC_OOTF) != 0) {
    float srcY = dot(color.rgb, ChainSrcOOTF.rgb);
    color.rgb *= sign(srcY) * pow(abs(srcY), ChainSrcOOTF.a);
  }
  if ((ChainCSFlags & CS_GAMUT) != 0) {
    color.rgb = ChainColorXform * color.rgb;
  }
  if ((ChainCSFlags & CS_DST_OOTF) != 0) {
    float dstY = dot(color.rgb, ChainDstOOTF.rgb);
    color.rgb *= sign(dstY) * pow(abs(dstY), ChainDstOOTF.a);
  }
  if ((ChainCSFlags & CS_ENCODE) != 0) {
    color.r = chainDstTF(color.r);
    color.g = chainDstTF(color.g);
    color.b = chainDstTF(color.b);
  }
  if ((ChainCSFlags & CS_PREMUL) != 0) {
    color.rgb *= color.a;
  }
  return color;
}

