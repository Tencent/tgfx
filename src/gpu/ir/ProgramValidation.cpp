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
#include "gpu/shaders/contract/OpTable.h"

namespace tgfx {
namespace {

const char* TypeName(SLType type) {
  switch (type) {
    case SLType::Float:
      return "float";
    case SLType::Float2:
      return "vec2";
    case SLType::Float3:
      return "vec3";
    case SLType::Float4:
      return "vec4";
    default:
      return "unknown";
  }
}

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

void RefreshValueRef(ValueRef* value, const ValueTable& values) {
  if (value->name.empty()) {
    return;
  }
  const auto* resolved = values.Find(value->name);
  if (resolved != nullptr) {
    *value = *resolved;
  }
}

void RefreshValueTypes(ProgramDescription* description) {
  for (auto& call : description->vertexCalls) {
    RefreshValueRef(&call.dst, description->values);
    RefreshValueRef(&call.src, description->values);
    RefreshValueRef(&call.operand, description->values);
    RefreshValueRef(&call.extra, description->values);
  }
  for (auto& instruction : description->fragment.instructions) {
    RefreshValueRef(&instruction.outputRef, description->values);
    for (auto& argument : instruction.argRefs) {
      RefreshValueRef(&argument, description->values);
    }
  }
}

VaryingDesc* FindVarying(InterfaceDesc* interface, const std::string& name) {
  for (auto& varying : interface->varyings) {
    if (varying.name == name) {
      return &varying;
    }
  }
  return nullptr;
}

VaryingDesc* FindVaryingByID(InterfaceDesc* interface, VaryingID id) {
  for (auto& varying : interface->varyings) {
    if (varying.id == id) {
      return &varying;
    }
  }
  return nullptr;
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

bool ValidateFragmentCalls(ProgramDescription* description, std::string* error) {
  for (const auto& instruction : description->fragment.instructions) {
    if (instruction.kind != FragmentInstructionKind::Call) {
      continue;
    }
    const auto* row = FindOperator(instruction.function);
    if (row == nullptr || row->args == nullptr) {
      continue;
    }
    if (instruction.argRefs.size() != row->argCount) {
      *error = "the call to '" + instruction.function + "' has " +
               std::to_string(instruction.argRefs.size()) +
               " arguments, but the operator table "
               "declares " +
               std::to_string(row->argCount);
      return false;
    }
    for (size_t index = 0; index < instruction.argRefs.size(); ++index) {
      const auto expected = ShaderTypeOf(row->args[index].type);
      const auto actual = instruction.argRefs[index].type;
      if (expected != SLType::Void && actual != SLType::Void && actual != expected) {
        *error = "the argument " + std::to_string(index) + " of '" + instruction.function +
                 "' is " + TypeName(actual) + ", but the operator table declares " +
                 TypeName(expected);
        return false;
      }
    }
    const auto expectedResult = ShaderTypeOf(row->returnType);
    const auto actualResult = instruction.outputRef.type;
    if (instruction.wrapType.empty() && expectedResult != SLType::Void &&
        actualResult != SLType::Void && expectedResult != actualResult) {
      *error = "the call to '" + instruction.function + "' returns " + TypeName(expectedResult) +
               ", but its output is " + TypeName(actualResult);
      return false;
    }
  }
  return true;
}

bool ValidateFragmentCoords(ProgramDescription* description, std::string* error) {
  for (const auto& instruction : description->fragment.instructions) {
    if (instruction.varying == INVALID_VARYING) {
      continue;
    }
    const bool project = instruction.kind == FragmentInstructionKind::Project;
    const bool read = instruction.kind == FragmentInstructionKind::ReadVarying;
    if (!project && !read) {
      continue;
    }
    auto* varying = FindVaryingByID(&description->interface, instruction.varying);
    if (varying == nullptr) {
      *error = "the fragment stage reads undeclared varying " + std::to_string(instruction.varying);
      return false;
    }
    const SLType expected = project ? SLType::Float3 : SLType::Float2;
    if (varying->type != expected) {
      *error = "the fragment stage reads varying '" + varying->name + "' as " + TypeName(expected) +
               ", but it is declared as " + TypeName(varying->type);
      return false;
    }
  }
  return true;
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
    auto* varying = FindVarying(&description->interface, call.dst.name);
    if (varying == nullptr) {
      *error = "the vertex stage writes undeclared varying '" + call.dst.name + "'";
      return false;
    }
    if (call.resultType != SLType::Void && call.resultType != varying->type) {
      *error = "the vertex stage writes " + std::string(TypeName(call.resultType)) +
               " into varying '" + varying->name + "' declared as " + TypeName(varying->type);
      return false;
    }
    if (call.kind == VertexCallKind::StoreVarying && call.src.type != SLType::Void &&
        call.src.type != varying->type) {
      *error = "the vertex stage stores " + std::string(TypeName(call.src.type)) +
               " into varying '" + varying->name + "' declared as " + TypeName(varying->type);
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
  if (!AssignIDs(description, error)) {
    return false;
  }
  RefreshValueTypes(description);
  return LinkVertexWrites(description, error) && ValidateFragmentCoords(description, error) &&
         ValidateFragmentCalls(description, error);
}

}  // namespace tgfx
