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
PixelFormat GLSizeFormatToPixelFormat(unsigned sizeFormat) {
  switch (sizeFormat) {
    case GL_BGRA:
    case GL_BGRA8:
      return PixelFormat::BGRA_8888;
    case GL_R8:
    case GL_RED:
    case GL_ALPHA8:
    case GL_ALPHA:
      return PixelFormat::ALPHA_8;
    case GL_LUMINANCE8:
    case GL_LUMINANCE:
      return PixelFormat::GRAY_8;
    case GL_RG8:
    case GL_RG:
      return PixelFormat::RG_88;
    default:
      break;
  }
  return PixelFormat::RGBA_8888;
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

unsigned CreateGLProgram(const GLFunctions* gl, const std::string& vertex,
                         const std::string& fragment) {
  auto vertexShader = LoadGLShader(gl, GL_VERTEX_SHADER, vertex);
  if (vertexShader == 0) {
    return 0;
  }
  auto fragmentShader = LoadGLShader(gl, GL_FRAGMENT_SHADER, fragment);
  if (fragmentShader == 0) {
    return 0;
  }
  auto programHandle = gl->createProgram();
  gl->attachShader(programHandle, vertexShader);
  gl->attachShader(programHandle, fragmentShader);
  gl->linkProgram(programHandle);
  int success;
  gl->getProgramiv(programHandle, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    gl->getProgramInfoLog(programHandle, 512, nullptr, infoLog);
    gl->deleteProgram(programHandle);
    programHandle = 0;
    LOGE("CreateGLProgram failed:%s", infoLog);
  }
  gl->deleteShader(vertexShader);
  gl->deleteShader(fragmentShader);
  return programHandle;
}

unsigned LoadGLShader(const GLFunctions* gl, unsigned shaderType, const std::string& source) {
  auto shader = gl->createShader(shaderType);
  const char* files[] = {source.c_str()};
  gl->shaderSource(shader, 1, files, nullptr);
  gl->compileShader(shader);
#if defined(DEBUG) || !defined(TGFX_BUILD_FOR_WEB)
  int success;
  gl->getShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    gl->getShaderInfoLog(shader, 512, nullptr, infoLog);
    LOGE("Could not compile shader: %d %s", shaderType, infoLog);
    gl->deleteShader(shader);
    shader = 0;
  }
#endif
  return shader;
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
