/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
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

#include "GLSLProgramBuilder.h"
#include <set>
#include <string>
#include "GLSLPrinter.h"
#include "core/utils/Log.h"
#include "gpu/UniformData.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "tgfx/gpu/GPU.h"
#if defined(TGFX_USE_VULKAN)
#include "gpu/vulkan/VulkanProgramBuilder.h"
#endif
#if defined(TGFX_USE_METAL)
#include "gpu/metal/MetalProgramBuilder.h"
#endif
#if defined(TGFX_USE_WEBGPU)
#include "gpu/webgpu/WebGPUProgramBuilder.h"
#endif

namespace tgfx {
static std::string TypeModifierString(ShaderVar::TypeModifier t, ShaderStage stage) {
  switch (t) {
    case ShaderVar::TypeModifier::Attribute:
      return "in";
    case ShaderVar::TypeModifier::Varying:
      return stage == ShaderStage::Vertex ? "out" : "in";
    case ShaderVar::TypeModifier::FlatVarying:
      return stage == ShaderStage::Vertex ? "flat out" : "flat in";
    case ShaderVar::TypeModifier::Uniform:
      return "uniform";
    case ShaderVar::TypeModifier::Out:
      return "out";
    case ShaderVar::TypeModifier::InOut:
      return "inout";
    default:
      return "";
  }
}

static constexpr std::pair<SLType, const char*> SLTypes[] = {
    {SLType::Void, "void"},
    {SLType::Float, "float"},
    {SLType::Float2, "vec2"},
    {SLType::Float3, "vec3"},
    {SLType::Float4, "vec4"},
    {SLType::Float2x2, "mat2"},
    {SLType::Float3x3, "mat3"},
    {SLType::Float4x4, "mat4"},
    {SLType::Half, "float"},
    {SLType::Half2, "vec2"},
    {SLType::Half3, "vec3"},
    {SLType::Half4, "vec4"},
    {SLType::Int, "int"},
    {SLType::Int2, "ivec2"},
    {SLType::Int3, "ivec3"},
    {SLType::Int4, "ivec4"},
    {SLType::Texture2DRectSampler, "sampler2DRect"},
    {SLType::TextureExternalSampler, "samplerExternalOES"},
    {SLType::Texture2DSampler, "sampler2D"},
};

static std::string SLTypePrecision(SLType t) {
  switch (t) {
    case SLType::Float:
    case SLType::Float2:
    case SLType::Float3:
    case SLType::Float4:
    case SLType::Float2x2:
    case SLType::Float3x3:
    case SLType::Float4x4:
    case SLType::Int:
    case SLType::Int2:
    case SLType::Int3:
    case SLType::Int4:
      return "highp";
    case SLType::Half:
    case SLType::Half2:
    case SLType::Half3:
    case SLType::Half4:
      // case SLType::Short:
      // case SLType::Short2:
      // case SLType::Short3:
      // case SLType::Short4:
      // case SLType::UShort:
      // case SLType::UShort2:
      // case SLType::UShort3:
      // case SLType::UShort4:
      return "mediump";
    default:
      return "";
  }
}

static std::string SLTypeString(SLType t) {
  for (const auto& pair : SLTypes) {
    if (pair.first == t) {
      return pair.second;
    }
  }
  return "";
}

std::shared_ptr<Program> ProgramBuilder::CreateProgram(Context* context,
                                                       const ProgramInfo* programInfo) {
#if defined(TGFX_USE_VULKAN)
  if (context != nullptr && context->backend() == Backend::Vulkan) {
    return VulkanProgramBuilder::Create(context, programInfo);
  }
#endif
#if defined(TGFX_USE_METAL)
  if (context != nullptr && context->backend() == Backend::Metal) {
    return MetalProgramBuilder::Create(context, programInfo);
  }
#endif
#if defined(TGFX_USE_WEBGPU)
  if (context != nullptr && context->backend() == Backend::WebGPU) {
    return WebGPUProgramBuilder::Create(context, programInfo);
  }
#endif
  GLSLProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}

GLSLProgramBuilder::GLSLProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
  _vertexBuilder.setVertexRecorder(&description.vertexCalls);
  _vertexBuilder.setValueTable(&description.values);
  _fragBuilder.setFragmentRecorder(&description.fragment);
  _fragBuilder.setValueTable(&description.values);
}

std::string GLSLProgramBuilder::getShaderVarDeclarations(const ShaderVar& var,
                                                         ShaderStage stage) const {
  std::string ret;
  if (var.modifier() != ShaderVar::TypeModifier::None) {
    ret += TypeModifierString(var.modifier(), stage);
    ret += " ";
  }
  auto shaderCaps = context->shaderCaps();
  if (shaderCaps->usesPrecisionModifiers) {
    ret += SLTypePrecision(var.type());
    ret += " ";
  }

  ret += SLTypeString(var.type());
  ret += " ";
  ret += var.name();
  return ret;
}

