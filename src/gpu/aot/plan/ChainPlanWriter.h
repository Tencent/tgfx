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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/ProgramWriter.h"
#include "gpu/shaders/contract/ChainPlan.h"

namespace tgfx {

/// The plan program's writer: the variant prefix keeps the base writer's uploads (the entry-level
/// members and the vertex block), and the plan's named parameter block — one member per
/// (operator, slot, parameter) — appends past the prefix bytes, filled from the typed slot fields
/// directly. The positional pool never enters this path.
class ChainPlanWriter : public ProgramWriter {
 public:
  /// @param prefixBytes  The variant declaration prefix's fragment block size; the plan's
  ///                     parameter members start at this offset inside the one fragment buffer.
  ChainPlanWriter(Variant variant, ChainPlan plan, uint32_t prefixBytes);

 private:
  void writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                     UniformData* fragmentUniformData) const override;

  ChainPlan plan;
  uint32_t prefixBytes = 0;
};

}  // namespace tgfx
