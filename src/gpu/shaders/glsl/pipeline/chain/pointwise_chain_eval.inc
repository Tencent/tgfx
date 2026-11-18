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

#ifndef TGSL_POINTWISE_CHAIN_EVAL_INCLUDED
#define TGSL_POINTWISE_CHAIN_EVAL_INCLUDED

#pragma tgfx_spec_reader

#include "operator/pointwise_math.inc"
#include "operator/pointwise_color_dispatch.inc"

#include "coverage/analytic_clip_math.inc"

#if HAS_XP >= 1
// The operands of the XferProcessor's advanced blend pass (xp_blend_pass.inc), set by main()
// before it evaluates slot SlotCount.
vec4 chainXPSrc = vec4(0.0);
vec4 chainXPDst = vec4(0.0);
#endif

// OP_COLOR_SPACE_XFORM's parameters are the program-level block, not per-slot registers, so the
// transform is its own function: a text gate can only wrap a whole body, and a chain whose slots
// carry no ColorSpaceXform never reaches the fallthrough that calls this. The gate is closed for
// every libpag draw (the operator is unused there), and the stub's value is never observed.
#pragma tgfx_spec_gate ColorSpace : (SPEC_UsedOps() & (1 << OP_COLOR_SPACE_XFORM)) != 0
vec4 chainApplyColorSpaceXform(vec4 color) {
  PM_APPLY_COLOR_SPACE_XFORM(color, ChainCSFlags, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType,
                             ChainSrcOOTF, ChainColorXform, ChainDstOOTF, ChainDstTF0,
                             ChainDstTF1, ChainDstTFType)
  return color;
}

// OP_TEXTURE's modulation stage: only a chain whose slots carry a texture leaf reaches it, so a
// chain without one closes the gate and the runtime splices the branch out of the text.
#pragma tgfx_spec_gate TextureSlot : (SPEC_UsedOps() & (1 << OP_TEXTURE)) != 0
vec4 chainEvalTextureSlot(int slot, int selector) {
  vec4 texColor = chainLeafTex[slot];
  if ((selector & 2) != 0) {
    // Alpha-only leaf (R8 on Metal returns (r,0,0,1)): splat .r into all channels so the alpha
    // channel carries the mask value, matching the runtime TextureEffect alpha-only readback.
    texColor = aotTextureAlphaOnly(texColor);
    if ((selector & 8) != 0) {
      // A two-child blend operand: the xfer emission feeds each child vec4(inputColor.rgb, 1.0),
      // so the mask modulates by the geometry RGB with opaque alpha.
      texColor = aotTextureModulate(texColor, vec4(TGFX_CHAIN_GEOM_COLOR.rgb, 1.0), true);
    }
    if ((selector & 16) != 0) {
      // The color root: the runtime readback multiplies the mask by its FULL input color
      // (sample.a * inputColor), one multiply over the premul geometry color — RGB and alpha
      // in a single step (the bit0|bit3 pair applied the paint alpha to the RGB twice).
      texColor = aotTextureModulate(texColor, TGFX_CHAIN_GEOM_COLOR, true);
    }
  }
  if ((selector & 4) != 0) {
    // Coverage-root leaf: modulated by the coverage unit's alpha, matching the runtime
    // coverage-FP readback (tex * coverageIn.a).
    texColor = aotTextureModulate(texColor, TGFX_CHAIN_UNIT_COVERAGE, false);
  }
  if ((selector & 1) != 0) {
    // Color-source leaves are modulated by the geometry color's alpha, matching the runtime
    // paint-alpha wrap. Mask and blend-operand leaves sample raw, as the runtime emits them.
    texColor = aotTextureModulate(texColor, TGFX_CHAIN_GEOM_COLOR, false);
  }
  return texColor;
}

// A texture whose runtime input is a computed value splits into a raw sampling slot in the
// leading block (static sampler binding) plus this modulate instruction in topological order.
// chainLeafTex is prefetched in main and never recycled, so any later slot may read it by index.
// The two modulations follow the TextureEffect readback: alpha-only is sample.a * inputColor,
// RGBA is sample * inputColor.a. The source slot ordinal and the alpha-only flag ride the
// selector (bits 16-19 and bit 0), not Packed.z, because the register allocator rewrites every
// non-negative Packed.y/Packed.z into a result register index.
#pragma tgfx_spec_gate TexModulate : (SPEC_UsedOps() & (1 << OP_TEX_MODULATE)) != 0
vec4 chainEvalTexModulate(int slot, int selector, vec4 in0) {
  int srcSlot = (selector >> 16) & 0xF;
  vec4 tex = chainLeafTex[srcSlot];
  if ((selector & 1) != 0) {
    tex = aotTextureAlphaOnly(tex);
  }
  return aotTextureModulate(tex, in0, (selector & 1) != 0);
}

