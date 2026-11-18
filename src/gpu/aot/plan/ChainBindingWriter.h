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

#include "gpu/ProgramWriter.h"
#include "gpu/aot/chain/ChainProgramEmit.h"

namespace tgfx {

/**
 * The assembled chain program's writer: it walks the emit's declaration tables and fills each
 * member from its reader, so the upload and the emitted text address the same bytes by
 * construction. The stored vertex stage's members ride the variant's derived block (the stored text
 * is their declaration); the emitted fragment block is the emit's own, with no prefix and no
 * parameter pool.
 */
class ChainBindingWriter : public ProgramWriter {
 public:
  ChainBindingWriter(Variant variant, ChainBindings bindings);

 private:
  void writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                     UniformData* fragmentUniformData) const override;
  bool writeSamplers(const ProgramInfo& info, Program* program,
                     RenderPass* renderPass) const override;

  ChainBindings bindings = {};
};

}  // namespace tgfx
