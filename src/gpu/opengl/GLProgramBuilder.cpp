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
#include <sstream>
#include "gpu/UniformBuffer.h"
#include "gpu/UniformLayout.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
static std::string TypeModifierString(bool isLegacyES, ShaderVar::TypeModifier t,
                                      ShaderStage stage) {
  switch (t) {
    case ShaderVar::TypeModifier::None:
      return "";
    case ShaderVar::TypeModifier::Attribute:
      return isLegacyES ? "attribute" : "in";
    case ShaderVar::TypeModifier::Varying:
      return isLegacyES ? "varying" : (stage == ShaderStage::Vertex ? "out" : "in");
    case ShaderVar::TypeModifier::FlatVarying:
      return isLegacyES ? "varying" : (stage == ShaderStage::Vertex ? "flat out" : "flat in");
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
  if (const auto* caps = GLCaps::Get(context); caps->standard == GLStandard::GL) {
    // #version 140 - OpenGL 3.1
    return "#version 140\n";
  }

  return isLegacyES() ? "#version 100\n" : "#version 300 es\n";
}

std::string GLProgramBuilder::textureFuncName() const {
  return isLegacyES() ? "texture2D" : "texture";
}

std::string GLProgramBuilder::getShaderVarDeclarations(const ShaderVar& var,
                                                       ShaderStage stage) const {
  std::string ret;
  if (var.modifier() != ShaderVar::TypeModifier::None) {
    ret += TypeModifierString(isLegacyES(), var.modifier(), stage);
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

std::string GLProgramBuilder::getUniformBlockDeclaration(
    ShaderStage stage, const std::vector<Uniform>& uniforms) const {
  if (uniforms.empty()) {
    return "";
  }

  std::ostringstream os;
  const std::string& uniformBlockName =
      stage == ShaderStage::Vertex ? VertexUniformBlockName : FragmentUniformBlockName;
  constexpr std::string_view INDENT_STR = "    ";  // 4 spaces
  os << "layout(std140) uniform " << uniformBlockName << " {" << std::endl;
  std::string precision = "";
  for (const auto& uniform : uniforms) {
    const auto& var = ShaderVar(uniform);
    if (context->caps()->usesPrecisionModifiers) {
      precision = SLTypePrecision(var.type());
    } else {
      precision = "";
    }
    os << INDENT_STR << precision << " " << SLTypeString(var.type()) << " " << uniform.name() << ";"
       << std::endl;
  }
  os << "};" << std::endl;
  return os.str();
}

std::unique_ptr<PipelineProgram> GLProgramBuilder::finalize() {
  if (!isLegacyES()) {
    fragmentShaderBuilder()->declareCustomOutputColor();
  }
  finalizeShaders();
  const auto& vertex = vertexShaderBuilder()->shaderString();
  const auto& fragment = fragmentShaderBuilder()->shaderString();

#if 0
  LOGI("vertex shader:\n%s\n\n", vertex.c_str());
  LOGI("fragment shader:\n%s\n\n", fragment.c_str());
#endif

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
  auto vertexUniformBuffer = _uniformHandler.makeUniformBuffer(ShaderStage::Vertex);
  auto fragmentUniformBuffer = _uniformHandler.makeUniformBuffer(ShaderStage::Fragment);

  std::unique_ptr<UniformLayout> uniformLayout = nullptr;
  const auto* caps = GLCaps::Get(context);
  if (caps->uboSupport) {
    uniformLayout = std::make_unique<UniformLayout>(
        std::vector<std::string>{VertexUniformBlockName, FragmentUniformBlockName},
        vertexUniformBuffer.get(), fragmentUniformBuffer.get());
  } else {
    uniformLayout = std::make_unique<UniformLayout>(
        std::vector<std::string>{}, vertexUniformBuffer.get(), fragmentUniformBuffer.get());
  }

  auto pipeline = std::make_unique<GLRenderPipeline>(programID, std::move(uniformLayout),
                                                     programInfo->getVertexAttributes(),
                                                     programInfo->getBlendFormula());

  return std::make_unique<PipelineProgram>(std::move(pipeline), std::move(vertexUniformBuffer),
                                           std::move(fragmentUniformBuffer));
}

bool GLProgramBuilder::checkSamplerCounts() {
  auto caps = GLCaps::Get(context);
  if (numFragmentSamplers > caps->maxFragmentSamplers) {
    LOGE("Program would use too many fragment samplers.");
    return false;
  }
  return true;
}

bool GLProgramBuilder::isLegacyES() const {
  const auto* caps = GLCaps::Get(context);
  return (caps->standard == GLStandard::GLES && caps->version < GL_VER(3, 0)) ||
         (caps->standard == GLStandard::WebGL && caps->version < GL_VER(2, 0));
}
}  // namespace tgfx
