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
  varying._id = static_cast<VaryingID>(varyings.size());
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

}  // namespace tgfx
