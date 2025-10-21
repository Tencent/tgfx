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

#include "PixelImage.h"
#include "core/filters/DropShadowImageFilter.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> PixelImage::asFragmentProcessor(
    const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix,
    std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto mipmapped = hasMipmaps() && samplingArgs.sampling.mipmapMode != MipmapMode::None;
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, args.drawScale, BackingFit::Approx);
  auto textureProxy = lockTextureProxy(tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto fpMatrix =
      Matrix::MakeScale(static_cast<float>(textureProxy->width()) / static_cast<float>(width()),
                        static_cast<float>(textureProxy->height()) / static_cast<float>(height()));
  if (uvMatrix) {
    fpMatrix.preConcat(*uvMatrix);
  }
  auto fp = TiledTextureEffect::Make(textureProxy, samplingArgs, &fpMatrix, isAlphaOnly());
  if(!isAlphaOnly()) {
    return ColorSpaceXformEffect::Make(args.context->drawingBuffer(), std::move(fp), colorSpace().get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  }
  return fp;
}
}  // namespace tgfx
