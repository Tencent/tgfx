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
#include <cstdio>
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
#include "gpu/shaders/contract/OpTable.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/spirv/SPIRVProgramBuilder.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
namespace {

OperatorBundle BuildOperatorBundle(Context* context) {
  std::vector<OperatorArtifact> artifacts = {};
  auto* cache = ContextCacheAccess::Cache(context);
  if (cache == nullptr) {
    return OperatorBundle(std::move(artifacts));
  }
  for (uint32_t id = 1; id <= 12; ++id) {
    const auto* desc = FindOperator(static_cast<OperatorID>(id));
    if (desc == nullptr || desc->glslSymbol == nullptr) {
      continue;
    }
    const auto* blob = cache->findComposed(FragmentPackSPIRVOpBase | id);
    if (blob == nullptr || blob->data.size() % sizeof(uint32_t) != 0) {
      continue;
    }
    OperatorArtifact artifact = {};
    artifact.id = static_cast<OperatorID>(id);
    artifact.backend = OperatorBackend::SPIRV;
    artifact.symbol = desc->glslSymbol;
    const auto* words = reinterpret_cast<const uint32_t*>(blob->data.data());
    artifact.spirv.assign(words, words + blob->data.size() / sizeof(uint32_t));
    artifacts.push_back(std::move(artifact));
  }
  return OperatorBundle(std::move(artifacts));
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

std::shared_ptr<Program> VulkanProgramBuilder::buildIRProgram() {
  const auto& program = programIRBuilder().IR();
  if (program.vertexStatements.empty() || program.statements.empty()) {
    return nullptr;
  }
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Vertex)) {
    const auto type = ShaderVar(uniform).type();
    if (uniform.count() > 1) {
      programIRBuilder().AddUniformArray(
          {uniform.name(), type, Precision::Default, ShaderStage::Vertex, uniform.count()});
    } else {
      programIRBuilder().AddUniform(
          {uniform.name(), type, Precision::Default, ShaderStage::Vertex, uniform.count()});
    }
  }
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Fragment)) {
    const auto type = ShaderVar(uniform).type();
    if (uniform.count() > 1) {
      programIRBuilder().AddUniformArray(
          {uniform.name(), type, Precision::Default, ShaderStage::Fragment, uniform.count()});
    } else {
      programIRBuilder().AddUniform(
          {uniform.name(), type, Precision::Default, ShaderStage::Fragment, uniform.count()});
    }
  }
  for (const auto& sampler : _uniformHandler.getSamplers()) {
    programIRBuilder().AddSampler({sampler.name(), ShaderVar(sampler).type(), 0});
  }
  for (const auto& varying : _varyingHandler.getVaryings()) {
    programIRBuilder().AddVarying(
        {varying.name(), varying.type(), Precision::High, 0, varying.isFlat()});
  }

  SPIRVProgramModules modules = {};
  std::string error = {};
  const auto operatorBundle = BuildOperatorBundle(context);
  if (!SPIRVProgramBuilder::Build(programIRBuilder().IR(), &modules, &error, &operatorBundle)) {
    LOGE("VulkanProgramBuilder: the SPIR-V IR route failed: %s", error.c_str());
    return nullptr;
  }
  auto* gpu = context->gpu();
  auto toBytes = [](const std::vector<uint32_t>& words) {
    std::vector<uint8_t> bytes(words.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), words.data(), bytes.size());
    return bytes;
  };
  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = ShaderCodeFormat::SPIRV;
  vertexDesc.stage = ShaderStage::Vertex;
  vertexDesc.binaryData = toBytes(modules.vertex);
  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = ShaderCodeFormat::SPIRV;
  fragmentDesc.stage = ShaderStage::Fragment;
  fragmentDesc.binaryData = toBytes(modules.fragment);
  for (const auto& binding : modules.layout.bindings()) {
    if (binding.type != SLType::Void) {
      continue;
    }
    if (binding.stage == ShaderStage::Vertex) {
      vertexDesc.uniformSlots[binding.name] = binding.binding;
    } else {
      fragmentDesc.uniformSlots[binding.name] = binding.binding;
    }
  }
  auto vertexShader = gpu->createShaderModule(vertexDesc);
  auto fragmentShader = gpu->createShaderModule(fragmentDesc);
  if (vertexShader == nullptr || fragmentShader == nullptr) {
    LOGE("VulkanProgramBuilder: the IR shader modules could not be created");
    return nullptr;
  }

  BindingLayout bindingLayout = {};
  if (!modules.layout.BuildBindingLayout(&bindingLayout, &error)) {
    LOGE("VulkanProgramBuilder: %s", error.c_str());
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  const auto& attributes = programInfo->getVertexAttributes();
  VertexBufferLayout vertexLayout(attributes);
  for (size_t index = 0; index < attributes.size(); ++index) {
    vertexLayout.shaderLocations.push_back(static_cast<uint32_t>(index));
  }
  descriptor.vertex.bindAttributesByLocation = true;
  descriptor.vertex.bufferLayouts = {vertexLayout};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());
  descriptor.layout = bindingLayout;
  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();
  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline == nullptr) {
    LOGE("VulkanProgramBuilder: the IR render pipeline could not be created");
    return nullptr;
  }
  auto vertexUniformData = _uniformHandler.makeUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = _uniformHandler.makeUniformData(ShaderStage::Fragment);
  LOGI("VulkanProgramBuilder: built an IR program (%zu bindings)",
       modules.layout.bindings().size());
  return std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                   std::move(fragmentUniformData), nullptr);
}

std::shared_ptr<Program> VulkanProgramBuilder::finalize() {
  return buildIRProgram();
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
