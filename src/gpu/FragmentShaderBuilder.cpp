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
}  // namespace tgfx
