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

#pragma once

#include <string>
#include "gpu/opengl/GLInterface.h"

namespace tgfx {
struct GLVersion {
  int majorVersion = -1;
  int minorVersion = -1;

  GLVersion() = default;

  GLVersion(int major, int minor) : majorVersion(major), minorVersion(minor) {
  }
};

PixelFormat GLSizeFormatToPixelFormat(unsigned sizeFormat);

unsigned PixelFormatToGLSizeFormat(PixelFormat pixelFormat);

GLVersion GetGLVersion(const char* versionString);

unsigned CreateGLProgram(const GLFunctions* gl, const std::string& vertex,
                         const std::string& fragment);

unsigned LoadGLShader(const GLFunctions* gl, unsigned shaderType, const std::string& source);

void ClearGLError(const GLFunctions* gl);

bool CheckGLErrorImpl(const GLFunctions* gl, std::string file, int line);

#ifdef DEBUG

#define CheckGLError(gl) CheckGLErrorImpl(gl, __FILE__, __LINE__)

#else

#define CheckGLError(gl) CheckGLErrorImpl(gl, "", 0)

#endif
}  // namespace tgfx
