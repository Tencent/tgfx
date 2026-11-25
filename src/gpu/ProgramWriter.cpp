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

#include "ProgramWriter.h"
#include "Program.h"
#include "ProgramInfo.h"
#include "core/utils/Log.h"
#include "gpu/GlobalCache.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/modules/Chain.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget) {
  std::array<float, 4> result = {};
  result[0] = 2.f / static_cast<float>(renderTarget->width());
  result[2] = 2.f / static_cast<float>(renderTarget->height());
  result[1] = -1.f;
  result[3] = -1.f;
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  // WebGPU viewport Y transform is (1 - y_ndc)/2, meaning NDC y=+1 maps to framebuffer top.
  // For TopLeft origin (pixel y=0 at top), we need pixel y=0 -> NDC y=+1, requiring Y negate.
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

ProgramWriter::ProgramWriter(Variant variant)
    : variant(std::move(variant)), layout(DeriveLayout(this->variant)) {
}

bool ProgramWriter::carriesUniformContent(const ProgramInfo& info) const {
  const Family& family = Families()[variant.family];
  const auto modules = ModulesOf(family, variant);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    if (modules.list[slot] != &Chain) {
      continue;
    }
    // The interpreter's writers read the compiled program the selection attached; without it the
    // draw would submit with unwritten fields.
    const auto* sources = info.getSources();
    return sources != nullptr && sources->chain != nullptr;
  }
  return true;
}

bool ProgramWriter::write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const {
  if (!carriesUniformContent(info)) {
    LOGE("ProgramWriter::write() the program's uniform content is unavailable; skipping the draw");
    return false;
  }
  size_t vertexOffset = 0;
  size_t fragmentOffset = 0;
  auto uniformBuffer = info.getUniformBuffer(program, &vertexOffset, &fragmentOffset);

  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);
  if (uniformBuffer == nullptr &&
      (vertexUniformData != nullptr || fragmentUniformData != nullptr)) {
    // The program needs a uniform block but no backing store is available (allocation or map
    // failed). Aborting the draw loses one frame's content; uploading anyway would write through
    // a null pointer. Tell the caller to skip the draw.
    LOGE("ProgramWriter::write() no uniform buffer available; skipping the draw");
    return false;
  }

  writeUniforms(info, vertexUniformData, fragmentUniformData);

  info.bindUniformBufferAndUnloadToGPU(program, std::move(uniformBuffer), renderPass, vertexOffset,
                                       fragmentOffset);
  return writeSamplers(info, program, renderPass);
}

void ProgramWriter::writeUniforms(const ProgramInfo& info, UniformData* /*vertexUniformData*/,
                                  UniformData* /*fragmentUniformData*/) const {
  // The selection layer that recorded the draw's data sources is retired: a program built for
  // this route carries no sources, so the uniforms stay unwritten (the FP-tree route owns the
  // uniforms now).
  if (info.getSources() == nullptr) {
    LOGE("ProgramWriter: the draw carries no selection for the %s family; uniforms stay unwritten",
         Families()[variant.family].name);
  }
}

bool ProgramWriter::writeSamplers(const ProgramInfo& info, Program* /*program*/,
                                  RenderPass* /*renderPass*/) const {
  if (info.getSources() == nullptr) {
    LOGE("ProgramWriter: the draw carries no selection; nothing binds");
    return false;
  }
  return true;
}

}  // namespace tgfx
