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

#include "core/utils/Profiling.h"
#include "gpu/Resource.h"
#include "gpu/TPArgs.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * The base class for all images that contains a UniqueKey and can be cached as a GPU resource.
 */
class ResourceImage : public Image {
 public:
  explicit ResourceImage(UniqueKey uniqueKey);

  std::shared_ptr<Image> makeRasterized(float rasterizationScale = 1.0f,
                                        const SamplingOptions& sampling = {}) const override;

 protected:
  UniqueKey uniqueKey = {};

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override final;

  virtual std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args,
                                                           const UniqueKey& key) const = 0;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args, TileMode tileModeX,
                                                         TileMode tileModeY,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

  friend class MipmapImage;
};
}  // namespace tgfx
