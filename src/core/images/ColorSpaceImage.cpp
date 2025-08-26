/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ColorSpaceImage.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/BackingFit.h"
#include "gpu/TPArgs.h"

namespace tgfx {

std::shared_ptr<TextureProxy> ColorSpaceImage::lockTextureProxy(const TPArgs& args) const {
  return _sourceImage->lockTextureProxy(args);
}

PlacementPtr<FragmentProcessor> ColorSpaceImage::asFragmentProcessor(const FPArgs& args,
    const SamplingArgs& samplingArgs, const Matrix* uvMatrix) const {
  auto textureProxy = lockTextureProxy(
      TPArgs(args.context, args.renderFlags, hasMipmaps(), 1.0f, BackingFit::Exact, _colorSpace));
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return TiledTextureEffect::Make(std::move(textureProxy), samplingArgs, uvMatrix, isAlphaOnly());
}
}  // namespace tgfx
