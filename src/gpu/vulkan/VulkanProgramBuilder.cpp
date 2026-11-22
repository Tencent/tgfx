/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "VulkanProgramBuilder.h"
#include <cstdlib>
#include <map>
#include "core/utils/Log.h"
#include "gpu/ShaderCaps.h"
#include "gpu/aot/chain/ChainProgramPlan.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {
namespace {

FragmentType FragmentTypeOf(UniformFormat format) {
  switch (format) {
    case UniformFormat::Float:
      return FragmentType::Float;
    case UniformFormat::Float2:
      return FragmentType::Float2;
    case UniformFormat::Float3:
      return FragmentType::Float3;
    case UniformFormat::Float4:
      return FragmentType::Float4;
    case UniformFormat::Float3x3:
      return FragmentType::Matrix3;
    case UniformFormat::Float4x4:
      return FragmentType::Matrix4;
    case UniformFormat::Int:
      return FragmentType::Int;
    case UniformFormat::Int4:
      return FragmentType::Int4;
    default:
      return FragmentType::Float4;
  }
}

FragmentType FragmentTypeOf(SLType type) {
  switch (type) {
    case SLType::Float:
      return FragmentType::Float;
    case SLType::Float2:
      return FragmentType::Float2;
    case SLType::Float3:
      return FragmentType::Float3;
    case SLType::Float4:
      return FragmentType::Float4;
    case SLType::Int:
      return FragmentType::Int;
    case SLType::Int4:
      return FragmentType::Int4;
    default:
      return FragmentType::Float4;
  }
}

// The std140 footprint one member occupies (arrays stride every element to 16 bytes).
uint32_t Std140Size(FragmentType type, uint32_t count) {
  uint32_t element = 16;
  switch (type) {
    case FragmentType::Float:
    case FragmentType::Int:
      element = 4;
      break;
    case FragmentType::Float2:
      element = 8;
      break;
    case FragmentType::Float3:
    case FragmentType::Float4:
    case FragmentType::Int4:
      element = 16;
      break;
    case FragmentType::Matrix3:
      element = 48;
      break;
    case FragmentType::Matrix4:
      element = 64;
      break;
  }
  const uint32_t stride = element < 16 ? 16 : element;
  return count <= 1 ? element : count * stride;
}

// The operator id behind a function name (the same table the build tool compiles the modules
// from).
int FindOpOfFunction(const std::string& name) {
  const size_t count = FragmentCount();
  for (size_t index = 0; index < count; ++index) {
    const auto* fragment = FragmentAt(index);
    if (fragment != nullptr && fragment->functionName != nullptr &&
        name == fragment->functionName) {
      return fragment->op;
    }
  }
  return -1;
}

bool IsIntegerLiteral(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  size_t start = text[0] == '-' ? 1 : 0;
  if (start == text.size()) {
    return false;
  }
  for (size_t index = start; index < text.size(); ++index) {
    if (text[index] < '0' || text[index] > '9') {
      return false;
    }
  }
  return true;
}

}  // namespace

VulkanProgramBuilder::VulkanProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
  // The bytecode route never spells shader text: the calls land in the record list instead.
  _fragBuilder.setCallRecorder(&fragmentCalls);
}

std::string VulkanProgramBuilder::getShaderVarDeclarations(const ShaderVar&, ShaderStage) const {
  return "";
}

std::string VulkanProgramBuilder::getUniformBlockDeclaration(
    ShaderStage, const std::vector<Uniform>&) const {
  return "";
}

bool VulkanProgramBuilder::checkSamplerCounts() {
  return true;
}

std::shared_ptr<Program> VulkanProgramBuilder::finalize() {
  ChainProgramPlan plan = {};
  // The declared members, in the handler's declaration order; the std140 offsets are derived
  // here because this route owns the only uniform block the shader declares.
  uint32_t offset = 0;
  std::map<std::string, int> uniformIndex = {};
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Fragment)) {
    ChainUniformMember member = {};
    member.name = uniform.name();
    member.type = FragmentTypeOf(uniform.format());
    member.count = static_cast<uint16_t>(uniform.count());
    member.offset = offset;
    member.size = Std140Size(member.type, member.count);
    offset += member.size;
    uniformIndex[member.name] = static_cast<int>(plan.uniforms.size());
    plan.uniforms.push_back(std::move(member));
  }
  const auto& samplers = _uniformHandler.getSamplers();
  for (size_t index = 0; index < samplers.size(); ++index) {
    ChainSamplerBinding binding = {};
    binding.name = samplers[index].name();
    binding.source = ChainSamplerSource::Leaf;
    binding.index = static_cast<int>(index);
    plan.samplers.push_back(std::move(binding));
  }
  for (const auto& varying : _varyingHandler.getVaryings()) {
    ChainVaryingDecl decl = {};
    decl.name = varying.name();
    decl.type = FragmentTypeOf(varying.type());
    plan.varyings.push_back(std::move(decl));
  }
  // The recorded calls in emission order; every argument must resolve to a register, a uniform
  // member, or an integer literal, otherwise the shape stays on its runtime path.
  std::map<std::string, int> registerOf = {};
  int nextRegister = 0;
  for (const auto& record : fragmentCalls) {
    const int op = FindOpOfFunction(record.function);
    if (op < 0) {
      LOGE("VulkanProgramBuilder: no operator for %s", record.function.c_str());
      return nullptr;
    }
    ChainCall call = {};
    call.op = op;
    for (const auto& arg : record.args) {
      ChainArg chainArg = {};
      const auto member = uniformIndex.find(arg);
      if (member != uniformIndex.end()) {
        chainArg.kind = ChainArgKind::Uniform;
        chainArg.index = member->second;
      } else {
        const auto reg = registerOf.find(arg);
        if (reg != registerOf.end()) {
          chainArg.kind = ChainArgKind::Register;
          chainArg.index = reg->second;
        } else if (IsIntegerLiteral(arg)) {
          chainArg.kind = ChainArgKind::Literal;
          chainArg.index = std::atoi(arg.c_str());
        } else {
          LOGE("VulkanProgramBuilder: the argument %s of %s does not resolve",
               arg.c_str(), record.function.c_str());
          return nullptr;
        }
      }
      call.args.push_back(chainArg);
    }
    call.outRegister = nextRegister++;
    registerOf[record.output] = call.outRegister;
    plan.calls.push_back(std::move(call));
  }
  // The body emitter and the module link land here next.
  LOGE("VulkanProgramBuilder: the SPIR-V finalize is not wired yet");
  return nullptr;
}
}  // namespace tgfx
