////////////////////////////////////////////////////////////////////////////////////////////////
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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "MetalProgramBuilder.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/utils/Log.h"
#include "gpu/ProgramWriter.h"
#include "gpu/ir/ProgramValidation.h"
#include "gpu/metal/MSLPrinter.h"
#include "gpu/shaders/ShaderLibraryBundle.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

std::shared_ptr<Program> MetalProgramBuilder::Create(Context* context,
                                                     const ProgramInfo* programInfo) {
  MetalProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}

MetalProgramBuilder::MetalProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
  _vertexBuilder.setVertexRecorder(&description.vertexCalls);
  _vertexBuilder.setValueTable(&description.values);
  _fragBuilder.setFragmentRecorder(&description.fragment);
  _fragBuilder.setValueTable(&description.values);
  _vertexBuilder.setLibraryLanguage(ShaderLibraryLanguage::MSL);
  _fragBuilder.setLibraryLanguage(ShaderLibraryLanguage::MSL);
}

std::string MetalProgramBuilder::getShaderVarDeclarations(const ShaderVar&, ShaderStage) const {
  return "";
}

std::string MetalProgramBuilder::getUniformBlockDeclaration(ShaderStage,
                                                            const std::vector<Uniform>&) const {
  return "";
}

std::string MetalProgramBuilder::getUniformBlockDeclaration(ShaderStage,
                                                            const std::vector<UniformDesc>&) const {
  return "";
}

bool MetalProgramBuilder::checkSamplerCounts() {
  auto shaderCaps = context->shaderCaps();
  if (numFragmentSamplers > shaderCaps->maxFragmentSamplers) {
    LOGE("Program would use too many fragment samplers.");
    return false;
  }
  return true;
}

std::shared_ptr<Program> MetalProgramBuilder::finalize() {
  fragmentShaderBuilder()->declareCustomOutputColor();
  for (const auto& attribute : getProgramInfo()->getVertexAttributes()) {
    const auto type = ShaderVar(attribute).type();
    description.interface.attributes.push_back({attribute.name(), type});
    description.values.Intern(attribute.name(), type, ValueOrigin::Attribute);
  }
  for (const auto& attribute : getProgramInfo()->getInstanceAttributes()) {
    const auto type = ShaderVar(attribute).type();
    description.interface.attributes.push_back({attribute.name(), type});
    description.values.Intern(attribute.name(), type, ValueOrigin::Attribute);
  }
  for (const auto& varying : _varyingHandler.getVaryings()) {
    description.interface.varyings.push_back(
        {varying.name(), varying.type(), varying.id(), 0, false, varying.isFlat()});
    description.values.Intern(varying.name(), varying.type(), ValueOrigin::Varying);
  }
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Vertex)) {
    description.interface.uniforms.push_back(
        {uniform.name(), ShaderVar(uniform).type(), ShaderStage::Vertex, uniform.count()});
    description.values.Intern(uniform.name(), ShaderVar(uniform).type(), ValueOrigin::Uniform);
  }
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Fragment)) {
    description.interface.uniforms.push_back(
        {uniform.name(), ShaderVar(uniform).type(), ShaderStage::Fragment, uniform.count()});
    description.values.Intern(uniform.name(), ShaderVar(uniform).type(), ValueOrigin::Uniform);
  }
  for (const auto& sampler : _uniformHandler.getSamplers()) {
    description.interface.samplers.push_back({sampler.name(), ShaderVar(sampler).type()});
    description.values.Intern(sampler.name(), ShaderVar(sampler).type(), ValueOrigin::Sampler);
  }
  finalizeShaders();
  std::string validationError;
  if (!ValidateProgramDescription(&description, &validationError)) {
    LOGE("MetalProgramBuilder: %s", validationError.c_str());
    return nullptr;
  }
  MslCallSite callSite = {};
  for (const auto& sampler : description.interface.samplers) {
    callSite.samplerNames.insert(sampler.name);
  }
  std::set<std::string> listEmitted = {};
  std::string samplerList = {};
  ShaderLibraryBundle::Get().appendFunction(ShaderLibraryLanguage::MSL, "__msl_samplers__",
                                            &listEmitted, &samplerList);
  size_t listStart = 0;
  while (listStart <= samplerList.size()) {
    const size_t comma = samplerList.find(',', listStart);
    const std::string name = samplerList.substr(
        listStart, comma == std::string::npos ? std::string::npos : comma - listStart);
    if (!name.empty()) {
      callSite.samplerFunctions.insert(name);
    }
    if (comma == std::string::npos) {
      break;
    }
    listStart = comma + 1;
  }
  _vertexBuilder.setCodeText(MSLPrinter::PrintVertexProgram(description.vertexCalls, callSite));
  _fragBuilder.setCodeText(MSLPrinter::PrintFragmentProgram(description.fragment, callSite));
  const auto vertexText = MSLPrinter::PrintStage(*this, ShaderStage::Vertex, description.interface,
                                                 _vertexBuilder, callSite);
  const auto fragmentText = MSLPrinter::PrintStage(*this, ShaderStage::Fragment,
                                                   description.interface, _fragBuilder, callSite);
  if (const char* dumpDir = std::getenv("TGFX_DUMP_PROGRAM")) {
    static std::atomic<int> dumpIndex{0};
    const int index = dumpIndex.fetch_add(1);
    const std::string base = std::string(dumpDir) + "/msl_program_" + std::to_string(index);
    if (auto* file = fopen((base + ".vert").c_str(), "wb")) {
      fwrite(vertexText.data(), 1, vertexText.size(), file);
      fclose(file);
    }
    if (auto* file = fopen((base + ".frag").c_str(), "wb")) {
      fwrite(fragmentText.data(), 1, fragmentText.size(), file);
      fclose(file);
    }
  }
  auto gpu = context->gpu();
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = vertexText;
  vertexModule.stage = ShaderStage::Vertex;
  vertexModule.format = ShaderCodeFormat::MSL;
  if (!_uniformHandler.getUniforms(ShaderStage::Vertex).empty()) {
    vertexModule.uniformSlots[VertexUniformBlockName] = 0;
  }
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = fragmentText;
  fragmentModule.stage = ShaderStage::Fragment;
  fragmentModule.format = ShaderCodeFormat::MSL;
  if (!_uniformHandler.getUniforms(ShaderStage::Fragment).empty()) {
    fragmentModule.uniformSlots[FragmentUniformBlockName] = 0;
  }
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  VertexBufferLayout vertexLayout(programInfo->getVertexAttributes());
  descriptor.vertex.bufferLayouts = {vertexLayout};
  auto& instanceAttributes = programInfo->getInstanceAttributes();
  if (!instanceAttributes.empty()) {
    VertexBufferLayout instanceLayout(instanceAttributes, VertexStepMode::Instance);
    descriptor.vertex.bufferLayouts.push_back(instanceLayout);
  }
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());
  auto vertexUniformData = _uniformHandler.makeUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = _uniformHandler.makeUniformData(ShaderStage::Fragment);
  if (vertexUniformData) {
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT,
                                  ShaderVisibility::Vertex};
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (fragmentUniformData) {
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT,
                                    ShaderVisibility::Fragment};
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  int textureBinding = 0;
  for (const auto& sampler : _uniformHandler.getSamplers()) {
    descriptor.layout.textureSamplers.emplace_back(sampler.name(), textureBinding++);
  }
  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  descriptor.multisample.count = programInfo->getSampleCount();
  descriptor.depthStencil = programInfo->getDepthStencil();
  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline == nullptr) {
    return nullptr;
  }
  return std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                   std::move(fragmentUniformData), nullptr);
}

}  // namespace tgfx
