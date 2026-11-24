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

#include "gpu/ir/ProgramValidation.h"

namespace tgfx {
namespace {

bool AssignIDs(ProgramDescription* description, std::string* error) {
  auto& interface = description->interface;
  if (interface.attributes.size() > 0xFFFFu) {
    *error = "the attribute count exceeds the 16-bit ID space";
    return false;
  }
  for (size_t index = 0; index < interface.attributes.size(); ++index) {
    interface.attributes[index].id = static_cast<AttributeID>(index);
  }
  if (interface.varyings.size() > 0xFFFFu || interface.varyings.size() > 0xFFu) {
    *error = "the varying count exceeds the 8-bit location space";
    return false;
  }
  for (size_t index = 0; index < interface.varyings.size(); ++index) {
    interface.varyings[index].id = static_cast<VaryingID>(index);
    interface.varyings[index].location = static_cast<uint8_t>(index);
    interface.varyings[index].produced = false;
  }
  if (interface.uniforms.size() > 0xFFFFu) {
    *error = "the uniform count exceeds the 16-bit ID space";
    return false;
  }
  for (size_t index = 0; index < interface.uniforms.size(); ++index) {
    interface.uniforms[index].id = static_cast<UniformID>(index);
  }
  if (interface.samplers.size() > 0xFFFFu) {
    *error = "the sampler count exceeds the 16-bit ID space";
    return false;
  }
  for (size_t index = 0; index < interface.samplers.size(); ++index) {
    interface.samplers[index].id = static_cast<SamplerID>(index);
  }
  return true;
}

VaryingDesc* FindVarying(InterfaceDesc* interface, const std::string& name) {
  for (auto& varying : interface->varyings) {
    if (varying.name == name) {
      return &varying;
    }
  }
  return nullptr;
}

bool LinkVertexWrites(ProgramDescription* description, std::string* error) {
  bool hasPosition = false;
  for (auto& call : description->vertexCalls) {
    if (call.kind == VertexCallKind::StorePosition) {
      hasPosition = true;
      continue;
    }
    if (call.kind != VertexCallKind::StoreVarying &&
        call.kind != VertexCallKind::TransformVarying) {
      continue;
    }
    auto* varying = FindVarying(&description->interface, call.dst);
    if (varying == nullptr) {
      *error = "the vertex stage writes undeclared varying '" + call.dst + "'";
      return false;
    }
    call.varying = varying->id;
    varying->produced = true;
  }
  if (!hasPosition) {
    *error = "the vertex stage never writes the clip-space position";
    return false;
  }
  for (const auto& varying : description->interface.varyings) {
    if (!varying.produced) {
      *error = "the vertex stage never produced the varying '" + varying.name + "'";
      return false;
    }
  }
  return true;
}

}  // namespace

bool ValidateProgramDescription(ProgramDescription* description, std::string* error) {
  if (description == nullptr || error == nullptr) {
    return false;
  }
  return AssignIDs(description, error) && LinkVertexWrites(description, error);
}

}  // namespace tgfx
