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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"

namespace tgfx {

// Coordinates and lengths use layer-local units.
struct TextureGrainParams {
  float frequencyX = 0.04f;
  float frequencyY = 0.04f;
  float radius = 8.0f;
  uint32_t seed = 0;
  Point nodeCenter = Point::Zero();
  Point sourceOrigin = Point::Zero();
  float pixelsPerLocalUnit = 1.0f;
  bool clipToShape = false;
};

// pixelsPerLocalUnit must be finite and positive.
TextureGrainParams MakeTextureGrainParams(float sizeX, float sizeY, float radius,
                                          const Point& nodeCenter, uint32_t seed = 0,
                                          const Point& sourceOrigin = Point::Zero(),
                                          float pixelsPerLocalUnit = 1.0f,
                                          bool clipToShape = false);

class TextureImageFilter : public ImageFilter {
 public:
  explicit TextureImageFilter(const TextureGrainParams& params);

 protected:
  Type type() const override {
    return Type::Runtime;
  }

  Rect onFilterBounds(const Rect& rect, MapDirection mapDirection) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

 private:
  TextureGrainParams params = {};
};

}  // namespace tgfx
