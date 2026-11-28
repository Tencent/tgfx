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
#include "gpu/ir/ProgramBodyPrinter.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/metal/MSLPrinter.h"
#include "gpu/shaders/ShaderLibraryBundle.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
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
  std::string validationError;
  // The processors emit their bodies into the ProgramIR; only the text escape hatch still fills
  // the typed description, so the two routes validate different graphs.
      if (!ValidateProgramIR(programIRBuilder().IR(), &validationError)) {
      LOGE("MetalProgramBuilder: %s", validationError.c_str());
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
  bool readsDestinationColor = false;
      // The IR route prints the same bodies the GL path prints, spelled in MSL: the shared operator
    // functions are pulled per stage, because each stage compiles on its own in Metal.
    auto& ir = programIRBuilder().IR();
    ProgramBodyPrinter::Uniquify(ir);
    for (const auto& expr : ir.exprs) {
      const char* symbol = nullptr;
      if (expr.kind == IRExprKind::TextureSample) {
        symbol = expr.samplerType == SLType::Texture2DRectSampler
                     ? "tgfxTextureSampleRect"
                     : (expr.samplerType == SLType::TextureExternalSampler
                            ? "tgfxTextureSampleExternal"
                            : "tgfxTextureSample");
      } else if (expr.kind == IRExprKind::Call) {
        const auto* desc = FindOperator(expr.op);
        if (desc == nullptr || desc->glslSymbol == nullptr || desc->includes.empty()) {
          continue;
        }
        symbol = desc->glslSymbol;
      }
      if (symbol == nullptr) {
        continue;
      }
      _vertexBuilder.appendFunction(symbol);
      _fragBuilder.appendFunction(symbol);
    }
    const auto vertexBody = MSLPrinter::PrintVertexBodyFromIR(ir);
    auto fragmentBody = MSLPrinter::PrintFragmentBodyFromIR(ir);
    if (vertexBody.empty() || fragmentBody.empty()) {
      LOGE("MetalProgramBuilder: the stage bodies cannot be printed from the IR");
      return nullptr;
    }
    // Alpha-only targets read the fragment's alpha channel, the same epilogue the GL route
    // appends before the stage returns its color.
    const auto swizzle = getProgramInfo()->getOutputSwizzle();
    if (swizzle != Swizzle::RGBA()) {
      fragmentBody += std::string(CUSTOM_COLOR_OUTPUT_NAME) + " = " + CUSTOM_COLOR_OUTPUT_NAME +
                      "." + swizzle.c_str() + ";\n";
    }
    _vertexBuilder.setCodeText(vertexBody);
    _fragBuilder.setCodeText(fragmentBody);
    readsDestinationColor = IRReadsStageColor(ir);
  
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
