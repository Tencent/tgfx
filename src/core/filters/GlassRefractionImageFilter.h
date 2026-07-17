/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/processors/GlassRefractionFragmentProcessor.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

class GlassRefractionImageFilter : public ImageFilter {
 public:
  GlassRefractionImageFilter(const GlassRefractionParams& params, std::shared_ptr<Image> fineMask,
                             std::shared_ptr<Image> coarseMask);

 protected:
  Type type() const override {
    return Type::Runtime;
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                 const Rect& renderBounds,
                                                 const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

 private:
  GlassRefractionParams params;
  std::shared_ptr<Image> fineMask;
  std::shared_ptr<Image> coarseMask;
};

}  // namespace tgfx
