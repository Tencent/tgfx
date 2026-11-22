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
#include <cstring>
#include <map>
#include "VulkanEntryEmit.h"
#include "core/utils/Log.h"
#include "gpu/ContextInternal.h"
#include "gpu/Program.h"
#include "gpu/ProgramWriter.h"
#include "gpu/ShaderCaps.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/ChainProgramPlan.h"
#include "gpu/aot/chain/ChainSPIRVBody.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/gpu/GPU.h"

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

// The declared type one vertex input reads as: the normalized and half formats feed the float
// forms, so the emitted stage and the buffer layout agree about the component type.
UniformFormat UniformFormatOf(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
    case VertexFormat::Half:
    case VertexFormat::UByteNormalized:
      return UniformFormat::Float;
    case VertexFormat::Float2:
    case VertexFormat::Half2:
    case VertexFormat::UByte2Normalized:
      return UniformFormat::Float2;
    case VertexFormat::Float3:
    case VertexFormat::Half3:
    case VertexFormat::UByte3Normalized:
      return UniformFormat::Float3;
    case VertexFormat::Float4:
    case VertexFormat::Half4:
    case VertexFormat::UByte4Normalized:
      return UniformFormat::Float4;
    case VertexFormat::Int:
      return UniformFormat::Int;
    case VertexFormat::Int2:
      return UniformFormat::Int2;
    case VertexFormat::Int3:
      return UniformFormat::Int3;
    case VertexFormat::Int4:
      return UniformFormat::Int4;
  }
  return UniformFormat::Float4;
}

ShaderType ShaderTypeOf(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
      return ShaderType::Float;
    case FragmentType::Float2:
      return ShaderType::Float2;
    case FragmentType::Float3:
      return ShaderType::Float3;
    case FragmentType::Float4:
      return ShaderType::Float4;
    case FragmentType::Int:
      return ShaderType::Int;
    case FragmentType::Int4:
      return ShaderType::Int4;
    case FragmentType::Matrix3:
      return ShaderType::Mat3;
    case FragmentType::Matrix4:
      return ShaderType::Mat4;
  }
  return ShaderType::Float4;
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
  _fragBuilder.setSourceRecorder(&fragmentSources);
  _vertexBuilder.setVertexRecorder(&vertexCalls);
}

std::string VulkanProgramBuilder::getShaderVarDeclarations(const ShaderVar&, ShaderStage) const {
  return "";
}

