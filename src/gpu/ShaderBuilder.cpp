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
#include "gpu/shaders/ShaderLibraryBundle.h"
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

const ShaderCaps* ShaderBuilder::shaderCaps() const {
  return programBuilder->getContext()->shaderCaps();
}

void ShaderBuilder::setPrecisionQualifier(const std::string& precision) {
  shaderStrings[Type::PrecisionQualifier] = precision;
}

ValueRef ShaderBuilder::internValue(const std::string& name, SLType type, ValueOrigin origin) {
  if (valueTable == nullptr) {
    return ValueRef(name, type, origin);
  }
  const auto id = valueTable->Intern(name, type, origin);
  const auto* value = valueTable->Find(id);
  return value != nullptr ? *value : ValueRef(name, type, origin);
}

void ShaderBuilder::codeAppendf(const char* format, ...) {
  char buffer[ShaderBufferSize] = {};
  va_list args;
  va_start(args, format);
  const auto length = vsnprintf(buffer, ShaderBufferSize, format, args);
  va_end(args);
  if (length <= 0) {
    return;
  }
  if (static_cast<size_t>(length) < ShaderBufferSize) {
    codeAppend(std::string(buffer, static_cast<size_t>(length)));
    return;
  }
  // vsnprintf reports the length it needed; the stack buffer only holds the truncated prefix.
  std::string expanded(static_cast<size_t>(length) + 1, '\0');
  va_start(args, format);
  vsnprintf(expanded.data(), expanded.size(), format, args);
  va_end(args);
  expanded.resize(static_cast<size_t>(length));
  codeAppend(expanded);
}

void ShaderBuilder::codeAppend(const std::string& str) {
  if (str.empty()) {
    return;
  }
  auto& code = shaderStrings[Type::Code];
  const size_t start = code.size();
  appendIndentationIfNeeded(str);
  code += str;
  if (NeedsAppendEnter(code)) {
    appendEnterIfNotEmpty(Type::Code);
  }
  atLineStart = code[code.size() - 1] == '\n';
  if (fragmentProgramRecorder != nullptr && rawCaptureEnabled &&
      shaderStage() == ShaderStage::Fragment) {
    FragmentInstruction instruction = {};
    instruction.kind = FragmentInstructionKind::RawText;
    instruction.text = code.substr(start);
    fragmentProgramRecorder->instructions.push_back(std::move(instruction));
  }
}

void ShaderBuilder::addFunction(const std::string& str) {
  appendEnterIfNotEmpty(Type::Functions);
  shaderStrings[Type::Functions] += str;
}

std::string ShaderBuilder::appendFunction(const char* name) {
  std::string text;
  if (!ShaderLibraryBundle::Get().appendFunction(libraryLanguage, name, &emittedFunctions, &text)) {
    return "";
  }
  if (!text.empty()) {
    addFunction(text);
  }
  return name;
}

std::string ShaderBuilder::clampToSubsets(const std::string& coordName,
                                          const std::string& extraSubset,
                                          const std::string& subset) {
  if (extraSubset.empty() && subset.empty()) {
    return coordName;
  }
  auto entry = appendFunction("tgfxClampSubset");
  if (entry.empty()) {
    return coordName;
  }
  const std::string out = "clampedCoord" + std::to_string(clampedCoordIndex++);
  codeAppendf("highp vec2 %s = %s;", out.c_str(), coordName.c_str());
  if (!extraSubset.empty()) {
    codeAppendf("%s = %s(%s, %s);", out.c_str(), entry.c_str(), out.c_str(), extraSubset.c_str());
  }
  if (!subset.empty()) {
    codeAppendf("%s = %s(%s, %s);", out.c_str(), entry.c_str(), out.c_str(), subset.c_str());
  }
  recordClampSubsetsIR(coordName, extraSubset, subset, out);
  return out;
}

