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
#include <string>
#include "gpu/GPU.h"
#include "gpu/UniformData.h"

namespace tgfx {
static std::string TypeModifierString(bool varyingIsInOut, ShaderVar::TypeModifier t,
                                      ShaderStage stage) {
  switch (t) {
    case ShaderVar::TypeModifier::None:
      return "";
    case ShaderVar::TypeModifier::Attribute:
      return varyingIsInOut ? "in" : "attribute";
    case ShaderVar::TypeModifier::Varying:
      return varyingIsInOut ? (stage == ShaderStage::Vertex ? "out" : "in") : "varying";
    case ShaderVar::TypeModifier::FlatVarying:
      return varyingIsInOut ? (stage == ShaderStage::Vertex ? "flat out" : "flat in") : "varying";
    case ShaderVar::TypeModifier::Uniform:
      return "uniform";
    case ShaderVar::TypeModifier::Out:
      return "out";
    case ShaderVar::TypeModifier::InOut:
      return "inout";
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

static CullFaceDescriptor MakeCullFaceDescriptor(CullFaceType cullFaceType) {
  // Although the vertexProvider constructs the rectangle in a counterclockwise order, the model
  // uses a coordinate system with the Y-axis pointing downward, which is opposite to OpenGL's
  // default Y-axis direction (upward). Therefore, it is necessary to define the clockwise
  // direction as the front face, which is the opposite of OpenGL's default.
  CullFaceDescriptor descriptor = {false, CullFaceDescriptor::FrontDirection::CW,
                                   CullFaceDescriptor::Mode::Back};
  switch (cullFaceType) {
    case CullFaceType::None:
      break;
    case CullFaceType::Front:
      descriptor.enabled = true;
      descriptor.mode = CullFaceDescriptor::Mode::Front;
      break;
    case CullFaceType::Back:
      descriptor.enabled = true;
      descriptor.mode = CullFaceDescriptor::Mode::Back;
      break;
    case CullFaceType::FrontAndBack:
      descriptor.enabled = true;
      descriptor.mode = CullFaceDescriptor::Mode::FrontAndBack;
      break;
  }
  return descriptor;
}

std::shared_ptr<Program> ProgramBuilder::CreateProgram(Context* context,
                                                       const ProgramInfo* programInfo) {
  GLSLProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}

GLSLProgramBuilder::GLSLProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
}

std::string GLSLProgramBuilder::getShaderVarDeclarations(const ShaderVar& var,
                                                         ShaderStage stage) const {
  std::string ret;
  if (var.modifier() != ShaderVar::TypeModifier::None) {
    auto varyingIsInOut = getContext()->caps()->shaderCaps()->varyingIsInOut;
    ret += TypeModifierString(varyingIsInOut, var.modifier(), stage);
    ret += " ";
  }
  auto shaderCaps = context->caps()->shaderCaps();
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
  auto shaderCaps = context->caps()->shaderCaps();
  for (const auto& uniform : uniforms) {
    const auto& var = ShaderVar(uniform);
    if (shaderCaps->usesPrecisionModifiers) {
      precision = SLTypePrecision(var.type());
    } else {
      precision = "";
    }
    result +=
        INDENT_STR + precision + " " + SLTypeString(var.type()) + " " + uniform.name() + ";\n";
  }
  result += "};\n";
  return result;
}

std::shared_ptr<PipelineProgram> GLSLProgramBuilder::finalize() {
  auto shaderCaps = context->caps()->shaderCaps();
  if (shaderCaps->usesCustomColorOutputName) {
    fragmentShaderBuilder()->declareCustomOutputColor();
  }
  finalizeShaders();
  auto gpu = context->gpu();
  ShaderModuleDescriptor vertexModule = {};
  vertexModule.code = vertexShaderBuilder()->shaderString();
  vertexModule.stage = ShaderStage::Vertex;
  auto vertexShader = gpu->createShaderModule(vertexModule);
  if (vertexShader == nullptr) {
    return nullptr;
  }
  ShaderModuleDescriptor fragmentModule = {};
  fragmentModule.code = fragmentShaderBuilder()->shaderString();
  fragmentModule.stage = ShaderStage::Fragment;
  auto fragmentShader = gpu->createShaderModule(fragmentModule);
  if (fragmentShader == nullptr) {
    return nullptr;
  }
  RenderPipelineDescriptor descriptor = {};
  descriptor.vertex = {programInfo->getVertexAttributes()};
  descriptor.vertex.module = vertexShader;
  descriptor.fragment.module = fragmentShader;
  descriptor.fragment.colorAttachments.push_back(programInfo->getPipelineColorAttachment());
  auto vertexUniformData = _uniformHandler.makeUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = _uniformHandler.makeUniformData(ShaderStage::Fragment);
  if (vertexUniformData) {
    BindingEntry vertexBinding = {VertexUniformBlockName, VERTEX_UBO_BINDING_POINT};
    if (!shaderCaps->uboSupport) {
      vertexBinding.uniforms = vertexUniformData->uniforms();
      DEBUG_ASSERT(!vertexBinding.uniforms.empty());
    }
    descriptor.layout.uniformBlocks.push_back(vertexBinding);
  }
  if (fragmentUniformData) {
    BindingEntry fragmentBinding = {FragmentUniformBlockName, FRAGMENT_UBO_BINDING_POINT};
    if (!shaderCaps->uboSupport) {
      fragmentBinding.uniforms = fragmentUniformData->uniforms();
      DEBUG_ASSERT(!fragmentBinding.uniforms.empty());
    }
    descriptor.layout.uniformBlocks.push_back(fragmentBinding);
  }
  int textureBinding = TEXTURE_BINDING_POINT_START;
  for (const auto& sampler : _uniformHandler.getSamplers()) {
    descriptor.layout.textureSamplers.emplace_back(sampler.name(), textureBinding++);
  }
  descriptor.cullFace = MakeCullFaceDescriptor(programInfo->getCullFaceType());
  auto pipeline = gpu->createRenderPipeline(descriptor);
  if (pipeline == nullptr) {
    return nullptr;
  }
  return std::make_shared<PipelineProgram>(std::move(pipeline), std::move(vertexUniformData),
                                           std::move(fragmentUniformData));
}

bool GLSLProgramBuilder::checkSamplerCounts() {
  auto shaderCaps = context->caps()->shaderCaps();
  if (numFragmentSamplers > shaderCaps->maxFragmentSamplers) {
    LOGE("Program would use too many fragment samplers.");
    return false;
  }
  return true;
}
}  // namespace tgfx
