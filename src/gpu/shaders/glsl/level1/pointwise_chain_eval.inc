// Defines one chain slot's eval function. Include once per slot, with pointwise_chain_bind.inc
// redirecting the uniform and function names to that slot beforehand. Each eval reads its inputs
// from the chainResults array (or the Color uniform for index -1) and returns the slot's output.
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

vec4 evalChainSlot() {
  if (Packed.x == OP_NONE) {
    return vec4(0.0);
  }
  if (Packed.x == OP_TEXTURE) {
    vec4 texColor = chainTexFetch;
    if ((Packed.w & 2) != 0) {
      // Alpha-only leaf (R8 on Metal returns (r,0,0,1)): splat .r into every channel so the alpha
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
  vec4 in0 = Packed.y == -1 ? TGFX_CHAIN_GEOM_COLOR : (Packed.y < 0 ? vec4(0.0) : chainResults[Packed.y]);
  if (Packed.x == OP_CONST_COLOR) {
    if (Packed.w == 1) {
      return ConstColor * in0;
    }
    if (Packed.w == 2) {
      return ConstColor * in0.a;
    }
    return ConstColor;
  }
  if (Packed.x == OP_BLEND) {
    vec4 in1 = Packed.z == -1 ? TGFX_CHAIN_GEOM_COLOR : (Packed.z < 0 ? vec4(0.0) : chainResults[Packed.z]);
    return xpBlendColors(in0, in1, Packed.w);
  }
  if (Packed.x == OP_AARECT_COVERAGE) {
    // Analytic AA-rect coverage (identical math to GLSLAARectEffect): falloff from the fragment's
    // distance to the rect edges, evaluated in destination device coordinates via gl_FragCoord.
    highp vec4 dists4 =
        clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - CoverageRect), 0.0, 1.0);
    highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
    return in0 * (dists2.x * dists2.y);
  }
  vec4 color = in0;
  vec4 result;
  if (Packed.x == OP_COLOR_MATRIX) {
    // Unpremultiply, apply matrix + vector, clamp, re-premultiply.
    color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
    color = clamp(ColorMatrix * color + ColorVector, 0.0, 1.0);
    color.rgb *= color.a;
    result = color;
  } else if (Packed.x == OP_LUMA) {
    result = vec4(dot(color.rgb, LumaThresh.rgb));
  } else if (Packed.x == OP_ALPHA_THRESHOLD) {
    result = vec4(0.0);
    if (color.a > 0.0) {
      result.rgb = color.rgb / color.a;
      result.a = step(LumaThresh.a, color.a);
      result = clamp(result, 0.0, 1.0);
    }
  } else {  // OP_COLOR_SPACE_XFORM
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
    result = color;
  }
  return result;
}
