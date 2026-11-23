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

#include "GLSLFragmentShaderBuilder.h"
#include "gpu/glsl/GLSLProgramBuilder.h"

namespace tgfx {
static constexpr char DstColorName[] = "_dstColor";
static constexpr char SubpassInputName[] = "tgfx_SubpassInput";

GLSLFragmentShaderBuilder::GLSLFragmentShaderBuilder(ProgramBuilder* program)
    : FragmentShaderBuilder(program) {
  auto shaderCaps = program->getContext()->shaderCaps();
  if (shaderCaps->usesPrecisionModifiers) {
    setPrecisionQualifier("precision mediump float;");
  }
}

std::string GLSLFragmentShaderBuilder::dstColor() {
  auto shaderCaps = programBuilder->getContext()->shaderCaps();
  if (shaderCaps->frameBufferFetchSupport) {
    if (shaderCaps->frameBufferFetchUsesSubpassInput) {
      declareSubpassInput();
      return std::string("subpassLoad(") + SubpassInputName + ")";
    }
    if (!shaderCaps->frameBufferFetchExtensionString.empty()) {
      addFeature(PrivateFeature::FramebufferFetch, shaderCaps->frameBufferFetchExtensionString);
    }
    if (shaderCaps->frameBufferFetchNeedsCustomOutput) {
      return CUSTOM_COLOR_OUTPUT_NAME;
    }
    return shaderCaps->frameBufferFetchColorName;
  }
  return DstColorName;
}

void GLSLFragmentShaderBuilder::appendCall(const std::string& outputVar,
                                           const std::string& functionName,
                                           const std::vector<std::string>& args,
                                           const std::string& wrapType) {
  if (callRecorder != nullptr) {
    CallRecord record = {};
    record.output = outputVar;
    record.function = functionName;
    record.args.assign(args.begin(), args.end());
    record.order = nextEmitOrder();
    callRecorder->push_back(std::move(record));
  }
  codeAppendf("%s = ", outputVar.c_str());
  if (!wrapType.empty()) {
    codeAppendf("%s(", wrapType.c_str());
  }
  codeAppendf("%s(", functionName.c_str());
  bool first = true;
  for (const auto& arg : args) {
    if (!first) {
      codeAppend(", ");
    }
    first = false;
    codeAppend(arg);
  }
  codeAppend(")");
  if (!wrapType.empty()) {
    codeAppend(")");
  }
  codeAppend(";");
}

std::string GLSLFragmentShaderBuilder::appendTempCall(const std::string& functionName,
                                                      const std::vector<std::string>& args) {
  auto entry = appendFunction(functionName.c_str());
  if (entry.empty()) {
    return "";
  }
  const std::string temp = "tgfxTemp" + std::to_string(tempCallIndex++);
  codeAppendf("vec4 %s = %s(", temp.c_str(), entry.c_str());
  bool first = true;
  for (const auto& arg : args) {
    if (!first) {
      codeAppend(", ");
    }
    first = false;
    codeAppend(arg);
  }
  codeAppend(");");
  return temp;
}

void GLSLFragmentShaderBuilder::declareSubpassInput() {
  if (subpassInputDeclared) {
    return;
  }
  subpassInputDeclared = true;
  shaderStrings[Type::Uniforms] +=
      "layout(input_attachment_index=0, set=2, binding=0) uniform subpassInput ";
  shaderStrings[Type::Uniforms] += SubpassInputName;
  shaderStrings[Type::Uniforms] += ";\n";
}

std::string GLSLFragmentShaderBuilder::colorOutputName() {
  return CUSTOM_COLOR_OUTPUT_NAME;
}
}  // namespace tgfx
