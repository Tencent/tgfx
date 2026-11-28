/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include "GLSLPrinter.h"
#include "core/utils/Log.h"
#include "GLSLOperatorEmitter.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/ir/ProgramBodyPrinter.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/ShaderVar.h"
#include "gpu/VertexShaderBuilder.h"

namespace tgfx {
namespace {

void AppendSection(std::string* out, const std::string& section) {
  if (section.empty()) {
    return;
  }
  *out += section;
  *out += "\n";
}

}  // namespace

void GLSLPrinter::UniquifyValueNames(ProgramIR& program) {
  ProgramBodyPrinter::Uniquify(program);
}

std::string GLSLPrinter::PrintFragmentBodyFromIR(const ProgramIR& program) {
  return ProgramBodyPrinter::PrintFragment(program, ProgramBodyPrinter::GLSLTraits());
}

std::string GLSLPrinter::PrintVertexBodyFromIR(const ProgramIR& program) {
  return ProgramBodyPrinter::PrintVertex(program, ProgramBodyPrinter::GLSLTraits());
}

std::string GLSLPrinter::PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                    const InterfaceIR& interface, const ShaderBuilder& builder) {
  std::string uniforms = builder.extraUniforms();
  std::vector<UniformDesc> uniformDescriptors = {};
  for (const auto& uniform : interface.uniforms) {
    if (uniform.stage == stage) {
      uniformDescriptors.push_back({uniform.name, uniform.type, uniform.stage, uniform.count});
    }
  }
  uniforms += programBuilder.getUniformBlockDeclaration(stage, uniformDescriptors);
  if (stage == ShaderStage::Fragment) {
    for (const auto& sampler : interface.samplers) {
      ShaderVar var(sampler.name, sampler.type, ShaderVar::TypeModifier::Uniform);
      uniforms += programBuilder.getShaderVarDeclarations(var, stage);
      uniforms += ";\n";
    }
  }

  std::string inputs = {};
  std::string outputs = {};
  for (const auto& input : builder.inputVars()) {
    inputs += programBuilder.getShaderVarDeclarations(input, stage);
    inputs += ";\n";
  }
  for (const auto& output : builder.outputVars()) {
    outputs += programBuilder.getShaderVarDeclarations(output, stage);
    outputs += ";\n";
  }
  if (stage == ShaderStage::Vertex) {
    for (const auto& attribute : interface.attributes) {
      ShaderVar var(attribute.name, attribute.type, ShaderVar::TypeModifier::Attribute);
      inputs += programBuilder.getShaderVarDeclarations(var, stage);
      inputs += ";\n";
    }
    for (const auto& varying : interface.varyings) {
      ShaderVar var(
          varying.name, varying.type,
          varying.flat ? ShaderVar::TypeModifier::FlatVarying : ShaderVar::TypeModifier::Varying);
      outputs += programBuilder.getShaderVarDeclarations(var, stage);
      outputs += ";\n";
    }
  } else {
    for (const auto& varying : interface.varyings) {
      ShaderVar var(
          varying.name, varying.type,
          varying.flat ? ShaderVar::TypeModifier::FlatVarying : ShaderVar::TypeModifier::Varying);
      inputs += programBuilder.getShaderVarDeclarations(var, stage);
      inputs += ";\n";
    }
  }

  std::string out = {};
  AppendSection(&out, builder.versionDecl());
  AppendSection(&out, builder.extensions());
  AppendSection(&out, builder.definitions());
  AppendSection(&out, builder.precisionQualifier());
  AppendSection(&out, uniforms);
  AppendSection(&out, inputs);
  AppendSection(&out, outputs);
  AppendSection(&out, builder.functions());
  AppendSection(&out, builder.mainHeader());
  AppendSection(&out, builder.code());
  return out;
}

}  // namespace tgfx
