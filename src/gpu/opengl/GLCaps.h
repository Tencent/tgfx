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
#include <unordered_map>
#include <vector>
#include "core/utils/EnumHasher.h"
#include "core/utils/Log.h"
#include "gpu/ShaderCaps.h"
#include "gpu/Swizzle.h"
#include "tgfx/gpu/Caps.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/opengl/GLDefines.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

#define GL_VER(major, minor) ((static_cast<uint32_t>(major) << 16) | static_cast<uint32_t>(minor))

namespace tgfx {
enum class GLStandard { None, GL, GLES, WebGL };

struct GLTextureFormat {
  unsigned sizedFormat = 0;
  unsigned internalFormatTexImage = 0;
  unsigned internalFormatRenderBuffer = 0;
  unsigned externalFormat = 0;
  unsigned externalType = 0;
};

struct ConfigInfo {
  GLTextureFormat format;
  std::vector<int> colorSampleCounts;
  Swizzle readSwizzle = Swizzle::RGBA();
  Swizzle writeSwizzle = Swizzle::RGBA();
};

enum class GLVendor { ARM, Google, Imagination, Intel, Qualcomm, NVIDIA, ATI, Other };

class GLInfo {
 public:
  GLStandard standard = GLStandard::None;
  uint32_t version = 0;
  GLGetString* getString = nullptr;
  GLGetStringi* getStringi = nullptr;
  GLGetIntegerv* getIntegerv = nullptr;
  GLGetInternalformativ* getInternalformativ = nullptr;
  GLGetShaderPrecisionFormat* getShaderPrecisionFormat = nullptr;

  GLInfo(GLGetString* getString, GLGetStringi* getStringi, GLGetIntegerv* getIntegerv,
         GLGetInternalformativ* getInternalformativ,
         GLGetShaderPrecisionFormat* getShaderPrecisionFormat);

  bool hasExtension(const std::string& extension) const;

 private:
  void fetchExtensions();

  std::vector<std::string> extensions = {};
};

class GLCaps : public Caps {
 public:
  GLStandard standard = GLStandard::None;
  uint32_t version = 0;
  GLVendor vendor = GLVendor::Other;
  bool pboSupport = false;
  bool flushBeforeWritePixels = false;

  static const GLCaps* Get(Context* context);

  explicit GLCaps(const GLInfo& info);

  const ShaderCaps* shaderCaps() const override {
    return &_shaderCaps;
  }

  const GLTextureFormat& getTextureFormat(PixelFormat pixelFormat) const;

  const Swizzle& getReadSwizzle(PixelFormat pixelFormat) const override;

  const Swizzle& getWriteSwizzle(PixelFormat pixelFormat) const override;

  bool isFormatRenderable(PixelFormat pixelFormat) const override;

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override;

 private:
  ShaderCaps _shaderCaps = {};
  std::unordered_map<PixelFormat, ConfigInfo, EnumHasher> pixelFormatMap = {};

  void initFormatMap(const GLInfo& info);
  void initColorSampleCount(const GLInfo& info);
  void initGLSupport(const GLInfo& info);
  void initGLESSupport(const GLInfo& info);
  void initWebGLSupport(const GLInfo& info);
};
}  // namespace tgfx
