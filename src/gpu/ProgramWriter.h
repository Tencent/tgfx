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

#include "gpu/UniformData.h"

namespace tgfx {

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

  /**
   * Whether this writer can produce the program's full uniform content for the draw described by
   * info. The base accepts every program; the compiled-program routes (the chain kernel, the YUV
   * tail) refuse a draw whose compiled program is unavailable, so write() fails the draw before
   * any uniform is written instead of submitting it with unwritten fields.
   */
  virtual bool canWrite(const ProgramInfo& info) const;

 protected:
  /** Writes the engine-level default uniforms only this route's shaders declare. */
  virtual void writeEngineDefaults(const ProgramInfo& info, UniformData* fragmentUniformData) const;

  /**
   * Writes the geometry processor's uniforms. The default feeds the processor the draw's
   * coordinate transforms in tree order; the chain writer feeds it the compiled program's
   * leaf-ordered transforms instead.
   */
  virtual void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                                      UniformData* fragmentUniformData) const;

  /** Writes the fragment and xfer processors' per-instance uniforms. */
  virtual void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                               UniformData* fragmentUniformData) const = 0;

  /** Binds the draw's textures onto the program's sampler slots. */
  virtual void writeSamplers(const ProgramInfo& info, Program* program,
                             RenderPass* renderPass) const = 0;
};

/** The runtime-generated route: JIT name suffixes and the samplers the draw supplies. */
class RuntimeProgramWriter final : public ProgramWriter {
 protected:
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData) const override;
  void writeSamplers(const ProgramInfo& info, Program* program,
                     RenderPass* renderPass) const override;
};

/** The precompiled route: the IR-driven walk and the artifact's sampler slots. */
class PrecompiledProgramWriter : public ProgramWriter {
 public:
  /**
   * The YUV family appends its compiled tail records after the walk (the tail program the
   * family's matching compiled, or that compiles here once on a cache hit); every other family
   * uploads the walk's content alone.
   */
  explicit PrecompiledProgramWriter(bool bindYUVTail = false) : bindYUVTail(bindYUVTail) {
  }

  bool canWrite(const ProgramInfo& info) const override;

 protected:
  void writeEngineDefaults(const ProgramInfo& info,
                           UniformData* fragmentUniformData) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData) const override;
  void writeSamplers(const ProgramInfo& info, Program* program,
                     RenderPass* renderPass) const override;
  /// The processor-tree fallback for IRs the action list cannot bind (see writeProcessors).
  void writeProcessorsByTree(const ProgramInfo& info, UniformData* vertexUniformData,
                             UniformData* fragmentUniformData) const;

 private:
  bool bindYUVTail = false;
};

/**
 * The fused chain kernel's writer: the draw's whole uniform content, its vertex transforms and its
 * sampler bindings all come from the compiled chain program the info carries (uniforms through the
 * aot binder, transforms from the binding table, samplers in binding order) — no processor tree is
 * walked. It stays a ProgramWriter until P7 folds the whole writer hierarchy away.
 */
class ChainProgramWriter final : public PrecompiledProgramWriter {
 public:
  bool canWrite(const ProgramInfo& info) const override;

 protected:
  void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                              UniformData* fragmentUniformData) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData) const override;
  void writeSamplers(const ProgramInfo& info, Program* program,
                     RenderPass* renderPass) const override;
};

/**
 * The YUV family's writer: the source leaf's family uniforms and samplers ride the standard
 * precompiled walk (the tree's TextureEffect writes them — on a planned draw the operators above
 * it declare no fields the kernel reads, so their writes are no-ops), and the color-grade tail
 * rides the compiled tail program's chain-encoded instruction records. No rebuilt processor:
 * the tail's upload no longer depends on one existing.
 */
class YUVProgramWriter final : public PrecompiledProgramWriter {
 public:
  YUVProgramWriter() : PrecompiledProgramWriter(true) {
  }
};

}  // namespace tgfx
