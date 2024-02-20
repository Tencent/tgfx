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

#pragma once

#include "gpu/Resource.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * The base class for all images that can directly generate a texture proxy.
 */
class TextureImage : public Image {
 public:
  /**
   * Creates an Image wraps the existing TextureProxy, returns nullptr if textureProxy is nullptr.
   */
  static std::shared_ptr<Image> Wrap(std::shared_ptr<TextureProxy> textureProxy);

  explicit TextureImage(UniqueKey uniqueKey);

  std::shared_ptr<Image> makeRasterized(float rasterizationScale = 1.0f,
                                        SamplingOptions sampling = {}) const override;

  std::shared_ptr<Image> makeTextureImage(Context* context) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(Context* context, uint32_t renderFlags = 0) const;

 protected:
  UniqueKey uniqueKey = {};

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<Image> onMakeRGBAAA(int displayWidth, int displayHeight, int alphaStartX,
                                      int alphaStartY) const override;

  std::unique_ptr<FragmentProcessor> onMakeFragmentProcessor(const DrawArgs& args,
                                                             const Matrix* localMatrix,
                                                             TileMode tileModeX,
                                                             TileMode tileModeY) const override;

  virtual std::shared_ptr<TextureProxy> onLockTextureProxy(Context* context, const UniqueKey& key,
                                                           bool mipmapped,
                                                           uint32_t renderFlags) const = 0;

  friend class MipmapImage;
};
}  // namespace tgfx
