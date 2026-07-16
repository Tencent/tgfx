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

#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"

namespace tgfx {

struct GlassRefractionParams {
  float glassWidth = 0.0f;
  float glassHeight = 0.0f;
  float halfWidth = 0.0f;
  float halfHeight = 0.0f;
  float cornerRadius = 0.0f;
  float minHalf = 0.0f;
  float innerHalfWidth = 0.0f;
  float innerHalfHeight = 0.0f;
  float innerRadius = 0.0f;
  float glassThickness = 0.0f;
  float refractionFactor = 0.0f;
  float dispersion = 0.0f;
  float splay = 0.0f;
  float depthRatio = 0.0f;
  float lightAngle = 0.0f;
  float lightIntensity = 0.0f;
  GlassShapeType shapeType = GlassShapeType::RoundedRect;
  bool hasMask = false;
  float glassOffsetX = 0.0f;
  float glassOffsetY = 0.0f;
  float glassScaleX = 1.0f;
  float glassScaleY = 1.0f;
  float invSourceW = 1.0f;
  float invSourceH = 1.0f;
};

class GlassRefractionFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              PlacementPtr<FragmentProcessor> source,
                                              PlacementPtr<FragmentProcessor> mask,
                                              const GlassRefractionParams& params);

  std::string name() const override {
    return "GlassRefractionFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GlassRefractionFragmentProcessor(PlacementPtr<FragmentProcessor> source,
                                   PlacementPtr<FragmentProcessor> mask,
                                   const GlassRefractionParams& params);

  void onComputeProcessorKey(BytesKey*) const override;

  GlassRefractionParams params;
};

}  // namespace tgfx
