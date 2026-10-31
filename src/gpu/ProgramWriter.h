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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include "core/utils/PlacementPtr.h"
#include "gpu/UniformData.h"

namespace tgfx {

class FragmentProcessor;
class Program;
class ProgramInfo;
class RenderPass;

/**
 * Writes one program's per-draw uniform values and texture bindings. A Program owns its writer:
 * runtime-generated programs address their uniforms through the JIT per-processor name suffixes,
 * precompiled programs through the artifact's structural layout. Keeping the two addressing
 * routes in separate writers keeps the precompiled-specific rules out of ProgramInfo.
 */
class ProgramWriter {
 public:
  virtual ~ProgramWriter() = default;

  /**
   * Uploads every uniform and binds the samplers of the program for the draw described by info.
   * @param info        The draw's processor tree and pipeline state.
   * @param renderPass  The pass to bind the uniform buffer and the textures on.
   * @param program     The program whose uniform layout and declared sampler count drive the
   *                    writes.
   * @return            False when the program needs a uniform block but no backing store is
   *                    available (allocation or map failure); the caller must skip the draw
   *                    rather than submit it with unwritten uniforms.
   */
  virtual bool write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const;

 protected:
  /**
   * A per-draw expression: either an owned re-expression built over the draw's own processors, or
   * a borrowed processor the info already carries. Exactly one is set; get() returns it or null.
   */
  struct Expression {
    PlacementPtr<FragmentProcessor> owned = {};
    const FragmentProcessor* borrowed = nullptr;

    const FragmentProcessor* get() const {
      return borrowed != nullptr ? borrowed : owned.get();
    }
  };

  /**
   * Builds the route's per-draw expression over the draw's own processors. The default returns an
   * empty expression (the generic routes read the info's processors directly); the chain writer
   * re-expresses the whole effect as the fused chain processor — or borrows the one the info
   * already carries — whose leaf-ordered children drive the geometry transforms, the uniform
   * writes and the sampler bindings.
   */
  virtual Expression buildExpression(const ProgramInfo& info) const;

  /** Resets per-draw addressing state on the reused UniformData before the base-name writes. */
  virtual void beginUniformWrites(UniformData* vertexUniformData,
                                  UniformData* fragmentUniformData) const;

  /** Writes the engine-level default uniforms only this route's shaders declare. */
  virtual void writeEngineDefaults(const ProgramInfo& info, UniformData* fragmentUniformData) const;

  /**
   * Writes the geometry processor's uniforms. The default feeds the processor the draw's
   * coordinate transforms in tree order; the chain writer feeds it the expression's leaf-ordered
   * transforms instead.
   */
  virtual void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                                      UniformData* fragmentUniformData,
                                      const FragmentProcessor* expression) const;

  /** Writes the fragment and xfer processors' per-instance uniforms. */
  virtual void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                               UniformData* fragmentUniformData,
                               const FragmentProcessor* expression) const = 0;

  /** Binds the draw's textures onto the program's sampler slots. */
  virtual void writeSamplers(const ProgramInfo& info, Program* program, RenderPass* renderPass,
                             const FragmentProcessor* expression) const = 0;
};

/** The runtime-generated route: JIT name suffixes and the samplers the draw supplies. */
class RuntimeProgramWriter final : public ProgramWriter {
 protected:
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData,
                       const FragmentProcessor* expression) const override;
  void writeSamplers(const ProgramInfo& info, Program* program, RenderPass* renderPass,
                     const FragmentProcessor* expression) const override;
};

/** The precompiled route: structural ordinals, module binds and the artifact's sampler slots. */
class PrecompiledProgramWriter : public ProgramWriter {
 protected:
  void beginUniformWrites(UniformData* vertexUniformData,
                          UniformData* fragmentUniformData) const override;
  void writeEngineDefaults(const ProgramInfo& info,
                           UniformData* fragmentUniformData) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData,
                       const FragmentProcessor* expression) const override;
  void writeSamplers(const ProgramInfo& info, Program* program, RenderPass* renderPass,
                     const FragmentProcessor* expression) const override;
};

/**
 * The shared base of the writers whose routes re-express the draw's effect before uploading: the
 * per-draw values come from the rebuilt expression (walked in its own tree order, then the xfer
 * processor), not from the info's original processor list.
 */
class ExpressionProgramWriter : public PrecompiledProgramWriter {
 protected:
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData,
                       const FragmentProcessor* expression) const override;
};

/**
 * The fused chain kernel's writer: it re-expresses the draw's effect as the chain processor every
 * draw (the structure is stable for a given program key, the values are not), uploads through the
 * processor's own data writer, and binds the expression's leaf-ordered children as the artifact's
 * sampler slots.
 */
class ChainProgramWriter final : public ExpressionProgramWriter {
 protected:
  Expression buildExpression(const ProgramInfo& info) const override;
  void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                              UniformData* fragmentUniformData,
                              const FragmentProcessor* expression) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData,
                       const FragmentProcessor* expression) const override;
  void writeSamplers(const ProgramInfo& info, Program* program, RenderPass* renderPass,
                     const FragmentProcessor* expression) const override;
};

/**
 * The perlin kernel's writer: it re-expresses the draw's color expression as the perlin processor
 * with its trailing operators folded into the slot records every draw, and uploads through the
 * processor's own data writer. The samplers stay the generic precompiled binding (the lookup
 * textures ride the info's own perlin processor).
 */
class PerlinProgramWriter final : public ExpressionProgramWriter {
 protected:
  Expression buildExpression(const ProgramInfo& info) const override;
};

}  // namespace tgfx
