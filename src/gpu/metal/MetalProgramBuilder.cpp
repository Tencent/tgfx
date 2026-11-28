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
#include "gpu/ProgramAssembler.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/utils/Log.h"
#include "gpu/ir/ProgramBodyPrinter.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/metal/MSLPrinter.h"
#include "gpu/shaders/ShaderLibraryBundle.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
namespace {

bool IRReadsStageColor(const ProgramIR& program) {
  for (const auto& expr : program.exprs) {
    for (const auto arg : expr.args) {
      if (arg < program.values.size() &&
          program.values[arg].debugName == CUSTOM_COLOR_OUTPUT_NAME) {
        return true;
      }
    }
  }
  for (const auto& statement : program.statements) {
    if (statement.src < program.values.size() &&
        program.values[statement.src].debugName == CUSTOM_COLOR_OUTPUT_NAME) {
      return true;
    }
  }
  return false;
}

}  // namespace

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
  ProgramAssembler assembler(this);
  std::string vertexBody = {};
  std::string fragmentBody = {};
  std::string libraryText = {};
  if (!assembler.assemble(MSL_Language, &vertexBody, &fragmentBody, &libraryText)) {
    return nullptr;
  }
  bool writesPosition = false;
  for (const auto& statement : programIRBuilder().IR().vertexStatements) {
    if (statement.kind == IRStatementKind::StoreOutput &&
        statement.dst.base < programIRBuilder().IR().values.size() &&
        programIRBuilder().IR().values[statement.dst.base].debugName == "gl_Position") {
      writesPosition = true;
      break;
    }
  }
  if (!writesPosition) {
    LOGE("MetalProgramBuilder: the vertex stage never writes the clip-space position");
    return nullptr;
  }
  _vertexBuilder.setCodeText(vertexBody);
  _fragBuilder.setCodeText(fragmentBody);
  const bool readsDestinationColor = IRReadsStageColor(programIRBuilder().IR());

  MslCallSite callSite = {};
  for (const auto& sampler : programIRBuilder().IR().interface.samplers) {
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
  const auto vertexText = MSLPrinter::PrintStage(*this, ShaderStage::Vertex, programIRBuilder().IR().interface,
                                                 _vertexBuilder, callSite);
  const auto fragmentText = MSLPrinter::PrintStage(*this, ShaderStage::Fragment,
                                                   programIRBuilder().IR().interface, _fragBuilder,
                                                   callSite,
                                                   readsDestinationColor);
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
                                   std::move(fragmentUniformData));
}

}  // namespace tgfx
