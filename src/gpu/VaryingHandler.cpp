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
  for (const auto* attr : processor.vertexAttributes()) {
    addAttribute(attr->asShaderVar());
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
                                 ShaderFlags flag) const {
  for (const auto& var : vars) {
    out->append(programBuilder->getShaderVarDeclarations(var, flag));
    out->append(";\n");
  }
}

void VaryingHandler::getVertexDecls(std::string* inputDecls, std::string* outputDecls) const {
  appendDecls(vertexInputs, inputDecls, ShaderFlags::Vertex);
  appendDecls(vertexOutputs, outputDecls, ShaderFlags::Vertex);
}

void VaryingHandler::getFragDecls(std::string* inputDecls) const {
  appendDecls(fragInputs, inputDecls, ShaderFlags::Fragment);
}
}  // namespace tgfx
