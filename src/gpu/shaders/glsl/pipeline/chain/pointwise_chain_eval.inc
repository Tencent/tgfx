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

// The pure op fragments the interpreter's branches call. The texture and modulation fragments
// depend on the leaf readback helpers (source/texture_read_swizzle.inc, included by the entry
// ahead of this file); the coverage fragments depend on the analytic cores above.
#include "ops/Texture.glsl"
#include "ops/TexModulate.glsl"
#include "ops/AARectCoverage.glsl"
#include "ops/LocalRectCoverage.glsl"
#include "ops/RRectCoverage.glsl"

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

// OP_TEXTURE's modulation stage: only a chain whose slots carry a texture leaf reaches it, so a
// chain without one closes the gate and the runtime splices the branch out of the text.
#pragma tgfx_spec_gate TextureSlot : (SPEC_UsedOps() & (1 << OP_TEXTURE)) != 0
vec4 chainEvalTextureSlot(int slot, int selector) {
  return tgfxOpTexture(chainLeafTex[slot], TGFX_CHAIN_GEOM_COLOR, TGFX_CHAIN_UNIT_COVERAGE,
                       selector & 0x1F);
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
  return tgfxOpTexModulate(chainLeafTex[srcSlot], in0, selector & 1);
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

// Analytic AA-rect coverage (identical math to GLSLRectEffect): the interpreter's dispatch and
// the composed executor both go through the pure fragment, so the formula exists once.
vec4 chainAARectCoverage(vec4 in0, vec4 rect) {
  return tgfxOpAARectCoverage(in0, rect, gl_FragCoord.xy);
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
    return tgfxOpLocalRectCoverage(in0, d.rect, d.deviceToLocal, gl_FragCoord.xy);
  }
  if (Packed.x == OP_RRECT_COVERAGE) {
    RRectCoverageData d = chainLoadRRectCoverage(dataAt);
    return tgfxOpRRectCoverage(in0, d.rect, d.radiiX, d.radiiY, d.antiAlias, d.deviceToLocal,
                               gl_FragCoord.xy);
  }
  vec4 resourceValue = vec4(0.0);
  if (evalResourceSlot(Packed, in0, dataAt, resourceValue)) {
    return resourceValue;
  }
  // OP_COLOR_SPACE_XFORM (the registered exception: its parameters are the program-level block,
  // not per-slot registers, so it does not ride the shared dispatch)
  return chainApplyColorSpaceXform(in0, ChainCSFlags, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType,
                                   ChainSrcOOTF, ChainColorXform, ChainDstOOTF, ChainDstTF0,
                                   ChainDstTF1, ChainDstTFType);
}


#endif  // TGSL_POINTWISE_CHAIN_EVAL_INCLUDED
