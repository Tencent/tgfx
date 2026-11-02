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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/CoordTransform.h"
#include "gpu/UniformData.h"
#include "gpu/aot/Program.h"

namespace tgfx {

class UniformData;
class ParamWriter;
struct AOTTextureParameters;

// The instruction contract's bind column (NodeContract.h's AOTOpContract): one function per op
// that writes a per-slot parameter block. The caller writes the packed word and its selector with
// its own dialect, then dispatches here with the slot's index and its ordinal into the per-kind
// parameter array (the coverage capacities; the slot index for every other op).
void BindColorMatrixSlot(UniformData* uniformData, size_t index, int ordinal,
                         const AOTChainSlot& slot);
void BindLumaSlot(UniformData* uniformData, size_t index, int ordinal, const AOTChainSlot& slot);
void BindAlphaThresholdSlot(UniformData* uniformData, size_t index, int ordinal,
                            const AOTChainSlot& slot);
void BindConstColorSlot(UniformData* uniformData, size_t index, int ordinal,
                        const AOTChainSlot& slot);
void BindAARectCoverageSlot(UniformData* uniformData, size_t index, int ordinal,
                            const AOTChainSlot& slot);
void BindLocalRectCoverageSlot(UniformData* uniformData, size_t index, int ordinal,
                               const AOTChainSlot& slot);
void BindRRectCoverageSlot(UniformData* uniformData, size_t index, int ordinal,
                           const AOTChainSlot& slot);

/**
 * Writes one compiled chain program's uniforms: the vertex-side coord-source mask and every
 * fragment field the kernel reads — the slot records, the coverage arrays, the tiled recipes, the
 * gradient and noise parameter blocks. This is the shell-free upload path: it reads the program
 * and its binding table only, never a FragmentProcessor.
 */
void BindChainProgram(const AOTProgram& program, UniformData* vertexUniformData,
                      UniformData* fragmentUniformData);

/**
 * Writes the YUV family's compiled tail program (CompileYUVProgramForInfo): SlotCount plus the
 * chain instruction arrays — the same record layout the interpreter reads, with a linear-tail
 * selector dialect (bits 0-7 the op's small value — the const-color input mode or the blend
 * mode — bit 8 the blend's operand order; no register bits, array order is dataflow order) —
 * and the shared color-space block. Like the chain upload, this reads the program only, never
 * a FragmentProcessor.
 */
void BindYUVProgram(const AOTProgram& program, UniformData* fragmentUniformData);

/**
 * Writes the program's per-binding child uniforms: every sampler-binding child's
 * Subset/AlphaOnly/RGBAAA records (the TextureEffect-class children share one structural-ordinal
 * sequence, exactly as the processor-tree walk numbered them) and the device mask's
 * DeviceCoordMatrix plus DeviceMaskSubset decoy. Field-for-field the same writes the child
 * TextureEffects' module bind and the DeviceSpaceTextureEffect writer produced; this replaces
 * that walk, so the upload no longer depends on the child processors existing.
 */
void BindChainBindings(const AOTProgram& program, UniformData* fragmentUniformData);

/**
 * The IR-driven texture write: the leaf's recorded facts (subset, alphaStart, sampling, flags)
 * plus the view its proxy resolves at bind time, value-gated exactly like
 * TextureSourceModule::Bind over the live processor — field for field, because the leaf was
 * lowered from that very processor. This is the fill route's twin of the chain's binding table:
 * a draw's texture uploads read the IR, not the processor tree.
 */
void BindTextureSourceLeaf(const AOTTextureParameters& leaf, ParamWriter* writer);

/**
 * The coordinate transforms the chain kernel's vertex stage consumes, in the order the geometry
 * processor writes them: for an extended profile, the chain-wide gradient/noise matrix first (the
 * source module reads it through the first transform slot), then one per sampler-binding child
 * that samples through a transform — every texture leaf, the LUT gradient and the padding. The
 * device mask samples in destination space and contributes none.
 */
std::vector<CoordTransform> BuildChainCoordTransforms(const AOTProgram& program);

}  // namespace tgfx
