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

void ShaderBuilder::appendColorGamutXform(std::string* out, const char* srcColor,
                                          ColorSpaceXformHelper* colorXformHelper) {
  if (!colorXformHelper || colorXformHelper->isNoop()) {
    *out = srcColor;
    return;
  }

  auto emitTFFunc = [this](const char* name, const char* tfVar0, const char* tfVar1,
                           gfx::skcms_TFType tfType) {
    std::string funcName = this->getMangledFunctionName(name);
    std::string function;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "float %s(float x)\n", funcName.c_str());
    function += buffer;
    function += "{\n";
    snprintf(buffer, sizeof(buffer), "\tfloat G = %s[0];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat A = %s[1];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat B = %s[2];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat C = %s[3];\n", tfVar0);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat D = %s[0];\n", tfVar1);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat E = %s[1];\n", tfVar1);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\tfloat F = %s[2];\n", tfVar1);
    function += buffer;
    function += "\tfloat s = sign(x);\n";
    function += "\tx = abs(x);\n";
    switch (tfType) {
      case gfx::skcms_TFType_sRGBish:
        function += "\tx = (x < D) ? (C * x) + F : pow(A * x + B, G) + E;\n";
        break;
      case gfx::skcms_TFType_PQish:
        function += "\tx = pow(max(A + B * pow(x, C), 0.0f) / (D + E * pow(x, C)), F);\n";
        break;
      case gfx::skcms_TFType_HLGish:
        function +=
            "\tx = (x * A <= 1.0f) ? pow(x * A, B) : exp(( x - E) * C) + D; x *= (F + 1.0f);\n";
        break;
      case gfx::skcms_TFType_HLGinvish:
        function += "\tx /= (F + 1.0f); x = (x <= 1.0f) ? A * pow(x, B) : C * log(x - D) + E;\n";
        break;
      default:
        DEBUG_ASSERT(false);
        break;
    }
    function += "\treturn s * x;\n";
    function += "}\n";
    this->addFunction(function);
    return funcName;
  };

  std::string srcTFFunctionName;
  if (colorXformHelper->applySrcTF()) {
    srcTFFunctionName =
        emitTFFunc("src_tf", colorXformHelper->srcTFUniform0().c_str(),
                   colorXformHelper->srcTFUniform1().c_str(), colorXformHelper->srcTFType());
  }

  std::string dstTFFunctionName;
  if (colorXformHelper->applyDstTF()) {
    dstTFFunctionName =
        emitTFFunc("dst_tf", colorXformHelper->dstTFUniform0().c_str(),
                   colorXformHelper->dstTFUniform1().c_str(), colorXformHelper->dstTFType());
  }

  auto emitOOTFFunc = [this](const char* name, const char* ootfVar) {
    std::string funcName = this->getMangledFunctionName(name);
    std::string function;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "vec3 %s(vec3 color)\n", funcName.c_str());
    function += buffer;
    function += "{\n";
    snprintf(buffer, sizeof(buffer), "\tfloat Y = dot(color, %s.rgb);\n", ootfVar);
    function += buffer;
    snprintf(buffer, sizeof(buffer), "\treturn color * sign(Y) * pow(abs(Y), %s.a);\n", ootfVar);
    function += buffer;
    function += "}\n";
    this->addFunction(function);
    return funcName;
  };

  std::string srcOOTFFuncName;
  if (colorXformHelper->applySrcOOTF()) {
    srcOOTFFuncName = emitOOTFFunc("src_ootf", colorXformHelper->srcOOTFUniform().c_str());
  }

  std::string dstOOTFFuncName;
  if (colorXformHelper->applyDstOOTF()) {
    dstOOTFFuncName = emitOOTFFunc("dst_ootf", colorXformHelper->dstOOTFUniform().c_str());
  }

  std::string gamutXformFuncName;
  if (colorXformHelper->applyGamutXform()) {
    gamutXformFuncName = this->getMangledFunctionName("gamut_xform");
    std::string function;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "vec4 %s(vec4 color)\n", gamutXformFuncName.c_str());
    function += buffer;
    function += "{\n";
    snprintf(buffer, sizeof(buffer), "\tcolor.rgb = (%s * color.rgb);\n",
             colorXformHelper->gamutXformUniform().c_str());
    function += buffer;
    function += "\treturn color;\n";
    function += "}\n";
    addFunction(function);
  }

  {
    std::string function;
    std::string colorXformFuncName = getMangledFunctionName("color_xform");
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "vec4 %s(vec4 color)\n", colorXformFuncName.c_str());
    function += buffer;
    function += "{\n";
    if (colorXformHelper->applyUnpremul()) {
      function += "\tfloat alpha = color.a;\n";
      function +=
          "\tcolor = alpha > 0.0f ? vec4(color.rgb / alpha, alpha) : vec4(0.0f, 0.0f, 0.0f, "
          "0.0f);\n";
    }
    if (colorXformHelper->applySrcTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.r = %s(color.r);\n", srcTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.g = %s(color.g);\n", srcTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.b = %s(color.b);\n", srcTFFunctionName.c_str());
      function += buffer;
    }
    if (colorXformHelper->applySrcOOTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.rgb = %s(color.rgb);\n", srcOOTFFuncName.c_str());
      function += buffer;
    }
    if (colorXformHelper->applyGamutXform()) {
      snprintf(buffer, sizeof(buffer), "\tcolor = %s(color);\n", gamutXformFuncName.c_str());
      function += buffer;
    }
    if (colorXformHelper->applyDstOOTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.rgb = %s(color.rgb);\n", dstOOTFFuncName.c_str());
      function += buffer;
    }
    if (colorXformHelper->applyDstTF()) {
      snprintf(buffer, sizeof(buffer), "\tcolor.r = %s(color.r);\n", dstTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.g = %s(color.g);\n", dstTFFunctionName.c_str());
      function += buffer;
      snprintf(buffer, sizeof(buffer), "\tcolor.b = %s(color.b);\n", dstTFFunctionName.c_str());
      function += buffer;
    }
    if (colorXformHelper->applyPremul()) {
      function += "\tcolor.rgb *= color.a;\n";
    }
    function += "\treturn color;\n";
    function += "}\n";
    addFunction(function);
    snprintf(buffer, sizeof(buffer), "%s(%s)", colorXformFuncName.c_str(), srcColor);
    *out += buffer;
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
  auto shaderCaps = programBuilder->getContext()->shaderCaps();
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