std::string GLSLProgramBuilder::getUniformBlockDeclaration(
    ShaderStage stage, const std::vector<Uniform>& uniforms) const {
  if (uniforms.empty()) {
    return "";
  }

  std::string result = "";
  const std::string& uniformBlockName =
      stage == ShaderStage::Vertex ? VertexUniformBlockName : FragmentUniformBlockName;
  static const std::string INDENT_STR = "    ";  // 4 spaces
  result += "layout(std140) uniform " + uniformBlockName + " {\n";
  std::string precision = "";
  auto shaderCaps = context->shaderCaps();
  for (const auto& uniform : uniforms) {
    const auto& var = ShaderVar(uniform);
    if (shaderCaps->usesPrecisionModifiers) {
      precision = SLTypePrecision(var.type());
    } else {
      precision = "";
    }
    result += INDENT_STR + precision + " " + SLTypeString(var.type()) + " " + uniform.name();
    if (uniform.count() > 1) {
      result += "[" + std::to_string(uniform.count()) + "]";
    }
    result += ";\n";
  }
  result += "};\n";
  return result;
}

std::string GLSLProgramBuilder::getUniformBlockDeclaration(
    ShaderStage stage, const std::vector<UniformDesc>& uniforms) const {
  if (uniforms.empty()) {
    return "";
  }
  std::string result = "";
  const std::string& uniformBlockName =
      stage == ShaderStage::Vertex ? VertexUniformBlockName : FragmentUniformBlockName;
  static const std::string INDENT_STR = "    ";  // 4 spaces
  result += "layout(std140) uniform " + uniformBlockName + " {\n";
  auto shaderCaps = context->shaderCaps();
  for (const auto& uniform : uniforms) {
    const std::string precision =
        shaderCaps->usesPrecisionModifiers ? SLTypePrecision(uniform.type) : "";
    result += INDENT_STR + precision + " " + SLTypeString(uniform.type) + " " + uniform.name;
    if (uniform.count > 1) {
      result += "[" + std::to_string(uniform.count) + "]";
    }
    result += ";\n";
  }
  result += "};\n";
  return result;
}

