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

#include "GLUtil.h"
#include "core/utils/USE.h"

namespace tgfx {

unsigned ToGLBlendFactor(BlendFactor blendFactor) {
  switch (blendFactor) {
    case BlendFactor::Zero:
      return GL_ZERO;
    case BlendFactor::One:
      return GL_ONE;
    case BlendFactor::Src:
      return GL_SRC_COLOR;
    case BlendFactor::OneMinusSrc:
      return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::Dst:
      return GL_DST_COLOR;
    case BlendFactor::OneMinusDst:
      return GL_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlpha:
      return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
      return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:
      return GL_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
      return GL_ONE_MINUS_DST_ALPHA;
    case BlendFactor::Src1:
      return GL_SRC1_COLOR;
    case BlendFactor::OneMinusSrc1:
      return GL_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::Src1Alpha:
      return GL_SRC1_ALPHA;
    case BlendFactor::OneMinusSrc1Alpha:
      return GL_ONE_MINUS_SRC1_ALPHA;
  }
  return GL_ZERO;
}

unsigned ToGLBlendOperation(BlendOperation blendOperation) {
  switch (blendOperation) {
    case BlendOperation::Add:
      return GL_FUNC_ADD;
    case BlendOperation::Subtract:
      return GL_FUNC_SUBTRACT;
    case BlendOperation::ReverseSubtract:
      return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOperation::Min:
      return GL_MIN;
    case BlendOperation::Max:
      return GL_MAX;
  }
  return GL_FUNC_ADD;
}

unsigned ToGLCompareFunction(CompareFunction compare) {
  switch (compare) {
    case CompareFunction::Never:
      return GL_NEVER;
    case CompareFunction::Less:
      return GL_LESS;
    case CompareFunction::Equal:
      return GL_EQUAL;
    case CompareFunction::LessEqual:
      return GL_LEQUAL;
    case CompareFunction::Greater:
      return GL_GREATER;
    case CompareFunction::NotEqual:
      return GL_NOTEQUAL;
    case CompareFunction::GreaterEqual:
      return GL_GEQUAL;
    case CompareFunction::Always:
      return GL_ALWAYS;
  }
  return GL_ALWAYS;
}

unsigned ToGLFrontFace(FrontFace frontFace) {
  switch (frontFace) {
    case FrontFace::CW:
      return GL_CW;
    case FrontFace::CCW:
      return GL_CCW;
  }
  return GL_CCW;
}

unsigned ToGLCullMode(CullMode mode) {
  switch (mode) {
    case CullMode::None:
      DEBUG_ASSERT(false);
      return GL_BACK;
    case CullMode::Front:
      return GL_FRONT;
    case CullMode::Back:
      return GL_BACK;
    case CullMode::FrontAndBack:
      return GL_FRONT_AND_BACK;
  }
  return GL_BACK;
}

unsigned ToGLStencilOperation(StencilOperation stencilOp) {
  switch (stencilOp) {
    case StencilOperation::Keep:
      return GL_KEEP;
    case StencilOperation::Zero:
      return GL_ZERO;
    case StencilOperation::Replace:
      return GL_REPLACE;
    case StencilOperation::Invert:
      return GL_INVERT;
    case StencilOperation::IncrementClamp:
      return GL_INCR;
    case StencilOperation::DecrementClamp:
      return GL_DECR;
    case StencilOperation::IncrementWrap:
      return GL_INCR_WRAP;
    case StencilOperation::DecrementWrap:
      return GL_DECR_WRAP;
  }
  return GL_KEEP;
}

unsigned PixelFormatToGLSizeFormat(PixelFormat pixelFormat) {
  switch (pixelFormat) {
    case PixelFormat::ALPHA_8:
      return GL_ALPHA8;
    case PixelFormat::GRAY_8:
      return GL_LUMINANCE8;
    case PixelFormat::RG_88:
      return GL_RG8;
    case PixelFormat::BGRA_8888:
      return GL_BGRA8;
    default:
      break;
  }
  return GL_RGBA8;
}

GLVersion GetGLVersion(const char* versionString) {
  if (versionString == nullptr) {
    return {};
  }
  int major, minor;
  int mesaMajor, mesaMinor;
  int n = sscanf(versionString, "%d.%d Mesa %d.%d", &major, &minor, &mesaMajor, &mesaMinor);
  if (4 == n) {
    return {major, minor};
  }
  n = sscanf(versionString, "%d.%d", &major, &minor);
  if (2 == n) {
    return {major, minor};
  }
  int esMajor, esMinor;
  n = sscanf(versionString, "OpenGL ES %d.%d (WebGL %d.%d", &esMajor, &esMinor, &major, &minor);
  if (4 == n) {
    return {major, minor};
  }
  char profile[2];
  n = sscanf(versionString, "OpenGL ES-%c%c %d.%d", profile, profile + 1, &major, &minor);
  if (4 == n) {
    return {major, minor};
  }
  n = sscanf(versionString, "OpenGL ES %d.%d", &major, &minor);
  if (2 == n) {
    return {major, minor};
  }
  return {};
}

void ClearGLError(const GLFunctions* gl) {
#ifdef TGFX_BUILD_FOR_WEB
  USE(gl);
#else
  while (gl->getError() != GL_NO_ERROR) {
  }
#endif
}

bool CheckGLErrorImpl(const GLFunctions* gl, std::string file, int line) {
#ifdef TGFX_BUILD_FOR_WEB
  USE(gl);
  USE(file);
  USE(line);
  return true;
#else
  bool success = true;
  unsigned errorCode;
  while ((errorCode = gl->getError()) != GL_NO_ERROR) {
    success = false;
    if (file.empty()) {
      LOGE("CheckGLError: %d", errorCode);
    } else {
      LOGE("CheckGLError: %d at %s:%d", errorCode, file.c_str(), line);
    }
  }
  return success;
#endif
}
}  // namespace tgfx
