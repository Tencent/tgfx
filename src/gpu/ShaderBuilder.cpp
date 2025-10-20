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

#include "ShaderBuilder.h"
#include "ProgramBuilder.h"
#include "Swizzle.h"
#include "stdarg.h"

namespace tgfx {
// number of bytes (on the stack) to receive the printf result
static constexpr size_t ShaderBufferSize = 1024;

static bool NeedsAppendEnter(const std::string& code) {
  if (code.empty()) {
    return false;
  }
  auto& ch = code[code.size() - 1];
  return ch == ';' || ch == '{' || ch == '}';
}

ShaderBuilder::ShaderBuilder(ProgramBuilder* builder) : programBuilder(builder) {
  for (int i = 0; i <= Type::Code; ++i) {
    shaderStrings.emplace_back("");
  }
  shaderStrings[Type::Main] = "void main() {";
  indentation = 1;
  atLineStart = true;
}

const ProgramInfo* ShaderBuilder::getProgramInfo() const {
  return programBuilder->getProgramInfo();
}

void ShaderBuilder::setPrecisionQualifier(const std::string& precision) {
  shaderStrings[Type::PrecisionQualifier] = precision;
}

void ShaderBuilder::codeAppendf(const char* format, ...) {
  char buffer[ShaderBufferSize];
  va_list args;
  va_start(args, format);
  auto length = vsnprintf(buffer, ShaderBufferSize, format, args);
  va_end(args);
  codeAppend(std::string(buffer, static_cast<size_t>(length)));
}

void ShaderBuilder::codeAppend(const std::string& str) {
  if (str.empty()) {
    return;
  }
  appendIndentationIfNeeded(str);
  auto& code = shaderStrings[Type::Code];
  code += str;
  if (NeedsAppendEnter(code)) {
    appendEnterIfNotEmpty(Type::Code);
  }
  atLineStart = code[code.size() - 1] == '\n';
}

void ShaderBuilder::addFunction(const std::string& str) {
  appendEnterIfNotEmpty(Type::Functions);
  shaderStrings[Type::Functions] += str;
}

std::string ShaderBuilder::getMangledFunctionName(const char* baseName) {
  return programBuilder->nameVariable(baseName);
}

static std::string TextureSwizzleString(const Swizzle& swizzle) {
  if (swizzle == Swizzle::RGBA()) {
    return "";
  }
  std::string ret = ".";
  ret.append(swizzle.c_str());
  return ret;
}

void ShaderBuilder::appendTextureLookup(SamplerHandle samplerHandle, const std::string& coordName) {
  auto uniformHandler = programBuilder->uniformHandler();
  auto sampler = uniformHandler->getSamplerVariable(samplerHandle);
  codeAppendf("texture(%s, %s)", sampler.name().c_str(), coordName.c_str());
  codeAppend(TextureSwizzleString(uniformHandler->getSamplerSwizzle(samplerHandle)));
}

void ShaderBuilder::appendColorGamutXform(const char* srcColor, const ColorSpaceXformSteps* steps) {
  appendColorGamutXformUniformAndFunction(steps);
  appendColorGamutXformCode(srcColor, steps);
}

void ShaderBuilder::appendColorGamutXformUniformAndFunction(const ColorSpaceXformSteps* steps) {
  auto uniformHandler = programBuilder->uniformHandler();
  auto colorXformHelper = std::make_shared<ColorSpaceXformHelper>();
  uint64_t key = ColorSpaceXformSteps::XFormKey(steps);
  if (stepKeySet.find(key) == stepKeySet.end()) {
    colorXformHelper->emitUniform(uniformHandler, steps);
    colorXformHelper->emitFunction(this, steps);
    stepKeySet.insert(key);
  }
}

void ShaderBuilder::appendColorGamutXformCode(const char* srcColor,
                                              const ColorSpaceXformSteps* steps) {
  if (steps->flags.mask() != 0) {
    uint64_t key = ColorSpaceXformSteps::XFormKey(steps);
    std::string nameSuffix = std::to_string(key);
    std::string colorXformFuncName = "color_xform_" + nameSuffix;
    colorXformFuncName = getMangledFunctionName(colorXformFuncName.c_str());
    codeAppendf("%s = %s(%s);", srcColor, colorXformFuncName.c_str(), srcColor);
  }
}

void ShaderBuilder::addFeature(uint32_t featureBit, const std::string& extensionName) {
  if (featureBit & features) {
    return;
  }
  char buffer[ShaderBufferSize];
  auto length =
      snprintf(buffer, ShaderBufferSize, "#extension %s: require\n", extensionName.c_str());
  shaderStrings[Type::Extensions].append(buffer, static_cast<size_t>(length));
  features |= featureBit;
}

std::string ShaderBuilder::getDeclarations(const std::vector<ShaderVar>& vars,
                                           ShaderStage stage) const {
  std::string ret;
  for (const auto& var : vars) {
    ret += programBuilder->getShaderVarDeclarations(var, stage);
    ret += ";\n";
  }
  return ret;
}

void ShaderBuilder::finalize() {
  if (finalized) {
    return;
  }
  auto shaderCaps = programBuilder->getContext()->caps()->shaderCaps();
  shaderStrings[Type::VersionDecl] = shaderCaps->versionDeclString;
  auto type = shaderStage();
  shaderStrings[Type::Uniforms] += programBuilder->uniformHandler()->getUniformDeclarations(type);
  shaderStrings[Type::Inputs] += getDeclarations(inputs, type);
  shaderStrings[Type::Outputs] += getDeclarations(outputs, type);
  programBuilder->varyingHandler()->getDeclarations(&shaderStrings[Type::Inputs],
                                                    &shaderStrings[Type::Outputs], type);
  // append the 'footer' to code
  shaderStrings[Type::Code] += "}\n";
  finalized = true;
}

void ShaderBuilder::appendEnterIfNotEmpty(uint8_t type) {
  if (shaderStrings[type].empty()) {
    return;
  }
  shaderStrings[type] += "\n";
}

void ShaderBuilder::appendIndentationIfNeeded(const std::string& code) {
  if (indentation <= 0 || !atLineStart) {
    return;
  }
  if (code.find('}') != std::string::npos) {
    --indentation;
  }
  for (int i = 0; i < indentation; ++i) {
    shaderStrings[Type::Code] += "    ";
  }
  if (code.find('{') != std::string::npos) {
    ++indentation;
  }
  atLineStart = false;
}

std::string ShaderBuilder::shaderString() {
  std::string fragment;
  for (auto& str : shaderStrings) {
    if (str.empty()) {
      continue;
    }
    fragment += str;
    fragment += "\n";
  }
  return fragment;
}
}  // namespace tgfx
