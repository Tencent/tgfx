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

#include "ProgramBuilder.h"

namespace tgfx {

ProgramBuilder::ProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : context(context), programInfo(programInfo) {
}

SamplerHandle ProgramBuilder::emitSampler(std::shared_ptr<Texture> texture,
                                          const std::string& name) {
  ++numFragmentSamplers;
  return uniformHandler()->addSampler(texture, name);
}

void ProgramBuilder::emitFSOutputSwizzle() {
  // Swizzle the fragment shader outputs if necessary.
  auto swizzle = programInfo->getOutputSwizzle();
  if (swizzle == Swizzle::RGBA()) {
    return;
  }
  // Inject a #define so the TGFX_OutputSwizzle helper (from tgfx_fs_boilerplate.glsl) picks up
  // the correct swizzle components. Callers that run through ModularProgramBuilder will
  // additionally include the FSBoilerplate module; the legacy path constructs its own call site.
  auto fragBuilder = fragmentShaderBuilder();
  fragBuilder->shaderStrings[ShaderBuilder::Type::Definitions] +=
      "#undef TGFX_OUT_SWIZZLE\n#define TGFX_OUT_SWIZZLE " + std::string(swizzle.c_str()) + "\n";
  const auto& output = fragBuilder->colorOutputName();
  fragBuilder->codeAppendf("%s = TGFX_OutputSwizzle(%s);", output.c_str(), output.c_str());
}

std::string ProgramBuilder::nameVariable(const std::string& name) const {
  auto processor = currentProcessors.empty() ? nullptr : currentProcessors.back();
  if (processor == nullptr) {
    return name;
  }
  return name + programInfo->getMangledSuffix(processor);
}

void ProgramBuilder::nameExpression(std::string* output, const std::string& baseName) {
  // Create var to hold the stage result. If we already have a valid output name, just use that
  // otherwise create a new mangled one. This name is only valid if we are reordering stages
  // and have to tell stage exactly where to put its output.
  std::string outName;
  if (!output->empty()) {
    outName = *output;
  } else {
    outName = nameVariable(baseName);
  }
  fragmentShaderBuilder()->codeAppendf("vec4 %s;", outName.c_str());
  *output = outName;
}

void ProgramBuilder::finalizeShaders() {
  varyingHandler()->finalize();
  vertexShaderBuilder()->finalize();
  fragmentShaderBuilder()->finalize();
}
}  // namespace tgfx