std::shared_ptr<Program> GLSLProgramBuilder::finalize() {
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
    const auto type = ShaderVar(uniform).type();
    description.interface.uniforms.push_back(
        {uniform.name(), type, ShaderStage::Vertex, uniform.count()});
    description.values.Intern(uniform.name(), type, ValueOrigin::Uniform);
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
    description.interface.uniforms.push_back(
        {uniform.name(), type, ShaderStage::Fragment, uniform.count()});
    description.values.Intern(uniform.name(), type, ValueOrigin::Uniform);
    if (uniform.count() > 1) {
      programIRBuilder().AddUniformArray(
          {uniform.name(), type, Precision::Default, ShaderStage::Fragment, uniform.count()});
    } else {
      programIRBuilder().AddUniform(
          {uniform.name(), type, Precision::Default, ShaderStage::Fragment, uniform.count()});
    }
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
  for (const auto& sampler : description.interface.samplers) {
    programIRBuilder().AddSampler({sampler.name, sampler.type, 0});
  }
  finalizeShaders();
  std::string validationError;
  if (!ValidateProgramIR(programIRBuilder().IR(), &validationError)) {
    LOGE("GLSLProgramBuilder: %s", validationError.c_str());
    return nullptr;
  }
  // The IR printer is the route: the main bodies come straight from the ProgramIR statements.
  // A failed print fails the program build loudly (no text fallback): the uncovered processor
  // form is the work item, not something to route around. TGFX_TEXT_PRINTER keeps the recorded
  // text route alive as a debugging escape hatch while the migration finishes.
    // Names become print-only here: one pass rewrites every renameable value name so that a
    // printed reference and a printed definition can only match when they are the same value.
    // Emitters that minted colliding (or missing) names stop silently connecting through them.
    GLSLPrinter::UniquifyValueNames(programIRBuilder().IR());
    // The printed body calls the operator functions by name, but nothing on the IR route pulls
    // their sources. Walk the calls and pull each referenced function; the library pull is
    // deduplicated and carries each function's own dependencies.
    for (const auto& expr : programIRBuilder().IR().exprs) {
      if (expr.kind == IRExprKind::TextureSample) {
        // The printed texture read goes through the shared reader function; the rect and external
        // samplers have their own.
        const char* reader = "tgfxTextureSample";
        if (expr.samplerType == SLType::Texture2DRectSampler) {
          reader = "tgfxTextureSampleRect";
        } else if (expr.samplerType == SLType::TextureExternalSampler) {
          reader = "tgfxTextureSampleExternal";
        }
        _fragBuilder.appendFunction(reader);
        continue;
      }
      if (expr.kind != IRExprKind::Call) {
        continue;
      }
      const auto* desc = FindOperator(expr.op);
      if (desc == nullptr || desc->glslSymbol == nullptr) {
        continue;
      }
      // A builtin wrapper (clamp) has no library source: pulling it would only log a miss.
      if (desc->includes.empty()) {
        continue;
      }
      _fragBuilder.appendFunction(desc->glslSymbol);
    }
    auto fragmentBody = GLSLPrinter::PrintFragmentBodyFromIR(programIRBuilder().IR());
    if (fragmentBody.empty()) {
      LOGE("GLSLProgramBuilder: the fragment body cannot be printed from the IR");
      return nullptr;
    }
    // A varying the fragment body reads must be written by the printed vertex body, or the link
    // stage rejects the program.
    {
      std::set<std::string> writtenVaryings = {};
      for (const auto& statement : programIRBuilder().IR().vertexStatements) {
        if (statement.kind == IRStatementKind::StoreVarying &&
            statement.dst.base < programIRBuilder().IR().values.size()) {
          writtenVaryings.insert(programIRBuilder().IR().values[statement.dst.base].debugName);
        }
      }
      for (const auto& varying : programIRBuilder().IR().interface.varyings) {
        if (fragmentBody.find(varying.name) == std::string::npos) {
          continue;
        }
        if (writtenVaryings.count(varying.name) == 0) {
          LOGE("GLSLProgramBuilder: the varying '%s' has no vertex-side IR writer",
               varying.name.c_str());
          return nullptr;
        }
      }
    }
    const auto swizzle = programInfo->getOutputSwizzle();
    if (swizzle != Swizzle::RGBA()) {
      // The epilogue belongs inside main: swizzle the output before the closing brace.
      fragmentBody += std::string(CUSTOM_COLOR_OUTPUT_NAME) + " = " + CUSTOM_COLOR_OUTPUT_NAME +
                      "." + swizzle.c_str() + ";\n";
    }
    fragmentBody += "}\n";
    _fragBuilder.setCodeText(fragmentBody);

    auto vertexBody = GLSLPrinter::PrintVertexBodyFromIR(programIRBuilder().IR());
    if (vertexBody.empty()) {
      LOGE("GLSLProgramBuilder: the vertex body cannot be printed from the IR");
      return nullptr;
    }
    vertexBody += "}\n";
    _vertexBuilder.setCodeText(vertexBody);
  const auto vertexText =
      GLSLPrinter::PrintStage(*this, ShaderStage::Vertex, description.interface, _vertexBuilder);
  const auto fragmentText =
      GLSLPrinter::PrintStage(*this, ShaderStage::Fragment, description.interface, _fragBuilder);
  auto gpu = context->gpu();
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = vertexText;
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = fragmentText;
  fragmentModule.stage = ShaderStage::Fragment;
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
  // Although the vertexProvider constructs the rectangle in a counterclockwise order, the model
  // uses a coordinate system with the Y-axis pointing downward, which is opposite to OpenGL's
  // default Y-axis direction (upward). Therefore, it is necessary to define the clockwise
  // direction as the front face, which is the opposite of OpenGL's default.
  descriptor.primitive = {programInfo->getCullMode(), FrontFace::CW};
  // Only sampleCount is needed here. The mask and alphaToCoverage are only used in RuntimeEffect.
  // If they are added here later, remember to encode them into ProgramInfo::getProgram()'s key.
  descriptor.multisample.count = programInfo->getSampleCount();
  // Forward the depth/stencil descriptor configured by the draw op (if any). Default
  // construction leaves all stencil ops at Keep so existing draw ops which never opt into
  // stencil writes keep their previous behaviour.
  descriptor.depthStencil = programInfo->getDepthStencil();
  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline == nullptr) {
    return nullptr;
  }
  return std::make_shared<Program>(std::move(pipeline), std::move(vertexUniformData),
                                   std::move(fragmentUniformData));
}

bool GLSLProgramBuilder::checkSamplerCounts() {
  auto shaderCaps = context->shaderCaps();
  if (numFragmentSamplers > shaderCaps->maxFragmentSamplers) {
    LOGE("Program would use too many fragment samplers.");
    return false;
  }
  return true;
}
}  // namespace tgfx
