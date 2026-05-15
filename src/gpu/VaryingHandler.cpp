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

#include "VaryingHandler.h"
#include <string>
#include "ProgramBuilder.h"

namespace tgfx {
Varying VaryingHandler::addVarying(const std::string& name, SLType type, bool isFlat) {
  Varying varying;
  varying._type = type;
  varying._name = programBuilder->nameVariable(name);
  varying._isFlat = isFlat;
  varyings.push_back(std::move(varying));
  return varyings[varyings.size() - 1];
}

void VaryingHandler::emitAttributes(const GeometryProcessor& processor) {
  for (auto& attribute : processor.vertexAttributes()) {
    addAttribute(ShaderVar(attribute));
  }
  for (auto& attribute : processor.instanceAttributes()) {
    addAttribute(ShaderVar(attribute));
  }
}

void VaryingHandler::addAttribute(const ShaderVar& var) {
  for (const auto& attr : vertexInputs) {
    // if attribute already added, don't add it again
    if (attr.name() == var.name()) {
      return;
    }
  }
  vertexInputs.push_back(var);
}

void VaryingHandler::finalize() {
  for (const auto& v : varyings) {
    vertexOutputs.emplace_back(
        v._name, v.type(),
        v._isFlat ? ShaderVar::TypeModifier::FlatVarying : ShaderVar::TypeModifier::Varying);
    fragInputs.emplace_back(
        v._name, v.type(),
        v._isFlat ? ShaderVar::TypeModifier::FlatVarying : ShaderVar::TypeModifier::Varying);
  }
}

void VaryingHandler::appendDecls(const std::vector<ShaderVar>& vars, std::string* out,
                                 ShaderStage stage) const {
  // Track the next interpolant location to assign so vertex outputs and fragment inputs end up
  // at exactly matching slots. Without this, ShaderCompiler::PreprocessGLSL would later number
  // the two stages independently, and any unused fragment input would shift the location of all
  // subsequent inputs — producing a valid GLSL program (Vulkan/GL link by location) but failing
  // D3D12's strict signature check ("PS input register is not a subset of VS output").
  uint32_t location = 0;
  for (const auto& var : vars) {
    bool isVarying = var.modifier() == ShaderVar::TypeModifier::Varying ||
                     var.modifier() == ShaderVar::TypeModifier::FlatVarying;
    if (isVarying) {
      out->append("layout(location=");
      out->append(std::to_string(location));
      out->append(") ");
      ++location;
    }
    out->append(programBuilder->getShaderVarDeclarations(var, stage));
    out->append(";\n");
  }
}

void VaryingHandler::getDeclarations(std::string* inputDecls, std::string* outputDecls,
                                     ShaderStage stage) const {
  if (stage == ShaderStage::Vertex) {
    appendDecls(vertexInputs, inputDecls, ShaderStage::Vertex);
    appendDecls(vertexOutputs, outputDecls, ShaderStage::Vertex);
  } else {
    appendDecls(fragInputs, inputDecls, ShaderStage::Fragment);
  }
}
}  // namespace tgfx
