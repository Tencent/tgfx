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

#include "GLProgramBuilder.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
static std::string TypeModifierString(bool isDesktopGL, ShaderVar::TypeModifier t,
                                      ShaderStage stage) {
  switch (t) {
    case ShaderVar::TypeModifier::None:
      return "";
    case ShaderVar::TypeModifier::Attribute:
      return isDesktopGL ? "in" : "attribute";
    case ShaderVar::TypeModifier::Varying:
      return isDesktopGL ? (stage == ShaderStage::Vertex ? "out" : "in") : "varying";
    case ShaderVar::TypeModifier::FlatVarying:
      return isDesktopGL ? (stage == ShaderStage::Vertex ? "flat out" : "flat in") : "varying";
    case ShaderVar::TypeModifier::Uniform:
      return "uniform";
    case ShaderVar::TypeModifier::Out:
      return "out";
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
  GLProgramBuilder builder(context, programInfo);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  auto program = builder.finalize();
  if (program == nullptr) {
    return nullptr;
  }
  return Resource::AddToCache(context, program.release());
}

GLProgramBuilder::GLProgramBuilder(Context* context, const ProgramInfo* programInfo)
    : ProgramBuilder(context, programInfo), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
}

std::string GLProgramBuilder::versionDeclString() {
  return isDesktopGL() ? "#version 150\n" : "#version 100\n";
}

std::string GLProgramBuilder::textureFuncName() const {
  return isDesktopGL() ? "texture" : "texture2D";
}

std::string GLProgramBuilder::getShaderVarDeclarations(const ShaderVar& var,
                                                       ShaderStage stage) const {
  std::string ret;
  if (var.modifier() != ShaderVar::TypeModifier::None) {
    ret += TypeModifierString(isDesktopGL(), var.modifier(), stage);
    ret += " ";
  }

  if (context->caps()->usesPrecisionModifiers) {
    ret += SLTypePrecision(var.type());
    ret += " ";
  }

  ret += SLTypeString(var.type());
  ret += " ";
  ret += var.name();
  return ret;
}

std::unique_ptr<PipelineProgram> GLProgramBuilder::finalize() {
  if (isDesktopGL()) {
    fragmentShaderBuilder()->declareCustomOutputColor();
  }
  finalizeShaders();
  auto vertex = vertexShaderBuilder()->shaderString();
  auto fragment = fragmentShaderBuilder()->shaderString();
  auto gl = GLFunctions::Get(context);
  auto programID = CreateGLProgram(gl, vertex, fragment);
  if (programID == 0) {
    return nullptr;
  }
  gl->useProgram(programID);
  auto& samplers = _uniformHandler.getSamplers();
  // Assign texture units to sampler uniforms up front, just once.
  int textureUint = 0;
  for (auto& sampler : samplers) {
    auto location = gl->getUniformLocation(programID, sampler.name().c_str());
    DEBUG_ASSERT(location != -1);
    gl->uniform1i(location, textureUint++);
  }
  auto uniformBuffer = _uniformHandler.makeUniformBuffer();
  auto pipeline = std::make_unique<GLRenderPipeline>(programID, uniformBuffer->uniforms(),
                                                     programInfo->getVertexAttributes(),
                                                     programInfo->getBlendFormula());
  return std::make_unique<PipelineProgram>(std::move(pipeline), std::move(uniformBuffer));
}

bool GLProgramBuilder::checkSamplerCounts() {
  auto caps = GLCaps::Get(context);
  if (numFragmentSamplers > caps->maxFragmentSamplers) {
    LOGE("Program would use too many fragment samplers.");
    return false;
  }
  return true;
}

bool GLProgramBuilder::isDesktopGL() const {
  auto caps = GLCaps::Get(context);
  return caps->standard == GLStandard::GL;
}
}  // namespace tgfx
