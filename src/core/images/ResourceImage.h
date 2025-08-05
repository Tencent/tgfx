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

#include "core/images/MipmapImage.h"
#include "gpu/Resource.h"
#include "gpu/TPArgs.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * The base class for all images that contains a UniqueKey and can be cached as a GPU resource.
 */
class ResourceImage : public MipmapImage {
 protected:
  explicit ResourceImage(bool mipmap) : MipmapImage(mipmap) {
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const final;

  virtual std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args) const = 0;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;
};
}  // namespace tgfx
