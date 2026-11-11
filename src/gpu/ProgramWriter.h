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
 * Writes one program's per-draw uniform values and texture bindings. A Program owns its writer;
 * precompiled programs address their uniforms through the artifact's structural layout.
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

  /**
   * Writes every per-draw uniform value of the draw described by info into the two uniform data
   * objects, without any GPU-side work: no uniform buffer is mapped and no sampler is bound. The
   * upload path (write) feeds it the program's uniform data; the specialization capture feeds it a
   * CPU scratch buffer, so the values a specialized shader folds and the values the upload writes
   * come from one and the same code path.
   * @param info               The draw's processor tree and pipeline state.
   * @param vertexUniformData  The vertex stage's uniform data, over any writable buffer; may be
   *                           null only when the stage declares no uniforms.
   * @param fragmentUniformData  The fragment stage's uniform data, over any writable buffer.
   */
  void writeUniformValues(const ProgramInfo& info, UniformData* vertexUniformData,
                          UniformData* fragmentUniformData) const;

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

/** The precompiled route: the IR-driven walk and the artifact's sampler slots. */
class PrecompiledProgramWriter : public ProgramWriter {
 public:
  PrecompiledProgramWriter() = default;

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

}  // namespace tgfx
