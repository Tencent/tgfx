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

#include "ResourceImage.h"
#include "core/images/MipmapImage.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
ResourceImage::ResourceImage(UniqueKey uniqueKey) : uniqueKey(std::move(uniqueKey)) {
}

std::shared_ptr<TextureProxy> ResourceImage::lockTextureProxy(const TPArgs& args) const {
  TRACE_EVENT;
  if (args.flattened && !isFlat()) {
    return Image::lockTextureProxy(args);
  }
  // Some options in TPArgs are ignored for ResourceImage because it has preset properties.
  TPArgs tpArgs(args.context, args.renderFlags, hasMipmaps(), args.flattened, uniqueKey);
  return onLockTextureProxy(tpArgs);
}

std::shared_ptr<Image> ResourceImage::onMakeMipmapped(bool enabled) const {
  TRACE_EVENT;
  auto source = std::static_pointer_cast<ResourceImage>(weakThis.lock());
  return enabled ? MipmapImage::MakeFrom(std::move(source)) : source;
}

std::unique_ptr<FragmentProcessor> ResourceImage::asFragmentProcessor(
    const FPArgs& args, TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  TRACE_EVENT;
  TPArgs tpArgs(args.context, args.renderFlags, hasMipmaps(), false, uniqueKey);
  auto proxy = onLockTextureProxy(tpArgs);
  return TiledTextureEffect::Make(std::move(proxy), tileModeX, tileModeY, sampling, uvMatrix,
                                  isAlphaOnly());
}
}  // namespace tgfx
