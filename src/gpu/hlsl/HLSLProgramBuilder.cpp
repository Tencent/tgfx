////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "HLSLProgramBuilder.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include "core/utils/Log.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/ShaderCaps.h"
#include "gpu/UniformData.h"
#include "tgfx/core/Swizzle.h"
#include "gpu/hlsl/HLSLPrinter.h"
#include "gpu/hlsl/HLSLOperatorEmitter.h"
#include "gpu/ir/ProgramValidation.h"
#include "gpu/shaders/ShaderLibraryBundle.h"
#include "tgfx/gpu/GPU.h"
#if defined(TGFX_USE_D3D12)
#include "gpu/d3d12/D3D12ShaderModule.h"
#endif

namespace tgfx {
namespace {

#if defined(TGFX_USE_D3D12)
/// Compares the compiled cbuffer member offsets against the std140 placements the CPU writes
/// through. The packoffset spelling is only correct if the two agree, and a drift shows up as a
/// slightly wrong colour rather than a failure, so the check exists as an explicit observation
/// behind TGFX_VERIFY_UNIFORM_LAYOUT.
void VerifyUniformOffsets(const std::shared_ptr<ShaderModule>& module, const UniformData* layout,
                          const char* blockName, const std::vector<UniformDesc>& uniforms,
                          ShaderStage stage) {
  if (module == nullptr || layout == nullptr) {
    return;
  }
  auto hlslModule = std::static_pointer_cast<D3D12ShaderModule>(module);
  for (const auto& uniform : uniforms) {
    if (uniform.stage != stage) {
      continue;
    }
    UniformData::FieldLayout field = {};
    if (!layout->fieldLayout(uniform.name, &field)) {
      continue;
    }
    uint32_t reflected = 0;
    if (!hlslModule->uniformOffset(blockName, uniform.name, &reflected)) {
      LOGE("HLSLProgramBuilder: '%s.%s' is not in the compiled cbuffer", blockName,
           uniform.name.c_str());
      continue;
    }
    if (reflected != field.offset) {
      LOGE("HLSLProgramBuilder: '%s.%s' reads at byte %u, the CPU writes it at %zu", blockName,
           uniform.name.c_str(), reflected, field.offset);
    }
  }
}
#endif

}  // namespace

std::shared_ptr<Program> HLSLProgramBuilder::Create(Context* context,
                                                    const ProgramInfo* programInfo) {
  HLSLProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}

HLSLProgramBuilder::HLSLProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
  _vertexBuilder.setVertexRecorder(&description.vertexCalls);
  _vertexBuilder.setValueTable(&description.values);
  _fragBuilder.setFragmentRecorder(&description.fragment);
  _fragBuilder.setValueTable(&description.values);
  _vertexBuilder.setLibraryLanguage(ShaderLibraryLanguage::HLSL);
  _fragBuilder.setLibraryLanguage(ShaderLibraryLanguage::HLSL);
}

std::string HLSLProgramBuilder::getShaderVarDeclarations(const ShaderVar&, ShaderStage) const {
  // The HLSL printer declares the interface as structs; the per-variable GLSL spelling does not
  // apply. An empty string keeps the base contract satisfied without emitting GLSL into an HLSL
  // shader.
  return "";
}

std::string HLSLProgramBuilder::getUniformBlockDeclaration(ShaderStage,
                                                           const std::vector<Uniform>&) const {
  return "";
}

std::string HLSLProgramBuilder::getUniformBlockDeclaration(ShaderStage,
                                                           const std::vector<UniformDesc>&) const {
  return "";
}

bool HLSLProgramBuilder::checkSamplerCounts() {
  auto shaderCaps = context->shaderCaps();
  if (numFragmentSamplers > shaderCaps->maxFragmentSamplers) {
    LOGE("HLSLProgramBuilder: the program would use too many fragment samplers.");
    return false;
  }
  return true;
}

