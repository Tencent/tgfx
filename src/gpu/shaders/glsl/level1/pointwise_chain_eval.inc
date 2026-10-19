// Shared chain-slot evaluation, included exactly once. Slot uniform fields are std140 arrays read
// by runtime index (constant-memory reads), so the op bodies exist in the binary exactly once:
// the slot loop in main() cannot be unrolled because SlotCount is runtime data. Leaf texture
// sampling happens in main() with statically bound samplers (dynamic sampler indexing is not
// portable); the OP_TEXTURE branch of evalChainSlot only applies the leaf modulation flags to the
// prefetched sample.
//
// Resource ops (gradient and noise sources) are not evaluated here: a slot that none of the core
// ops below claim goes to the kernel entry's evalResourceSlot(), which dispatches to the resource
// modules the entry includes, and only then falls through to OP_COLOR_SPACE_XFORM. The
// ColorSpaceXform parameters are shared chain-wide (ChainCSFlags & friends): the matcher
// guarantees at most one slot per chain uses them.

#include "pointwise_chain_ops.inc"
#include "pointwise_math.inc"

#include "transfer_function.inc"

#include "analytic_clip_math.inc"

// ColorSpaceXform transfer functions bound to the shared chain-wide parameter block.
vec3 chainSrcTF(vec3 x) {
  return transferTF(x, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType);
}

vec3 chainDstTF(vec3 x) {
  return transferTF(x, ChainDstTF0, ChainDstTF1, ChainDstTFType);
}

#if HAS_XP >= 1
// The operands of the XferProcessor's advanced blend pass (xp_blend_pass.inc), set by main()
// before it evaluates slot SlotCount.
vec4 chainXPSrc = vec4(0.0);
vec4 chainXPDst = vec4(0.0);
#endif

