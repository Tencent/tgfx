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
#include <cctype>
#include "gpu/glsl/GLSLProgramBuilder.h"
#include "gpu/shaders/contract/OpTable.h"

namespace tgfx {
static constexpr char DstColorName[] = "_dstColor";

namespace {

bool IsIdentifier(const std::string& text) {
  if (text.empty() || (!std::isalpha(static_cast<unsigned char>(text[0])) && text[0] != '_')) {
    return false;
  }
  for (const char character : text) {
    if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') {
      return false;
    }
  }
  return true;
}

SLType ShaderTypeOf(OpTable::Param param) {
  switch (param) {
    case OpTable::Param::Float:
      return SLType::Float;
    case OpTable::Param::Int:
      return SLType::Int;
    case OpTable::Param::Vec2:
      return SLType::Float2;
    case OpTable::Param::Vec3:
      return SLType::Float3;
    case OpTable::Param::Vec4:
      return SLType::Float4;
    case OpTable::Param::Mat3:
      return SLType::Float3x3;
    case OpTable::Param::Mat4:
      return SLType::Float4x4;
    case OpTable::Param::IVec4:
      return SLType::Int4;
    default:
      return SLType::Void;
  }
}

const OpTable::Row* FindOperator(const std::string& functionName) {
  for (size_t index = 0; index < OpTable::RowCount(); ++index) {
    const auto* row = OpTable::Rows() + index;
    if (row->exportName != nullptr && functionName == row->exportName) {
      return row;
    }
  }
  return nullptr;
}

}  // namespace
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
                                           const std::vector<ValueRef>& args,
                                           const std::string& wrapType,
                                           const std::string& expression) {
  if (callRecorder != nullptr) {
    CallRecord record = {};
    record.output = outputVar;
    record.function = functionName;
    for (const auto& arg : args) {
      record.args.push_back(arg.name);
    }
    record.order = nextEmitOrder();
    callRecorder->push_back(std::move(record));
  }
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  codeAppendf("%s = ", outputVar.c_str());
  if (!wrapType.empty()) {
    codeAppendf("%s(", wrapType.c_str());
  }
  if (!expression.empty()) {
    codeAppend(expression);
  } else {
    codeAppendf("%s(", functionName.c_str());
    bool first = true;
    for (const auto& arg : args) {
      if (!first) {
        codeAppend(", ");
      }
      first = false;
      codeAppend(arg.name);
    }
    codeAppend(")");
  }
  if (!wrapType.empty()) {
    codeAppend(")");
  }
  codeAppend(";");
  setRawCaptureEnabled(true);
  if (fragmentProgramRecorder != nullptr) {
    FragmentInstruction instruction = {};
    instruction.kind = FragmentInstructionKind::Call;
    instruction.output = outputVar;
    instruction.function = functionName;
    const auto* row = FindOperator(functionName);
    const SLType resultType =
        row != nullptr && wrapType.empty() ? ShaderTypeOf(row->returnType) : SLType::Void;
    instruction.outputRef = internValue(outputVar, resultType, ValueOrigin::CallResult);
    const bool typed = row != nullptr && row->args != nullptr && row->argCount == args.size();
    for (size_t index = 0; index < args.size(); ++index) {
      const auto& arg = args[index];
      instruction.args.push_back(arg.name);
      ValueRef ref = arg;
      if (typed && ref.type == SLType::Void && IsIdentifier(ref.name)) {
        ref.type = ShaderTypeOf(row->args[index].type);
      }
      if (ref.id == INVALID_VALUE_ID) {
        auto interned = internValue(ref.name, ref.type, ref.origin);
        if (interned.id != INVALID_VALUE_ID) {
          ref = std::move(interned);
        }
      }
      instruction.argRefs.push_back(std::move(ref));
    }
    instruction.wrapType = wrapType;
    instruction.text = codeTextFrom(start);
    fragmentProgramRecorder->instructions.push_back(std::move(instruction));
  }
}

std::string GLSLFragmentShaderBuilder::appendTempCall(const std::string& functionName,
                                                      const std::vector<ValueRef>& args) {
  auto entry = appendFunction(functionName.c_str());
  if (entry.empty()) {
    return "";
  }
  const std::string temp = "tgfxTemp" + std::to_string(tempCallIndex++);
  const size_t start = codeTextSize();
  setRawCaptureEnabled(false);
  codeAppendf("vec4 %s = %s(", temp.c_str(), entry.c_str());
  bool first = true;
  for (const auto& arg : args) {
    if (!first) {
      codeAppend(", ");
    }
    first = false;
    codeAppend(arg.name);
  }
  codeAppend(");");
  setRawCaptureEnabled(true);
  if (fragmentProgramRecorder != nullptr) {
    FragmentInstruction instruction = {};
    instruction.kind = FragmentInstructionKind::Call;
    instruction.output = temp;
    instruction.function = functionName;
    instruction.outputRef = internValue(temp, SLType::Float4, ValueOrigin::CallResult);
    for (const auto& arg : args) {
      instruction.args.push_back(arg.name);
      ValueRef ref = arg;
      if (ref.id == INVALID_VALUE_ID) {
        auto interned = internValue(arg.name, arg.type, arg.origin);
        if (interned.id != INVALID_VALUE_ID) {
          ref = std::move(interned);
        }
      }
      instruction.argRefs.push_back(std::move(ref));
    }
    instruction.declaresResult = true;
    instruction.resultType = "vec4";
    instruction.text = codeTextFrom(start);
    fragmentProgramRecorder->instructions.push_back(std::move(instruction));
  }
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
