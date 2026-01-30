/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "FragmentShaderBuilder.h"
#include "ProgramBuilder.h"
#include "SLType.h"
#include "core/utils/Log.h"

namespace tgfx {
FragmentShaderBuilder::FragmentShaderBuilder(ProgramBuilder* program) : ShaderBuilder(program) {
}

void FragmentShaderBuilder::declareCustomOutputColor() {
  auto typeModifier = ShaderVar::TypeModifier::Out;
  if (features & PrivateFeature::FramebufferFetch) {
    typeModifier = ShaderVar::TypeModifier::InOut;
  }

  outputs.emplace_back(CUSTOM_COLOR_OUTPUT_NAME, SLType::Float4, typeModifier);
}

void FragmentShaderBuilder::onBeforeChildProcEmitCode(const FragmentProcessor* child) const {
  programBuilder->currentProcessors.push_back(child);
}

void FragmentShaderBuilder::onAfterChildProcEmitCode() const {
  programBuilder->currentProcessors.pop_back();
}

std::string FragmentShaderBuilder::emitPerspTextCoord(const ShaderVar& coordVar) {
  if (coordVar.type() == SLType::Float2) {
    return coordVar.name();
  }
  if (coordVar.type() == SLType::Float3) {
    const std::string perspCoordName = "perspCoord2D";
    codeAppendf("highp vec2 %s = %s.xy / %s.z;", perspCoordName.c_str(), coordVar.name().c_str(),
                coordVar.name().c_str());
    return perspCoordName;
  }
  DEBUG_ASSERT(false);
  return "";
}
}  // namespace tgfx