std::shared_ptr<Program> HLSLProgramBuilder::finalize() {
  fragmentShaderBuilder()->declareCustomOutputColor();
  // The interface handlers collected the declarations while the processors emitted; the IR needs
  // the same entries so a printed reference resolves to an interface-owned name.
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
  // Direct3D matches the vertex output and the fragment input by semantic, and there is no
  // name-based linking to fall back on: each varying takes its own TEXCOORD slot, in declaration
  // order, and both stage structs are printed from this one list.
  uint8_t varyingLocation = 0;
  for (const auto& varying : _varyingHandler.getVaryings()) {
    description.interface.varyings.push_back(
        {varying.name(), varying.type(), varying.id(), varyingLocation++, false, varying.isFlat()});
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
    const auto type = ShaderVar(sampler).type();
    description.interface.samplers.push_back({sampler.name(), type});
    description.values.Intern(sampler.name(), type, ValueOrigin::Sampler);
  }
  for (const auto& attribute : description.interface.attributes) {
    programIRBuilder().AddAttribute({attribute.name, attribute.type, Precision::High, 0});
  }
  for (const auto& varying : description.interface.varyings) {
    programIRBuilder().AddVarying(
        {varying.name, varying.type, Precision::High, varying.location, varying.flat});
  }
  for (const auto& uniform : description.interface.uniforms) {
    programIRBuilder().AddUniform(
        {uniform.name, uniform.type, Precision::Default, uniform.stage, uniform.count});
  }
  for (const auto& sampler : description.interface.samplers) {
    programIRBuilder().AddSampler({sampler.name, sampler.type, 0});
  }
  finalizeShaders();
  std::string validationError;
  if (!ValidateProgramIR(programIRBuilder().IR(), &validationError)) {
    LOGE("HLSLProgramBuilder: %s", validationError.c_str());
    return nullptr;
  }
  // The uniform blocks are created before printing: the cbuffer members are pinned to their
  // derived std140 placements, which only this table carries.
  auto vertexUniformData = _uniformHandler.makeUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = _uniformHandler.makeUniformData(ShaderStage::Fragment);

  HLSLPrinter::UniquifyValueNames(programIRBuilder().IR());
  // The printed body calls the operator functions by name; walk the calls and pull each
  // referenced function from the HLSL section. A builtin wrapper (clamp) has no library source,
  // and a texture read goes through the shared reader the printer spells itself.
  const auto& ir = programIRBuilder().IR();
  for (const auto& expr : ir.exprs) {
    if (expr.kind == IRExprKind::TextureSample) {
      if (!_fragBuilder.appendFunction("tgfxTextureSample").empty()) {
        continue;
      }
      LOGE("HLSLProgramBuilder: the HLSL section carries no tgfxTextureSample");
      return nullptr;
    }
    if (expr.kind != IRExprKind::Call) {
      continue;
    }
    const auto* desc = FindOperator(expr.op);
    if (desc == nullptr || desc->glslSymbol == nullptr || desc->includes.empty()) {
      continue;
    }
    if (_fragBuilder.appendFunction(desc->glslSymbol).empty()) {
      LOGE("HLSLProgramBuilder: the HLSL section carries no '%s'", desc->glslSymbol);
      return nullptr;
    }
  }

  const auto fragmentBody = HLSLPrinter::PrintFragmentBodyFromIR(ir);
  if (fragmentBody.empty()) {
    LOGE("HLSLProgramBuilder: the fragment body cannot be printed from the IR");
    return nullptr;
  }
  const auto swizzle = programInfo->getOutputSwizzle();
  std::string fragmentCode = fragmentBody;
  if (swizzle != Swizzle::RGBA()) {
    fragmentCode += std::string(CUSTOM_COLOR_OUTPUT_NAME) + " = " + CUSTOM_COLOR_OUTPUT_NAME + "." +
                    swizzle.c_str() + ";\n";
  }
  fragmentCode += std::string("return ") + CUSTOM_COLOR_OUTPUT_NAME + ";\n";
  fragmentCode += "}\n";
  _fragBuilder.setCodeText(fragmentCode);

  const auto vertexBody = HLSLPrinter::PrintVertexBodyFromIR(ir);
  if (vertexBody.empty()) {
    LOGE("HLSLProgramBuilder: the vertex body cannot be printed from the IR");
    return nullptr;
  }
  std::string vertexCode = vertexBody;
  vertexCode += "  VSOut stage_output;\n";
  vertexCode += "  stage_output.position = tgfx_Position;\n";
  for (const auto& varying : description.interface.varyings) {
    vertexCode += "  stage_output." + varying.name + " = " + varying.name + ";\n";
  }
  vertexCode += "  return stage_output;\n";
  vertexCode += "}\n";
  _vertexBuilder.setCodeText(vertexCode);

  const auto vertexText = HLSLPrinter::PrintStage(*this, ShaderStage::Vertex,
                                                  description.interface, _vertexBuilder,
                                                  vertexUniformData.get());
  const auto fragmentText = HLSLPrinter::PrintStage(*this, ShaderStage::Fragment,
                                                    description.interface, _fragBuilder,
                                                    fragmentUniformData.get());
  if (vertexText.empty() || fragmentText.empty()) {
    LOGE("HLSLProgramBuilder: the stage text cannot be printed");
    return nullptr;
  }
  if (const char* dumpDir = std::getenv("TGFX_DUMP_PROGRAM")) {
    static std::atomic<int> dumpIndex{0};
    const int index = dumpIndex.fetch_add(1);
    const std::string base = std::string(dumpDir) + "/hlsl_program_" + std::to_string(index);
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
  vertexModule.format = ShaderCodeFormat::HLSL;
  if (vertexUniformData != nullptr) {
    vertexModule.uniformSlots[VertexUniformBlockName] = VERTEX_UBO_BINDING_POINT;
  }
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = fragmentText;
  fragmentModule.stage = ShaderStage::Fragment;
  fragmentModule.format = ShaderCodeFormat::HLSL;
  if (fragmentUniformData != nullptr) {
    fragmentModule.uniformSlots[FragmentUniformBlockName] = FRAGMENT_UBO_BINDING_POINT;
  }
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
#if defined(TGFX_USE_D3D12)
  if (std::getenv("TGFX_VERIFY_UNIFORM_LAYOUT") != nullptr) {
    VerifyUniformOffsets(vertexShader, vertexUniformData.get(), VertexUniformBlockName,
                         description.interface.uniforms, ShaderStage::Vertex);
    VerifyUniformOffsets(fragmentShader, fragmentUniformData.get(), FragmentUniformBlockName,
                         description.interface.uniforms, ShaderStage::Fragment);
  }
#endif
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
  int textureBinding = 0;
  for (const auto& sampler : _uniformHandler.getSamplers()) {
    descriptor.layout.textureSamplers.emplace_back(sampler.name(), textureBinding++);
  }
  // The rectangle is built counterclockwise in a y-down model space; Direct3D's front face is
  // clockwise there, the same convention the GLSL and Metal routes declare.
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
