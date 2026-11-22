/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "VulkanProgramBuilder.h"
#include "gpu/ShaderCaps.h"
#include "core/utils/Log.h"

namespace tgfx {
VulkanProgramBuilder::VulkanProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
  // The bytecode route never spells shader text: the calls land in the record list instead.
  _fragBuilder.setCallRecorder(&fragmentCalls);
}

std::string VulkanProgramBuilder::getShaderVarDeclarations(const ShaderVar&, ShaderStage) const {
  return "";
}

std::string VulkanProgramBuilder::getUniformBlockDeclaration(
    ShaderStage, const std::vector<Uniform>&) const {
  return "";
}

bool VulkanProgramBuilder::checkSamplerCounts() {
  return true;
}

std::shared_ptr<Program> VulkanProgramBuilder::finalize() {
  // The body emitter and the module link land here next.
  LOGE("VulkanProgramBuilder: the SPIR-V finalize is not wired yet");
  return nullptr;
}
}  // namespace tgfx
