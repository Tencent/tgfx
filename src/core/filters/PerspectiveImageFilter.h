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

#pragma once

#include "tgfx/core/ImageFilter.h"

namespace tgfx {

class PerspectiveImageFilter final : public ImageFilter {
 public:
  explicit PerspectiveImageFilter(const PerspectiveInfo& info);

 private:
  Type type() const override {
    return Type::Perspective;
  }

  Rect onFilterBounds(const Rect& srcRect) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                 const Rect& renderBounds,
                                                 const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

  PerspectiveInfo info = {};
};

}  // namespace tgfx