// The blend site serves both a chain's Blend slots and the xfer's advanced blend pass (slot
// SlotCount synthesizes an OP_BLEND), so the gate is the combined fact, not the op bit alone.
#pragma tgfx_spec_gate BlendSlot : SPEC_NeedsBlendMath() != 0
vec4 chainEvalBlend(int slot, int selector, vec4 in0, vec4 in1) {
  vec4 blendSrc = in0;
  vec4 blendDst = in1;
#if HAS_XP >= 1
  if (slot == SPEC_SlotCount()) {
    blendSrc = chainXPSrc;
    blendDst = chainXPDst;
  }
#endif
  vec4 blendResult = xpBlendColors(blendSrc, blendDst, selector & 0xFF);
  // Bit 8 of the selector: the two-child xfer epilogue re-multiplies by the input color's alpha,
  // which at the color root is the geometry color's alpha.
  if ((selector & 0x100) != 0) {
    blendResult *= TGFX_CHAIN_GEOM_COLOR.a;
  }
  return blendResult;
}

// Analytic AA-rect coverage (identical math to GLSLRectEffect): falloff from the fragment's
// distance to the rect edges, evaluated in destination device coordinates via gl_FragCoord.
// Shared by the interpreter's dispatch and the composed executor, so the formula exists once.
vec4 chainAARectCoverage(vec4 in0, vec4 rect) {
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - rect), 0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  return in0 * (dists2.x * dists2.y);
}

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
  if (slot == SPEC_SlotCount()) {
    Packed = ivec4(OP_BLEND, -4, -4, XPBlendMode);
  } else {
    Packed = SPEC_SlotPacked(slot);
  }
#else
  Packed = SPEC_SlotPacked(slot);
#endif
  // This slot's parameter block in the data pool: one block per slot, whatever operator it holds.
  int dataAt = slot * TGFX_CHAIN_DATA_SLOT_WORDS;
  if (Packed.x == OP_NONE) {
    return vec4(0.0);
  }
  if (Packed.x == OP_TEXTURE) {
    return chainEvalTextureSlot(slot, Packed.w);
  }
  vec4 in0 = Packed.y == -1   ? chainGeomColorSource()
             : Packed.y == -3 ? chainUnitCoverageSource()
             : Packed.y == -4 ? vec4(1.0)
             : Packed.y == -5 ? chainOpaqueGeomColorSource()
             : Packed.y < 0   ? vec4(0.0)
                              : chainResults[Packed.y];
  vec4 in1 = Packed.z == -1   ? chainGeomColorSource()
             : Packed.z == -3 ? chainUnitCoverageSource()
             : Packed.z == -4 ? vec4(1.0)
             : Packed.z == -5 ? chainOpaqueGeomColorSource()
             : Packed.z < 0   ? vec4(0.0)
                              : chainResults[Packed.z];
  if (Packed.x == OP_TEX_MODULATE) {
    return chainEvalTexModulate(slot, Packed.w, in0);
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
  if (Packed.x == OP_CONST_COLOR || Packed.x == OP_COLOR_MATRIX || Packed.x == OP_LUMA ||
      Packed.x == OP_ALPHA_THRESHOLD) {
    // The shared dispatch owns the operator-to-register mapping
    // (operator/pointwise_color_dispatch.inc); this interpreter supplies the slot's register
    // values and the selector word (the ConstColor input mode rides its low byte).
    return evalPointwiseColorOp(Packed.x, in0, dataAt, Packed.w);
  }
  if (Packed.x == OP_BLEND) {
    return chainEvalBlend(slot, Packed.w, in0, in1);
  }
  if (Packed.x == OP_AARECT_COVERAGE) {
    AARectCoverageData d = chainLoadAARectCoverage(dataAt);
    return chainAARectCoverage(in0, d.rect);
  }
  if (Packed.x == OP_LOCAL_RECT_COVERAGE) {
    // Analytic AA-rect coverage in the clip's local space: map gl_FragCoord through the
    // device-to-local matrix, then the shared separable half-pixel ramp.
    LocalRectCoverageData d = chainLoadLocalRectCoverage(dataAt);
    return in0 * vec4(analyticLocalRectCoverage(d.rect, d.deviceToLocal));
  }
  if (Packed.x == OP_RRECT_COVERAGE) {
    RRectCoverageData d = chainLoadRRectCoverage(dataAt);
    return aotApplyRRectCoverage(in0, d.rect, d.radiiX, d.radiiY, d.antiAlias, d.deviceToLocal);
  }
  vec4 resourceValue = vec4(0.0);
  if (evalResourceSlot(Packed, in0, dataAt, resourceValue)) {
    return resourceValue;
  }
  // OP_COLOR_SPACE_XFORM (the registered exception: its parameters are the program-level block,
  // not per-slot registers, so it does not ride the shared dispatch)
  return chainApplyColorSpaceXform(in0);
}


#endif  // TGSL_POINTWISE_CHAIN_EVAL_INCLUDED
