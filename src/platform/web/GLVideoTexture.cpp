/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLVideoTexture.h"
#include <emscripten/val.h>
#include "gpu/Gpu.h"

namespace tgfx {
using namespace emscripten;

static constexpr int ANDROID_ALIGNMENT = 16;

std::shared_ptr<GLVideoTexture> GLVideoTexture::Make(Context* context, int width, int height,
                                                     bool mipmapped) {
  static auto isAndroidMiniprogram =
      val::module_property("tgfx").call<bool>("isAndroidMiniprogram");
  int maxMipmapLevel = mipmapped ? context->caps()->getMaxMipmapLevel(width, height) : 0;
  auto sampler =
      context->gpu()->createSampler(width, height, PixelFormat::RGBA_8888, maxMipmapLevel + 1);
  if (sampler == nullptr) {
    return nullptr;
  }
  auto texture =
      Resource::AddToCache(context, new GLVideoTexture(std::move(sampler), width, height));
  if (isAndroidMiniprogram) {
    // https://stackoverflow.com/questions/28291204/something-about-stagefright-codec-input-format-in-android
    // Video decoder will align to multiples of 16 on the Android WeChat mini-program.
    texture->textureWidth += ANDROID_ALIGNMENT - width % ANDROID_ALIGNMENT;
    texture->textureHeight += ANDROID_ALIGNMENT - height % ANDROID_ALIGNMENT;
  }
  return texture;
}

GLVideoTexture::GLVideoTexture(std::unique_ptr<TextureSampler> sampler, int width, int height)
    : Texture(width, height, ImageOrigin::TopLeft), sampler(std::move(sampler)),
      textureWidth(width), textureHeight(height) {
}

size_t GLVideoTexture::memoryUsage() const {
  return static_cast<size_t>(width()) * static_cast<size_t>(height()) * 4;
}

Point GLVideoTexture::getTextureCoord(float x, float y) const {
  return {x / static_cast<float>(textureWidth), y / static_cast<float>(textureHeight)};
}

BackendTexture GLVideoTexture::getBackendTexture() const {
  return getSampler()->getBackendTexture(textureWidth, textureHeight);
}

void GLVideoTexture::onReleaseGPU() {
  context->gpu()->deleteSampler(sampler.get());
}
}  // namespace tgfx
