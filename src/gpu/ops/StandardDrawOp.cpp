/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "StandardDrawOp.h"
#include "core/utils/Log.h"
#include "gpu/Program.h"

namespace tgfx {
void StandardDrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (!bindStandardPipeline(renderPass, renderTarget)) {
    return;
  }
  onDraw(renderPass, renderTarget);
}

bool StandardDrawOp::bindStandardPipeline(RenderPass* renderPass, RenderTarget* renderTarget) {
  auto geometryProcessor = onMakeGeometryProcessor(renderTarget);
  if (geometryProcessor == nullptr) {
    return false;
  }
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  fragmentProcessors.reserve(colors.size() + coverages.size());
  for (auto& color : colors) {
    fragmentProcessors.emplace_back(color.get());
  }
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(coverage.get());
  }
  ProgramInfo programInfo(renderTarget, geometryProcessor.get(), std::move(fragmentProcessors),
                          colors.size(), xferProcessor.get(), blendMode);
  programInfo.setCullMode(cullMode);
  onConfigureProgramInfo(programInfo);
  auto program = programInfo.getProgram();
  if (program == nullptr) {
    LOGE("StandardDrawOp::bindStandardPipeline() Failed to get the program!");
    return false;
  }
  renderPass->setPipeline(program->getPipeline());
  programInfo.setUniformsAndSamplers(renderPass, program.get());
  applyScissor(renderPass, renderTarget);
  return true;
}
}  // namespace tgfx
