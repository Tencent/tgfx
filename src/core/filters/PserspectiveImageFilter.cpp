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

#include "PserspectiveImageFilter.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

PerspectiveImageFilter::PerspectiveImageFilter(const PerspectiveInfo& info) : info(info) {
}

Rect PerspectiveImageFilter::onFilterBounds(const Rect& srcRect) const {
  //TODO: RicharrdChen
  return srcRect;
}

std::shared_ptr<TextureProxy> PerspectiveImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& renderBounds, const TPArgs& args) const {
  (void)source;
  (void)renderBounds;
  (void)args;
  //TODO: RicharrdChen
  return nullptr;
}

PlacementPtr<FragmentProcessor> PerspectiveImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

}  // namespace tgfx