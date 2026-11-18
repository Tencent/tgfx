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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include "gpu/shaders/Family.h"
#include "gpu/shaders/Layout.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Sampler.h"

namespace tgfx {

class Program;
class ProgramInfo;
class RenderPass;
class RenderTarget;
class UniformData;

/// The render-target adjust vector (2/width, -1, ±2/height, ±1): the Frame module's single field.
/// The declared routes address it through their derived layouts, so both the chain and the
/// declaration-driven writers take the value from here rather than each carrying a copy.
std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget);

/// The sampler address mode a tile mode maps onto. Shared by the sampler passes.
inline AddressMode ToAddressMode(TileMode tileMode) {
  switch (tileMode) {
    case TileMode::Clamp:
      return AddressMode::ClampToEdge;
    case TileMode::Repeat:
      return AddressMode::Repeat;
    case TileMode::Mirror:
      return AddressMode::MirrorRepeat;
    case TileMode::Decal:
      return AddressMode::ClampToBorder;
  }
  return AddressMode::ClampToEdge;
}

/**
 * Writes one program's per-draw uniform values and texture bindings. The variant is fixed at
 * construction, so the layout is derived once per program (not once per draw) and every field
 * write addresses an element index the layout resolves — no name is composed at draw time.
 *
 * Both families reach the same walk over the variant's modules: a declared family's module writers
 * read the selection's leaves, the interpreter's read the compiled program, and neither path
 * duplicates a value the other computes.
 */
class ProgramWriter {
 public:
  explicit ProgramWriter(Variant variant);
  virtual ~ProgramWriter() = default;

  /**
   * Uploads every uniform and binds the samplers of the program for the draw described by info.
   * @param info        The draw's selection, geometry facts and pipeline state.
   * @param renderPass  The pass to bind the uniform buffer and the textures on.
   * @param program     The program whose uniform layout and declared sampler count drive the
   *                    writes.
   * @return            False when the program's content is unavailable (the interpreter route
   *                    without a compiled program) or the uniform block has no backing store; the
   *                    caller must skip the draw rather than submit it with unwritten uniforms.
   */
  bool write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const;

 protected:
  /// The declared-member writers fill the blocks their emission declares: the plan's block and the
  /// variant's prefix land in the one fragment buffer the upload carries.
  virtual void writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                             UniformData* fragmentUniformData) const;
  /// Binds the program's samplers. The declared routes walk the variant's texture list; a program
  /// whose fragment declares its own sampler set binds the emit's table instead.
  virtual bool writeSamplers(const ProgramInfo& info, Program* program,
                             RenderPass* renderPass) const;
  /// Whether the draw carries everything the variant's writers read: the interpreter's modules
  /// need the compiled program the selection attached.
  bool carriesUniformContent(const ProgramInfo& info) const;

  Variant variant;
  Layout layout;
};

}  // namespace tgfx
