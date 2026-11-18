/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tgfx {

/**
 * One instruction of a composed chain body, the way the runtime's decoded program expresses it
 * and the emitter consumes it: the operator's ABI value (ChainISA's constants), the two input
 * designators (-1 geometry color, -3 coverage unit, -4 white, -5 opaque geometry color, any other
 * negative zero, >=0 a result register), the result register, the precomputed selector word (the
 * ChainSlotSelector encoding; Texture and TexModulate read it), and the Blend and ConstColor small
 * values the call sites marshal. Values and texture identities are absent by construction — a
 * composition names code shape only.
 */
struct ChainCompositionSlot {
  int op = 0;
  int in0 = -2;
  int in1 = -2;
  int outRegister = -1;
  int selector = 0;
  int blendMode = 0;
  bool multiplyInputAlpha = false;
  int inputMode = 0;
};

/**
 * The structure a composed chain body evaluates: the slots in instruction order, the three
 * epilogue registers the fragment's tail consumes, and whether the xfer's advanced blend pass runs
 * after the slots. This is the layer-neutral contract between whoever knows a draw's shape (the
 * runtime's AOTProgram, or the build tool pre-composing a known shape) and the GLSL emitter, so
 * both spell one body from one description.
 */
struct ChainComposition {
  std::vector<ChainCompositionSlot> slots = {};
  int rootRegister = 0;
  int coverageRootRegister = -1;
  int clipCoverageRegister = -1;
  bool xpNeedsBlendPass = false;
  /// The feature masks the text gates read, carried as fields so the emitter's consumers and the
  /// runtime's spec resolution derive them from one place: the adapter copies them off the decoded
  /// program (ChainFeatureMasksOf), and a build-side manifest states them for a pre-composed shape.
  uint32_t usedOps = 0;
  uint32_t blendModes = 0;
  uint32_t textureFlags = 0;
  uint32_t gradientKinds = 0;
};

/**
 * Emits the GLSL statements that replace the chain executor loop (the tgfx_composed_executor
 * pragmas in pointwise_fragment_main.inc): one straight-line evaluation of the composition's
 * slots where every instruction becomes a local vec4 whose inputs are earlier locals, the fixed
 * designator source functions or literals. The opcode dispatch, the selector reads and the
 * result-register array disappear from the compiled text, while the values still upload through
 * the same data pool — the emitted loads read it with literal offsets and the per-operator calls
 * are the same functions the interpreter dispatches to.
 * @param composition  The shape to spell.
 * @return             The replacement statements, or an empty string when a slot uses an operator
 *                     this emitter does not cover or reads a register no earlier slot wrote; the
 *                     caller then keeps the interpreter's loop.
 */
/// @param pureOps  Emit the ops/ pure fragments' call forms (the assembled path, whose entry
///                 supplies the leaf samples and the fragment coordinate) instead of the
///                 interpreter-coupled shared functions the splice path lives with.
std::string EmitComposedChainBodyGLSL(const ChainComposition& composition, bool pureOps = false);

/**
 * Splices an emitted body into a chain fragment text, replacing everything between the executor
 * pragmas (both included).
 * @param composition  The shape to spell.
 * @param code         The fragment source to rewrite in place.
 * @return             False when the pragmas are absent (a text the current build did not
 *                     produce) or the emitter declined the composition; the text is untouched.
 */
bool ComposeChainFragmentGLSL(const ChainComposition& composition, std::string* code);

/**
 * Serializes the composition deterministically: a version tag, then every slot's fields, the three
 * epilogue registers, the blend-pass flag and the four feature masks. The serialization is the
 * structure key's only input besides the variant id, so a field added to the description bumps the
 * version tag and every stored product keyed on it.
 */
std::string SerializeChainComposition(const ChainComposition& composition);

/**
 * The lookup key of a composed fragment payload: a hash over the variant id and the composition's
 * serialization. The runtime computes it from the live draw's decoded program; the build computed
 * the same key when it stored the payload, so a hit means the stored text spells exactly this
 * shape.
 */
uint64_t ChainCompositionKey(uint32_t variantId, const ChainComposition& composition);

/**
 * The composed payload's id inside the bundle's fragment pool: the high bit set (real variant ids
 * are family << 20 | ordinal and stay far below it) plus the low 31 bits of the structure key.
 * Composed entries reuse the pool's storage, bounds and identity hash unchanged.
 */
uint32_t ComposedFragmentId(uint32_t variantId, const ChainComposition& composition);

/**
 * Prepends one `#define TGFX_SPEC_<id> <value>` line per pair after the #version line, the same
 * insertion the runtime specialization performs, so a pre-composed payload and a runtime-composed
 * text differ only in where the same bytes were produced.
 * @param code     The fragment source to rewrite in place.
 * @param idValues The constant ids and their values, ascending by id.
 * @return         False when the text carries no #version line to insert after.
 */
bool PrependChainSpecDefinesGLSL(std::string* code,
                                 const std::vector<std::pair<uint32_t, int32_t>>& idValues);

/**
 * Derives the specialization values a composition implies, field-name keyed the way the chain
 * module declares them: SlotCount, the SlotPacked array padded to the variant's slot capacity with
 * the inactive record, the three register indices, the four feature masks and NeedsBlendMath (a
 * Blend slot or the xfer's advanced blend pass). Values and texture identities are absent by
 * construction; the runtime's own resolution over the same shape must agree bit for bit.
 * @param composition   The shape to resolve.
 * @param slotCapacity  The variant's SlotPacked capacity (16 or 32; the inactive tail pads to it).
 * @return              The field name to its value sequence.
 */
std::map<std::string, std::vector<int32_t>> ResolveCompositionSpecValues(
    const ChainComposition& composition, size_t slotCapacity);

}  // namespace tgfx