void ShaderBuilder::recordClampSubsetsIR(const std::string& coordName,
                                         const std::string& extraSubset, const std::string& subset,
                                         const std::string& out) {
  auto& ir = programBuilder->programIRBuilder();
  auto coordKind = IRValueKind::Local;
  for (const auto& varying : ir.IR().interface.varyings) {
    if (varying.name == coordName) {
      coordKind = IRValueKind::Varying;
      break;
    }
  }
  const auto sourceValue = ir.InternValue(coordName, SLType::Float2, Precision::High, coordKind);
  if (sourceValue == INVALID_VALUE_ID) {
    return;
  }
  const auto outValue = ir.InternValue(out, SLType::Float2, Precision::High, IRValueKind::Local);
  if (outValue == INVALID_VALUE_ID) {
    return;
  }
  ValueID current = sourceValue;
  if (!extraSubset.empty()) {
    const auto subsetValue =
        ir.InternValue(extraSubset, SLType::Float4, Precision::High, IRValueKind::Uniform);
    current = ir.CreateCall(OperatorID::ClampSubset, SLType::Float2, {current, subsetValue}, out);
  }
  if (!subset.empty()) {
    const auto subsetValue =
        ir.InternValue(subset, SLType::Float4, Precision::High, IRValueKind::Uniform);
    current = ir.CreateCall(OperatorID::ClampSubset, SLType::Float2, {current, subsetValue}, out);
  }
  ir.Assign({outValue, ""}, current);
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

std::string ShaderBuilder::getSamplerSwizzleSuffix(SamplerHandle samplerHandle) const {
  return TextureSwizzleString(programBuilder->uniformHandler()->getSamplerSwizzle(samplerHandle));
}

const char* ShaderBuilder::TextureReaderName(SLType samplerType) {
  switch (samplerType) {
    case SLType::Texture2DSampler:
      return "tgfxTextureSample";
    case SLType::Texture2DRectSampler:
      return "tgfxTextureSampleRect";
    case SLType::TextureExternalSampler:
      return "tgfxTextureSampleExternal";
    default:
      return nullptr;
  }
}

int ShaderBuilder::ReadSwizzleCode(const Swizzle& swizzle) {
  if (swizzle == Swizzle::RGBA()) {
    return 0;
  }
  if (swizzle == Swizzle::RRRR()) {
    return 1;
  }
  const std::string channels = swizzle.c_str();
  if (channels == "rrra") {
    return 2;
  }
  if (channels == "rgrg") {
    return 3;
  }
  if (swizzle == Swizzle::AAAA()) {
    return 4;
  }
  if (channels == "bgra") {
    return 5;
  }
  return -1;
}

std::string ShaderBuilder::textureLookupExpression(SamplerHandle samplerHandle,
                                                   const std::string& coordName) {
  auto uniformHandler = programBuilder->uniformHandler();
  auto sampler = uniformHandler->getSamplerVariable(samplerHandle);
  const char* reader = TextureReaderName(sampler.type());
  auto entry = reader == nullptr ? std::string() : appendFunction(reader);
  // The 2D reader carries the subset clamp as a call argument, so every reader spells the same
  // four-argument shape; the rect/external readers keep their two-argument form for now.
  const bool carriesSubset = sampler.type() == SLType::Texture2DSampler;
  const int swizzleCode =
      carriesSubset ? ReadSwizzleCode(uniformHandler->getSamplerSwizzle(samplerHandle)) : -1;
  std::string expression = {};
  if (entry.empty()) {
    expression = "texture(" + sampler.name() + ", " + coordName + ")";
  } else if (carriesSubset && swizzleCode >= 0) {
    expression = entry + "(" + sampler.name() + ", " + coordName + ", vec4(0.0), 0, " +
                 std::to_string(swizzleCode) + ")";
  } else {
    expression = entry + "(" + sampler.name() + ", " + coordName + ")";
  }
  expression += TextureSwizzleString(uniformHandler->getSamplerSwizzle(samplerHandle));
  return expression;
}

void ShaderBuilder::appendTextureLookup(SamplerHandle samplerHandle, const std::string& coordName) {
  codeAppend(textureLookupExpression(samplerHandle, coordName));
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

void ShaderBuilder::finalize() {
  if (finalized) {
    return;
  }
  auto shaderCaps = programBuilder->getContext()->shaderCaps();
  shaderStrings[Type::VersionDecl] = shaderCaps->versionDeclString;
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

void ShaderBuilder::setCodeText(const std::string& text) {
  shaderStrings[Type::Code] = text;
  atLineStart = true;
}

}  // namespace tgfx