std::string VulkanProgramBuilder::getUniformBlockDeclaration(ShaderStage,
                                                             const std::vector<Uniform>&) const {
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
    member.source = ChainBindingSource::Unbound;
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
  // The stage-output sources the geometry and the xfer recorded: the composition's own inputs and
  // the plain formula that closes it. A value the plan cannot name as a declared member, a
  // varying, or a constant leaves the draw on its runtime path.
  auto argOfValue = [&](const std::string& rawName, ChainArg* arg) -> bool {
    std::string name = rawName;
    const auto dot = name.rfind('.');
    if (dot != std::string::npos && name.compare(dot + 1, std::string::npos, "xy") == 0) {
      // The geometries hoist a vec2's own components (`v.xy`); the value is the varying itself.
      name.resize(dot);
    }
    const auto member = uniformIndex.find(name);
    if (member != uniformIndex.end()) {
      arg->kind = ChainArgKind::Uniform;
      arg->index = member->second;
      return true;
    }
    for (size_t index = 0; index < plan.varyings.size(); ++index) {
      if (plan.varyings[index].name == name) {
        arg->kind = ChainArgKind::Varying;
        arg->index = static_cast<int>(index);
        return true;
      }
    }
    for (size_t index = 0; index < plan.samplers.size(); ++index) {
      if (plan.samplers[index].name == name) {
        arg->kind = ChainArgKind::Sampler;
        arg->index = static_cast<int>(index);
        return true;
      }
    }
    if (name == "vec4(1.0)" || name == "1.0") {
      arg->kind = ChainArgKind::White;
      return true;
    }
    if (name == "vec4(0.0)") {
      arg->kind = ChainArgKind::Black;
      return true;
    }
    if (IsIntegerLiteral(name)) {
      arg->kind = ChainArgKind::Literal;
      arg->index = std::atoi(name.c_str());
      return true;
    }
    return false;
  };
  // Every record the stage emitted, in emission order: the aliases and the stage outputs are
  // resolved in the same pass as the calls, because an alias always names a value produced
  // earlier in the emission.
  struct EmitEvent {
    uint32_t order = 0;
    bool call = false;
    size_t index = 0;
  };
  std::vector<EmitEvent> events = {};
  for (size_t index = 0; index < fragmentCalls.size(); ++index) {
    events.push_back({fragmentCalls[index].order, true, index});
  }
  for (size_t index = 0; index < fragmentSources.size(); ++index) {
    events.push_back({fragmentSources[index].order, false, index});
  }
  std::sort(events.begin(), events.end(),
            [](const EmitEvent& left, const EmitEvent& right) { return left.order < right.order; });
  std::map<std::string, ChainArg> sourceOf = {};
  std::map<std::string, int> registerOf = {};
  int nextRegister = 0;
  // A value by name: a register an earlier record wrote, or a declared source.
  auto resolveOutput = [&](const std::string& name, ChainArg* source, int* reg) -> bool {
    *reg = -1;
    *source = ChainArg::None();
    const auto written = registerOf.find(name);
    if (written != registerOf.end()) {
      *reg = written->second;
      return true;
    }
    const auto produced = sourceOf.find(name);
    if (produced != sourceOf.end()) {
      *source = produced->second;
      return true;
    }
    return false;
  };
  auto resolveArg = [&](const std::string& name, ChainArg* arg) -> bool {
    if (argOfValue(name, arg)) {
      return true;
    }
    int reg = -1;
    ChainArg source = ChainArg::None();
    if (!resolveOutput(name, &source, &reg)) {
      return false;
    }
    if (reg >= 0) {
      arg->kind = ChainArgKind::Register;
      arg->index = reg;
    } else {
      *arg = source;
    }
    return true;
  };
  plan.positionalArgs = true;
  plan.rootRegister = -1;
  plan.coverageRootRegister = -1;
  bool combined = false;
  for (const auto& event : events) {
    if (event.call) {
      const auto& record = fragmentCalls[event.index];
      const int op = FindOpOfFunction(record.function);
      if (op < 0) {
        LOGE("VulkanProgramBuilder: no operator for %s", record.function.c_str());
        return nullptr;
      }
      ChainCall call = {};
      call.op = op;
      for (const auto& arg : record.args) {
        ChainArg chainArg = ChainArg::None();
        if (!resolveArg(arg, &chainArg)) {
          LOGE("VulkanProgramBuilder: the argument %s of %s does not resolve", arg.c_str(),
               record.function.c_str());
          return nullptr;
        }
        call.args.push_back(chainArg);
      }
      call.outRegister = nextRegister++;
      registerOf[record.output] = call.outRegister;
      plan.calls.push_back(std::move(call));
      continue;
    }
    const auto& source = fragmentSources[event.index];
    if (source.kind == FragmentSourceRecord::Kind::Combine) {
      ChainArg colorSource = ChainArg::None();
      int colorRegister = -1;
      ChainArg coverageSource = ChainArg::None();
      int coverageRegister = -1;
      if (!resolveOutput(source.src, &colorSource, &colorRegister) ||
          !resolveOutput(source.operand, &coverageSource, &coverageRegister)) {
        LOGE("VulkanProgramBuilder: the output formula reads %s and %s, which the plan cannot name",
             source.src.c_str(), source.operand.c_str());
        return nullptr;
      }
      plan.rootRegister = colorRegister;
      if (colorRegister < 0) {
        plan.geomColor = colorSource;
      }
      plan.coverageRootRegister = coverageRegister;
      if (coverageRegister < 0) {
        plan.geomCoverage = coverageSource;
      }
      combined = true;
      continue;
    }
    if (source.kind == FragmentSourceRecord::Kind::Alias) {
      ChainArg arg = ChainArg::None();
      int reg = -1;
      if (!resolveOutput(source.src, &arg, &reg)) {
        LOGE("VulkanProgramBuilder: the alias %s reads %s, which the plan cannot name",
             source.dst.c_str(), source.src.c_str());
        return nullptr;
      }
      if (reg >= 0) {
        registerOf[source.dst] = reg;
      } else {
        sourceOf[source.dst] = arg;
      }
      continue;
    }
    ChainArg arg = ChainArg::None();
    if (!resolveArg(source.src, &arg)) {
      LOGE("VulkanProgramBuilder: the stage output %s reads %s, which the plan cannot name",
           source.dst.c_str(), source.src.c_str());
      return nullptr;
    }
    if (source.kind == FragmentSourceRecord::Kind::Color && arg.kind != ChainArgKind::Register) {
      plan.geomColor = arg;
    }
    sourceOf[source.dst] = arg;
  }
  for (const auto& call : plan.calls) {
    plan.usedOps.insert(call.op);
  }
  if (!combined) {
    LOGE("VulkanProgramBuilder: the xfer stage recorded no output formula");
    return nullptr;
  }

  // The program's interface: the geometry's attributes (vertex block first, then the instance
  // block), the plan's varyings and samplers. Every location and binding is numbered here once,
  // so the two emitted stages, the pipeline and the upload all read the same numbers.
  Layout layout = {};
  uint8_t location = 0;
  const auto& vertexAttributes = programInfo->getVertexAttributes();
  for (const auto& attribute : vertexAttributes) {
    Layout::Attribute entry = {};
    entry.name = attribute.name();
    entry.format = UniformFormatOf(attribute.format());
    entry.location = location++;
    layout.attributes.push_back(std::move(entry));
  }
  const auto& instanceAttributes = programInfo->getInstanceAttributes();
  for (const auto& attribute : instanceAttributes) {
    Layout::Attribute entry = {};
    entry.name = attribute.name();
    entry.format = UniformFormatOf(attribute.format());
    entry.location = location++;
    entry.instance = true;
    layout.attributes.push_back(std::move(entry));
  }
  for (size_t index = 0; index < plan.varyings.size(); ++index) {
    Layout::Varying varying = {};
    varying.name = plan.varyings[index].name;
    varying.type = ShaderTypeOf(plan.varyings[index].type);
    varying.location = static_cast<uint8_t>(index);
    layout.varyings.push_back(std::move(varying));
  }
  for (size_t index = 0; index < plan.samplers.size(); ++index) {
    Layout::Texture texture = {};
    texture.name = plan.samplers[index].name;
    texture.binding = static_cast<uint8_t>(index);
    layout.textures.push_back(std::move(texture));
  }

  if (plan.samplers.size() > 4) {
    LOGE("VulkanProgramBuilder: the plan declares %zu samplers, the body carries four",
         plan.samplers.size());
    return nullptr;
  }
  const int hasXp = plan.hasXp;
  if (!ChainSPIRVBodySupported(plan.usedOps, layout, hasXp)) {
    LOGE("VulkanProgramBuilder: the planned operators have no linkable modules");
    return nullptr;
  }
  std::string emitError;
  std::vector<uint32_t> body = {};
  if (!EmitChainBodySPIRV(plan, layout, &body, &emitError)) {
    LOGE("VulkanProgramBuilder: the body emission failed: %s", emitError.c_str());
    return nullptr;
  }
  std::vector<uint32_t> frame = {};
  if (!EmitVulkanFragmentFrame(plan, layout, &frame, &emitError)) {
    LOGE("VulkanProgramBuilder: the frame emission failed: %s", emitError.c_str());
    return nullptr;
  }
  const auto& vertexUniforms = _uniformHandler.getUniforms(ShaderStage::Vertex);
  std::vector<uint32_t> vertex = {};
  if (!EmitVulkanVertexBody(vertexCalls, layout, vertexUniforms, &vertex, &emitError)) {
    LOGE("VulkanProgramBuilder: the vertex emission failed: %s", emitError.c_str());
    return nullptr;
  }

  // The operators' precompiled modules, plus the pseudo operators the body imports on demand.
  auto* cache = ContextCacheAccess::Cache(context);
  if (cache == nullptr) {
    LOGE("VulkanProgramBuilder: the loaded bundle is unavailable");
    return nullptr;
  }
  std::vector<std::vector<uint32_t>> modules = {std::move(frame), std::move(body)};
  std::vector<uint32_t> moduleOps = {};
  bool wantsLeafFetch = false;
  for (int usedOp : plan.usedOps) {
    moduleOps.push_back(static_cast<uint32_t>(usedOp));
    if (usedOp == ChainISA::Texture || usedOp == ChainISA::TexModulate) {
      wantsLeafFetch = true;
    }
  }
  if (wantsLeafFetch) {
    moduleOps.push_back(FragmentPackSPIRVLeafFetch & ~FragmentPackSPIRVOpBase);
  }
  if (hasXp == 1) {
    moduleOps.push_back(FragmentPackSPIRVPortDuff & ~FragmentPackSPIRVOpBase);
  }
  for (uint32_t op : moduleOps) {
    if (op == ChainISA::InputOpaque || op == ChainISA::MulAlpha) {
      continue;  // the inline expressions need no module
    }
    const auto* entry = cache->findComposed(FragmentPackSPIRVOpBase | op);
    if (entry == nullptr) {
      LOGE("VulkanProgramBuilder: no SPIR-V module for op %u", op);
      return nullptr;
    }
    std::vector<uint32_t> words(entry->data.size() / sizeof(uint32_t));
    std::memcpy(words.data(), entry->data.data(), words.size() * sizeof(uint32_t));
    modules.push_back(std::move(words));
  }
  std::vector<uint32_t> linked = {};
  std::string linkError;
  if (!LinkChainSPIRV(modules, &linked, &linkError)) {
    LOGE("VulkanProgramBuilder: the link failed: %s", linkError.c_str());
    return nullptr;
  }
  if (!ValidateSPIRVModule(linked, &linkError)) {
    LOGE("VulkanProgramBuilder: the linked module failed validation: %s", linkError.c_str());
    return nullptr;
  }

  auto* gpu = context->gpu();
  auto* linkedBytes = reinterpret_cast<const uint8_t*>(linked.data());
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.format = ShaderCodeFormat::SPIRV;
  fragmentModule.stage = ShaderStage::Fragment;
  fragmentModule.binaryData.assign(linkedBytes, linkedBytes + linked.size() * sizeof(uint32_t));
  if (!plan.uniforms.empty()) {
    fragmentModule.uniformSlots = {
        {std::string(FragmentUniformBlockName), static_cast<unsigned>(FRAGMENT_UBO_BINDING_POINT)}};
  }
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.format = ShaderCodeFormat::SPIRV;
  vertexModule.stage = ShaderStage::Vertex;
  const auto* vertexBytes = reinterpret_cast<const uint8_t*>(vertex.data());
  vertexModule.binaryData.assign(vertexBytes, vertexBytes + vertex.size() * sizeof(uint32_t));
  if (!vertexUniforms.empty()) {
    vertexModule.uniformSlots = {
        {std::string(VertexUniformBlockName), static_cast<unsigned>(VERTEX_UBO_BINDING_POINT)}};
  }
  auto vertexShader = gpu->createShaderModule(vertexModule);
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (vertexShader == nullptr || fragmentShader == nullptr) {
    LOGE("VulkanProgramBuilder: the shader modules could not be created");
    return nullptr;
  }

  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(vertexAttributes);
  for (size_t index = 0; index < vertexAttributes.size(); ++index) {
    vertexLayout.shaderLocations.push_back(static_cast<uint32_t>(index));
  }
  descriptor.vertex.bindAttributesByLocation = true;
  descriptor.vertex.bufferLayouts = {vertexLayout};
  if (!instanceAttributes.empty()) {
    VertexBufferLayout instanceLayout(instanceAttributes, VertexStepMode::Instance);
    for (size_t index = 0; index < instanceAttributes.size(); ++index) {
      instanceLayout.shaderLocations.push_back(
          static_cast<uint32_t>(vertexAttributes.size() + index));
    }
    descriptor.vertex.bufferLayouts.push_back(instanceLayout);
  }
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());
  auto vertexUniformData = _uniformHandler.makeUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = _uniformHandler.makeUniformData(ShaderStage::Fragment);
  if (vertexUniformData != nullptr) {
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (fragmentUniformData != nullptr) {
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  for (size_t index = 0; index < plan.samplers.size(); ++index) {
    descriptor.layout.textureSamplers.emplace_back(plan.samplers[index].name,
                                                   static_cast<unsigned>(index));
  }
  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();
  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline == nullptr) {
    LOGE("VulkanProgramBuilder: the render pipeline could not be created");
    return nullptr;
  }
  LOGI("VulkanProgramBuilder: built a program (%zu calls, %zu varyings, %zu samplers)",
       plan.calls.size(), plan.varyings.size(), plan.samplers.size());
  return std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                   std::move(fragmentUniformData), nullptr);
}
std::shared_ptr<Program> VulkanProgramBuilder::Create(Context* context,
                                                      const ProgramInfo* programInfo) {
  VulkanProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}
}  // namespace tgfx
