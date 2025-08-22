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
                                      ShaderFlags flag) {
  switch (t) {
    case ShaderVar::TypeModifier::None:
      return "";
    case ShaderVar::TypeModifier::Attribute:
      return isDesktopGL ? "in" : "attribute";
    case ShaderVar::TypeModifier::Varying:
      return isDesktopGL ? (flag == ShaderFlags::Vertex ? "out" : "in") : "varying";
    case ShaderVar::TypeModifier::FlatVarying:
      return isDesktopGL ? (flag == ShaderFlags::Vertex ? "flat out" : "flat in") : "varying";
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
    {SLType::Int, "int"},
    {SLType::Int2, "ivec2"},
    {SLType::Int3, "ivec3"},
    {SLType::Int4, "ivec4"},
    {SLType::UByte4Color, "vec4"},
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
    case SLType::UByte4Color:
      return "highp";
    // case SLType::Half:
    // case SLType::Half2:
    // case SLType::Half3:
    // case SLType::Half4:
    // case SLType::Short:
    // case SLType::Short2:
    // case SLType::Short3:
    // case SLType::Short4:
    // case SLType::UShort:
    // case SLType::UShort2:
    // case SLType::UShort3:
    // case SLType::UShort4:
    //   return "mediump";
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

std::unique_ptr<Program> ProgramBuilder::CreateProgram(Context* context, const Pipeline* pipeline) {
  GLProgramBuilder builder(context, pipeline);
  if (!builder.emitAndInstallProcessors()) {
    return nullptr;
  }
  return builder.finalize();
}

GLProgramBuilder::GLProgramBuilder(Context* context, const Pipeline* pipeline)
    : ProgramBuilder(context, pipeline), _varyingHandler(this), _uniformHandler(this),
      _vertexBuilder(this), _fragBuilder(this) {
}

std::string GLProgramBuilder::versionDeclString() {
  return isDesktopGL() ? "#version 150\n" : "#version 100\n";
}

std::string GLProgramBuilder::textureFuncName() const {
  return isDesktopGL() ? "texture" : "texture2D";
}

std::string GLProgramBuilder::getShaderVarDeclarations(const ShaderVar& var,
                                                       ShaderFlags flag) const {
  std::string ret;
  if (var.typeModifier() != ShaderVar::TypeModifier::None) {
    ret += TypeModifierString(isDesktopGL(), var.typeModifier(), flag);
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

void GLProgramBuilder::collectAttributes(unsigned int programID) {
  auto gl = GLFunctions::Get(context);
  vertexStride = 0;
  for (const auto* attr : pipeline->getGeometryProcessor()->vertexAttributes()) {
    Attribute attribute = {};
    attribute.gpuType = attr->gpuType();
    attribute.offset = vertexStride;
    vertexStride += attr->sizeAlign4();
    attribute.location = gl->getAttribLocation(programID, attr->name().c_str());
    DEBUG_ASSERT(attribute.location != -1);
    attributes.push_back(attribute);
  }
}

static bool IsVariableChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
}

static int FindVariableIndex(const std::string& src, const std::string& varName) {
  enum State { Normal, Slash, LineComment, BlockComment, BlockStar } state = Normal;
  size_t n = src.size(), m = varName.size();
  for (size_t i = 0; i < n;) {
    char c = src[i];
    switch (state) {
      case Normal:
        if (c == '/') {
          state = Slash;
          ++i;
        } else {
          if (i + m <= n && src.substr(i, m) == varName) {
            char prev = (i == 0) ? '\0' : src[i - 1];
            char next = (i + m >= n) ? '\0' : src[i + m];
            if (!IsVariableChar(prev) && !IsVariableChar(next)) {
              return static_cast<int>(i);
            }
          }
          ++i;
        }
        break;
      case Slash:
        if (c == '/') {
          state = LineComment;
          ++i;
        } else if (c == '*') {
          state = BlockComment;
          ++i;
        } else {
          state = Normal;
        }
        break;
      case LineComment:
        if (c == '\n') state = Normal;
        ++i;
        break;
      case BlockComment:
        if (c == '*') state = BlockStar;
        ++i;
        break;
      case BlockStar:
        if (c == '/') {
          state = Normal;
          ++i;
        } else if (c == '*') {
          ++i;
        } else {
          state = BlockComment;
          ++i;
        }
        break;
    }
  }
  return -1;
}

struct UniformInfo {
  std::string name;
  SLType type;
  int variableIndex;
};

bool GLProgramBuilder::collectUniformsAndSamplers(unsigned programID, const std::string& vertex,
                                                  const std::string& fragment) {
  auto gl = GLFunctions::Get(context);
  int uniformCount = 0;
  gl->getProgramiv(programID, GL_ACTIVE_UNIFORMS, &uniformCount);
  std::vector<UniformInfo> uniformList = {};
  for (int i = 0; i < uniformCount; ++i) {
    char name[256];
    int length = 0;
    int size = 0;
    unsigned type = 0;
    gl->getActiveUniform(programID, static_cast<unsigned>(i), sizeof(name), &length, &size, &type,
                         name);
    if (length <= 0) {
      LOGE(
          "GLProgramBuilder::collectUniformsAndSamplers() Invalid uniform name length %d for "
          "uniform %d",
          length, i);
      return false;
    }
    UniformInfo info = {};
    info.name = std::string(name, static_cast<size_t>(length));
    switch (type) {
      case GL_FLOAT:
        info.type = SLType::Float;
        break;
      case GL_FLOAT_VEC2:
        info.type = SLType::Float2;
        break;
      case GL_FLOAT_VEC3:
        info.type = SLType::Float3;
        break;
      case GL_FLOAT_VEC4:
        info.type = SLType::Float4;
        break;
      case GL_INT:
        info.type = SLType::Int;
        break;
      case GL_INT_VEC2:
        info.type = SLType::Int2;
        break;
      case GL_INT_VEC3:
        info.type = SLType::Int3;
        break;
      case GL_INT_VEC4:
        info.type = SLType::Int4;
        break;
      case GL_FLOAT_MAT2:
        info.type = SLType::Float2x2;
        break;
      case GL_FLOAT_MAT3:
        info.type = SLType::Float3x3;
        break;
      case GL_FLOAT_MAT4:
        info.type = SLType::Float4x4;
        break;
      case GL_SAMPLER_2D:
        info.type = SLType::Texture2DSampler;
        break;
      case GL_SAMPLER_2D_RECT:
        info.type = SLType::Texture2DRectSampler;
        break;
      case GL_SAMPLER_EXTERNAL_OES:
        info.type = SLType::TextureExternalSampler;
        break;
      default:
        LOGE(
            "GLProgramBuilder::collectUniformsAndSamplers() Unknown uniform type %u for uniform %s",
            type, name);
        return false;
        break;
    }
    info.variableIndex = FindVariableIndex(vertex, name);
    if (info.variableIndex < 0) {
      info.variableIndex = FindVariableIndex(fragment, name);
      if (info.variableIndex < 0) {
        LOGE(
            "GLProgramBuilder::collectUniformsAndSamplers() Could not find variable %s in either "
            "vertex or fragment shader",
            name);
        return false;
      }
      info.variableIndex += static_cast<int>(vertex.size());
    }
    uniformList.push_back(info);
  }
  std::sort(uniformList.begin(), uniformList.end(), [](const UniformInfo& a, const UniformInfo& b) {
    return a.variableIndex < b.variableIndex;
  });
  for (auto& info : uniformList) {
    if (info.type == SLType::Texture2DSampler || info.type == SLType::Texture2DRectSampler ||
        info.type == SLType::TextureExternalSampler) {
      samplers.emplace_back(info.name, info.type);
    } else {
      uniforms.emplace_back(info.name, info.type);
    }
  }
  return true;
}

#ifdef DEBUG
static bool CompareUniforms(const std::vector<Uniform>& a, const std::vector<Uniform>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].name() != b[i].name() || a[i].type() != b[i].type()) {
      return false;
    }
  }
  return true;
}
#endif

std::unique_ptr<GLProgram> GLProgramBuilder::finalize() {
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
  collectAttributes(programID);
  if (!collectUniformsAndSamplers(programID, vertex, fragment)) {
    gl->deleteProgram(programID);
    return nullptr;
  }
  DEBUG_ASSERT(CompareUniforms(_uniformHandler.getUniforms(), uniforms));
  DEBUG_ASSERT(CompareUniforms(_uniformHandler.getSamplers(), samplers));
  gl->useProgram(programID);
  std::vector<int> uniformLocations = {};
  uniformLocations.reserve(uniforms.size());
  for (auto& uniform : uniforms) {
    auto location = gl->getUniformLocation(programID, uniform.name().c_str());
    DEBUG_ASSERT(location != -1);
    uniformLocations.push_back(location);
  }
  // Assign texture units to sampler uniforms up front, just once.
  int textureUint = 0;
  for (auto& sampler : samplers) {
    auto location = gl->getUniformLocation(programID, sampler.name().c_str());
    DEBUG_ASSERT(location != -1);
    gl->uniform1i(location, textureUint++);
  }
  return std::make_unique<GLProgram>(programID, uniforms, std::move(uniformLocations), attributes,
                                     static_cast<int>(vertexStride));
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