// Evaluates slot `slot`, reading its fields from the uniform arrays. OP_TEXTURE slots read the
// prefetched sample from chainLeafTex (fetched in main with statically bound samplers) and apply
// the leaf modulation flags; every other op shares this single implementation so the op library
// exists exactly once in the binary.
//
// With a dst-reading XferProcessor, slot SlotCount is the XferProcessor's advanced blend pass: an
// OP_BLEND of chainXPSrc and chainXPDst in XPBlendMode that writes no register, so the final
// composite shares the instruction loop's blend site instead of inlining xpBlendColors again.
vec4 evalChainSlot(int slot) {
  ivec4 Packed;
#if HAS_XP >= 1
  if (slot == SlotCount) {
    Packed = ivec4(OP_BLEND, -4, -4, XPBlendMode);
  } else {
    Packed = SlotPacked[slot];
  }
#else
  Packed = SlotPacked[slot];
#endif
  if (Packed.x == OP_NONE) {
    return vec4(0.0);
  }
  if (Packed.x == OP_TEXTURE) {
    vec4 texColor = chainLeafTex[slot];
    if ((Packed.w & 2) != 0) {
      // Alpha-only leaf (R8 on Metal returns (r,0,0,1)): splat .r into all channels so the alpha
      // channel carries the mask value, matching the runtime TextureEffect alpha-only readback.
      texColor = vec4(texColor.r);
      if ((Packed.w & 8) != 0) {
        // A two-child blend operand: the xfer emission feeds each child vec4(inputColor.rgb,
        // 1.0), so the mask modulates by the geometry RGB with opaque alpha.
        texColor *= vec4(TGFX_CHAIN_GEOM_COLOR.rgb, 1.0);
      }
      if ((Packed.w & 16) != 0) {
        // The color root: the runtime readback multiplies the mask by its FULL input color
        // (sample.a * inputColor), one multiply over the premul geometry color — RGB and alpha
        // in a single step (the former bit0|bit3 pair applied the paint alpha to the RGB twice).
        texColor *= TGFX_CHAIN_GEOM_COLOR;
      }
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
  vec4 in1 = Packed.z == -1   ? TGFX_CHAIN_GEOM_COLOR
             : Packed.z == -3 ? TGFX_CHAIN_UNIT_COVERAGE
             : Packed.z == -4 ? vec4(1.0)
             : Packed.z == -5 ? vec4(TGFX_CHAIN_GEOM_COLOR.rgb, 1.0)
             : Packed.z < 0   ? vec4(0.0)
                              : chainResults[Packed.z];
  if (Packed.x == OP_TEX_MODULATE) {
    // A texture whose runtime input is a computed value splits into a raw sampling slot in the
    // leading block (static sampler binding) plus this modulate instruction in topological
    // order. chainLeafTex is prefetched in main and never recycled, so any later slot may read
    // it by index. The two modulations mirror the runtime TextureEffect readback line for line
    // (GLSLTextureEffect::emitCode): alpha-only is sample.a * inputColor, RGBA is
    // sample * inputColor.a. The source slot ordinal and the alpha-only flag ride the
    // selector (bits 16-19 and bit 0), not Packed.z, because the register allocator rewrites
    // every non-negative Packed.y/Packed.z into a result register index.
    int srcSlot = (Packed.w >> 16) & 0xF;
    vec4 tex = chainLeafTex[srcSlot];
    if ((Packed.w & 1) != 0) {
      return vec4(tex.r) * in0;
    }
    return tex * in0.a;
  }
  if (Packed.x == OP_INPUT_OPAQUE) {
    // A two-child xfer whose input is a computed node C feeds its children
    // vec4(inputColor.rgb, 1.0); this instruction materializes that child input environment
    // as an ordinary chain value (the runtime's alpha override applied to C).
    return vec4(in0.rgb, 1.0);
  }
  if (Packed.x == OP_MUL_ALPHA) {
    // The two-child xfer epilogue re-multiplies the blended output by the xfer input's alpha;
    // with a computed input C that alpha is C.a, so the multiply is an explicit instruction
    // rather than the selector's fixed geometry-alpha bit.
    return in0 * in1.a;
  }
  if (Packed.x == OP_CONST_COLOR) {
    // The input mode occupies the selector's low bits; bits 20-24 carry the result register.
    int constMode = Packed.w & 0xF;
    return pmApplyConstColor(SlotConstColor[slot], in0, constMode);
  }
  if (Packed.x == OP_BLEND) {
    vec4 blendSrc = in0;
    vec4 blendDst = in1;
#if HAS_XP >= 1
    if (slot == SlotCount) {
      blendSrc = chainXPSrc;
      blendDst = chainXPDst;
    }
#endif
    vec4 blendResult = xpBlendColors(blendSrc, blendDst, Packed.w & 0xFF);
    // Bit 8 of the selector: the two-child xfer epilogue re-multiplies by the input color's
    // alpha, which at the color root is the geometry color's alpha.
    if ((Packed.w & 0x100) != 0) {
      blendResult *= TGFX_CHAIN_GEOM_COLOR.a;
    }
    return blendResult;
  }
  vec4 color = in0;
  if (Packed.x == OP_COLOR_MATRIX) {
    return pmApplyColorMatrix(color, SlotColorMatrix[slot], SlotColorVector[slot]);
  }
  if (Packed.x == OP_LUMA) {
    return pmApplyLuma(color, SlotLumaThresh[slot].rgb);
  }
  if (Packed.x == OP_ALPHA_THRESHOLD) {
    return pmApplyAlphaThreshold(color, SlotLumaThresh[slot].a);
  }
  if (Packed.x == OP_AARECT_COVERAGE) {
    // Analytic AA-rect coverage (identical math to GLSLRectEffect): falloff from the fragment's
    // distance to the rect edges, evaluated in destination device coordinates via gl_FragCoord.
    // Bits 16-19 of the selector carry this slot's ordinal into the CoverageRect array.
    int rectOrdinal = (Packed.w >> 16) & 0xF;
    highp vec4 dists4 =
        clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - CoverageRect[rectOrdinal]),
              0.0, 1.0);
    highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
    return in0 * (dists2.x * dists2.y);
  }
  if (Packed.x == OP_LOCAL_RECT_COVERAGE) {
    // Analytic AA-rect coverage in the clip's local space: map gl_FragCoord through the
    // device-to-local matrix, then the shared separable half-pixel ramp. Bits 16-19 carry the
    // slot's ordinal into the CoverageLocalRect arrays.
    int localRectOrdinal = (Packed.w >> 16) & 0xF;
    return in0 * vec4(analyticLocalRectCoverage(CoverageLocalRect[localRectOrdinal],
                                                CoverageLocalDeviceToLocal[localRectOrdinal]));
  }
  if (Packed.x == OP_RRECT_COVERAGE) {
    // Analytic rrect coverage; bits 16-19 of the selector carry this slot's ordinal into the
    // CoverageRRect* parameter arrays.
    int rrectOrdinal = (Packed.w >> 16) & 0xF;
    return in0 * vec4(analyticRRectCoverage(CoverageRRectRect[rrectOrdinal],
                                            CoverageRRectRadiiX[rrectOrdinal],
                                            CoverageRRectRadiiY[rrectOrdinal],
                                            CoverageRRectAntiAlias[rrectOrdinal],
                                            CoverageRRectDeviceToLocal[rrectOrdinal]));
  }
  vec4 resourceValue = vec4(0.0);
  if (evalResourceSlot(Packed, in0, resourceValue)) {
    return resourceValue;
  }
  // OP_COLOR_SPACE_XFORM
  if ((ChainCSFlags & CS_UNPREMUL) != 0) {
    float a = color.a;
    color = (a > 0.0) ? vec4(color.rgb / a, a) : vec4(0.0);
  }
  if ((ChainCSFlags & CS_LINEARIZE) != 0) {
    color.rgb = chainSrcTF(color.rgb);
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
    color.rgb = chainDstTF(color.rgb);
  }
  if ((ChainCSFlags & CS_PREMUL) != 0) {
    color.rgb *= color.a;
  }
  return color;
}
