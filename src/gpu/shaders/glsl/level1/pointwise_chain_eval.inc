// Shared chain-slot evaluation, included exactly once. Slot uniform fields are std140 arrays read
// by runtime index (constant-memory reads), so the op bodies exist in the binary exactly once:
// the op-slot loop in main() cannot be unrolled because SlotCount is runtime data. Leaf slots
// (indices < NTEX) go through evalChainSlotWithTex with statically bound samplers; op slots share
// evalChainSlotNoTex and are never OP_TEXTURE by construction.
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

// ColorSpaceXform transfer functions (verbatim from pointwise_op.inc, reading the shared block).
float chainSrcTF(float x) {
  float s = sign(x);
  x = abs(x);
  float A = ChainSrcTF0[1], B = ChainSrcTF0[2], C = ChainSrcTF0[3];
  float D = ChainSrcTF1[0], E = ChainSrcTF1[1], F = ChainSrcTF1[2];
  if (ChainSrcTFType == 0) {
    float G = ChainSrcTF0[0];
    x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
  } else if (ChainSrcTFType == 1) {
    float xC = pow(x, C);
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
  } else if (ChainSrcTFType == 2) {
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
  }
  return s * x;
}

float chainDstTF(float x) {
  float s = sign(x);
  x = abs(x);
  float A = ChainDstTF0[1], B = ChainDstTF0[2], C = ChainDstTF0[3];
  float D = ChainDstTF1[0], E = ChainDstTF1[1], F = ChainDstTF1[2];
  if (ChainDstTFType == 0) {
    float G = ChainDstTF0[0];
    x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
  } else if (ChainDstTFType == 1) {
    float xC = pow(x, C);
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
  } else if (ChainDstTFType == 2) {
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
  }
  return s * x;
}

// Unrolled-binary gradient colorizer, verbatim from gradient_fill.frag (the runtime search tree
// structure is preserved exactly); IntervalCount is a runtime uniform so all interval counts
// share this path.
vec4 gradientColorizeUnrolled(float t) {
  vec4 scale;
  vec4 bias;
  if (GradientIntervalCount >= 4) {
    if (t < GradientThresholds1_7.w) {
      if (t < GradientThresholds1_7.y) {
        if (t < GradientThresholds1_7.x) {
          scale = GradientScale0_1;
          bias = GradientBias0_1;
        } else {
          scale = GradientScale2_3;
          bias = GradientBias2_3;
        }
      } else {
        if (t < GradientThresholds1_7.z) {
          scale = GradientScale4_5;
          bias = GradientBias4_5;
        } else {
          scale = GradientScale6_7;
          bias = GradientBias6_7;
        }
      }
    } else {
      if (GradientIntervalCount >= 6) {
        if (t < GradientThresholds9_13.y) {
          if (t < GradientThresholds9_13.x) {
            scale = GradientScale8_9;
            bias = GradientBias8_9;
          } else {
            scale = GradientScale10_11;
            bias = GradientBias10_11;
          }
        } else {
          if (GradientIntervalCount >= 7) {
            if (t < GradientThresholds9_13.z) {
              scale = GradientScale12_13;
              bias = GradientBias12_13;
            } else {
              scale = GradientScale14_15;
              bias = GradientBias14_15;
            }
          } else {
            scale = GradientScale12_13;
            bias = GradientBias12_13;
          }
        }
      } else {
        if (t < GradientThresholds9_13.x) {
          scale = GradientScale8_9;
          bias = GradientBias8_9;
        } else {
          scale = GradientScale10_11;
          bias = GradientBias10_11;
        }
      }
    }
  } else if (GradientIntervalCount >= 2) {
    if (t < GradientThresholds1_7.y) {
      if (t < GradientThresholds1_7.x) {
        scale = GradientScale0_1;
        bias = GradientBias0_1;
      } else {
        scale = GradientScale2_3;
        bias = GradientBias2_3;
      }
    } else {
      if (GradientIntervalCount >= 3) {
        if (t < GradientThresholds1_7.z) {
          scale = GradientScale4_5;
          bias = GradientBias4_5;
        } else {
          scale = GradientScale6_7;
          bias = GradientBias6_7;
        }
      } else {
        scale = GradientScale4_5;
        bias = GradientBias4_5;
      }
    }
  } else {
    scale = GradientScale0_1;
    bias = GradientBias0_1;
  }
  return vec4(t * scale + bias);
}

// Evaluates every non-texture op of slot `slot`, reading its fields from the uniform arrays.
vec4 evalChainSlotNoTex(int slot) {
  ivec4 Packed = SlotPacked[slot];
  if (Packed.x == OP_NONE) {
    return vec4(0.0);
  }
  vec4 in0 = Packed.y == -1 ? TGFX_CHAIN_GEOM_COLOR
                            : (Packed.y < 0 ? vec4(0.0) : chainResults[Packed.y]);
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
    vec4 in1 = Packed.z == -1 ? TGFX_CHAIN_GEOM_COLOR
                              : (Packed.z < 0 ? vec4(0.0) : chainResults[Packed.z]);
    return xpBlendColors(in0, in1, Packed.w);
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
    highp vec2 gradientCoord = GradientCoords;
    float gradT;
    if (GradientLayoutType == 1) {
      gradT = length(gradientCoord);
    } else if (GradientLayoutType == 2) {
      float angle = atan(-gradientCoord.y, -gradientCoord.x);
      gradT = ((angle * 0.15915494309180001 + 0.5) + GradientBias) * GradientScale;
    } else if (GradientLayoutType == 3) {
      gradT = max(abs(gradientCoord.x), abs(gradientCoord.y));
    } else {
      gradT = gradientCoord.x + 1.0000000000000001e-05;
    }
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
    } else {
      gradColor = gradientColorizeUnrolled(gradT);
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

// Texture-leaf variant: the sampler stays statically bound per slot (dynamic sampler indexing is
// not portable), while every other field comes from the uniform arrays by slot index.
vec4 evalChainSlotWithTex(int slot, sampler2D leafSampler, vec2 leafCoords, vec4 leafSubset,
                          int leafIndex) {
  ivec4 Packed = SlotPacked[slot];
  if (Packed.x == OP_TEXTURE) {
    vec4 texColor = chainLeafFetch(leafSampler, leafCoords, leafSubset, leafIndex);
    if ((Packed.w & 2) != 0) {
      // Alpha-only leaf (R8 on Metal returns (r,0,0,1)): splat .r into all channels so the alpha
      // channel carries the mask value, matching the runtime TextureEffect alpha-only readback.
      texColor = vec4(texColor.r);
    }
    if ((Packed.w & 1) != 0) {
      // Color-source leaves are modulated by the geometry color's alpha, matching the runtime
      // paint-alpha wrap. Mask and blend-operand leaves sample raw, as the runtime emits them.
      texColor *= TGFX_CHAIN_GEOM_COLOR.a;
    }
    return texColor;
  }
  return evalChainSlotNoTex(slot);
}
