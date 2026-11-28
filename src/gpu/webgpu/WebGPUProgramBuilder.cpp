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
//  license is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "WebGPUProgramBuilder.h"
#include <string>
#include "WGSLPrinter.h"
#include "core/utils/Log.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/ShaderVar.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/ir/OperatorManifest.h"
#include "gpu/ir/ProgramBodyPrinter.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gpu/shaders/ShaderLibraryBundle.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
namespace {

bool IRWritesPosition(const ProgramIR& program) {
  for (const auto& statement : program.vertexStatements) {
    if (statement.kind == IRStatementKind::StoreOutput &&
        statement.dst.base < program.values.size() &&
        program.values[statement.dst.base].debugName == "gl_Position") {
      return true;
    }
  }
  return false;
}

}  // namespace

std::shared_ptr<Program> WebGPUProgramBuilder::Create(Context* context,
                                                     const ProgramInfo* programInfo) {
  WebGPUProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}

WebGPUProgramBuilder::WebGPUProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
  _vertexBuilder.setLibraryLanguage(ShaderLibraryLanguage::WGSL);
  _fragBuilder.setLibraryLanguage(ShaderLibraryLanguage::WGSL);
}

std::string WebGPUProgramBuilder::getShaderVarDeclarations(const ShaderVar&, ShaderStage) const {
  return "";
}

std::string WebGPUProgramBuilder::getUniformBlockDeclaration(
    ShaderStage, const std::vector<Uniform>&) const {
  return "";
}

std::string WebGPUProgramBuilder::getUniformBlockDeclaration(
    ShaderStage, const std::vector<UniformDesc>&) const {
  return "";
}

bool WebGPUProgramBuilder::checkSamplerCounts() {
  auto shaderCaps = context->shaderCaps();
  if (numFragmentSamplers > shaderCaps->maxFragmentSamplers) {
    LOGE("Program would use too many fragment samplers.");
    return false;
  }
  return true;
}

std::shared_ptr<Program> WebGPUProgramBuilder::finalize() {
  fragmentShaderBuilder()->declareCustomOutputColor();
  for (const auto& attribute : getProgramInfo()->getVertexAttributes()) {
    programIRBuilder().AddAttribute(
        {attribute.name(), ShaderVar(attribute).type(), Precision::High, 0});
  }
  for (const auto& attribute : getProgramInfo()->getInstanceAttributes()) {
    programIRBuilder().AddAttribute(
        {attribute.name(), ShaderVar(attribute).type(), Precision::High, 0});
  }
  for (const auto& varying : _varyingHandler.getVaryings()) {
    programIRBuilder().AddVarying(
        {varying.name(), varying.type(), Precision::High, 0, varying.isFlat()});
  }
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Vertex)) {
    programIRBuilder().AddUniform({uniform.name(), ShaderVar(uniform).type(), Precision::Default,
                                   ShaderStage::Vertex, uniform.count()});
  }
  for (const auto& uniform : _uniformHandler.getUniforms(ShaderStage::Fragment)) {
    programIRBuilder().AddUniform({uniform.name(), ShaderVar(uniform).type(), Precision::Default,
                                   ShaderStage::Fragment, uniform.count()});
  }
  for (const auto& sampler : _uniformHandler.getSamplers()) {
    programIRBuilder().AddSampler({sampler.name(), ShaderVar(sampler).type(), 0});
  }
  finalizeShaders();

  auto& ir = programIRBuilder().IR();
  std::string validationError;
  if (!ValidateProgramIR(ir, &validationError)) {
    LOGE("WebGPUProgramBuilder: %s", validationError.c_str());
    return nullptr;
  }
  if (!IRWritesPosition(ir)) {
    LOGE("WebGPUProgramBuilder: the vertex stage never writes the clip-space position");
    return nullptr;
  }

  ProgramBodyPrinter::Uniquify(ir);
  const auto vertexBody = ProgramBodyPrinter::PrintVertex(ir, ProgramBodyPrinter::WGSLTraits());
  auto fragmentBody = ProgramBodyPrinter::PrintFragment(ir, ProgramBodyPrinter::WGSLTraits());
  if (vertexBody.empty() || fragmentBody.empty()) {
    LOGE("WebGPUProgramBuilder: the stage bodies cannot be printed from the IR");
    return nullptr;
  }
  const auto swizzle = getProgramInfo()->getOutputSwizzle();
  if (swizzle != Swizzle::RGBA()) {
    fragmentBody += std::string(CUSTOM_COLOR_OUTPUT_NAME) + " = " + CUSTOM_COLOR_OUTPUT_NAME + "." +
                    swizzle.c_str() + ";\n";
  }

  WgslCallSite callSite = {};
  for (const auto& sampler : programIRBuilder().IR().interface.samplers) {
    callSite.samplerNames.insert(sampler.name);
  }
  std::set<std::string> emittedFunctions = {};
  std::string libraryText = {};
  ShaderLibraryBundle::Get().appendFunction(ShaderLibraryLanguage::WGSL, "__wgsl_support__",
                                            &emittedFunctions, &libraryText);
  for (const auto& expr : ir.exprs) {
    const char* symbol = nullptr;
    if (expr.kind == IRExprKind::TextureSample) {
      symbol = "tgfxTextureSample";
    } else if (expr.kind == IRExprKind::Call) {
      const auto* desc = FindOperator(expr.op);
      if (desc != nullptr && desc->glslSymbol != nullptr) {
        symbol = desc->glslSymbol;
      }
    }
    if (symbol != nullptr) {
      ShaderLibraryBundle::Get().appendFunction(ShaderLibraryLanguage::WGSL, symbol,
                                                &emittedFunctions, &libraryText);
    }
  }
  auto vertexText = WGSLPrinter::PrintVertex(programIRBuilder().IR().interface, libraryText, vertexBody, callSite);
  auto fragmentText =
      WGSLPrinter::PrintFragment(programIRBuilder().IR().interface, libraryText, fragmentBody, callSite);

  auto gpu = context->gpu();
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = std::move(vertexText);
  vertexModule.stage = ShaderStage::Vertex;
  vertexModule.format = ShaderCodeFormat::WGSL;
  if (!_uniformHandler.getUniforms(ShaderStage::Vertex).empty()) {
    vertexModule.uniformSlots[VertexUniformBlockName] = VERTEX_UBO_BINDING_POINT;
  }
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = std::move(fragmentText);
  fragmentModule.stage = ShaderStage::Fragment;
  fragmentModule.format = ShaderCodeFormat::WGSL;
  if (!_uniformHandler.getUniforms(ShaderStage::Fragment).empty()) {
    fragmentModule.uniformSlots[FragmentUniformBlockName] = FRAGMENT_UBO_BINDING_POINT;
  }
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }

  RenderPipelineDescriptor descriptor = {};
  const auto& vertexAttributes = programInfo->getVertexAttributes();
  VertexBufferLayout vertexLayout(vertexAttributes);
  for (size_t index = 0; index < vertexAttributes.size(); ++index) {
    vertexLayout.shaderLocations.push_back(static_cast<uint32_t>(index));
  }
  descriptor.vertex.bufferLayouts = {vertexLayout};
  const auto& instanceAttributes = programInfo->getInstanceAttributes();
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
  auto vertexUniformData = makeUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = makeUniformData(ShaderStage::Fragment);
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
