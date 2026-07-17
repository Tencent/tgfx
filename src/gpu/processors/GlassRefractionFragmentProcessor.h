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
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"

namespace tgfx {

struct GlassRefractionParams {
  float glassWidth = 0.0f;
  float glassHeight = 0.0f;
  float halfW = 0.0f;
  float halfH = 0.0f;
  float cornerRadius = 0.0f;
  float minHalf = 0.0f;
  float innerHalfW = 0.0f;
  float innerHalfH = 0.0f;
  float innerRadius = 0.0f;
  float glassThickness = 0.0f;
  float refractionFactor = 0.0f;
  float dispersion = 0.0f;
  float splay = 0.0f;
  float depthRatio = 0.0f;
  float lightAngle = 0.0f;
  float lightIntensity = 0.0f;
  float origMinHalf = 0.0f;
  float udfPixelToLayerPixel = 1.0f;
  GlassShapeType shapeType = GlassShapeType::RoundedRect;
};

class GlassRefractionFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<GlassRefractionFragmentProcessor> Make(
      BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
      std::shared_ptr<TextureProxy> fineMask, std::shared_ptr<TextureProxy> coarseMask,
      const GlassRefractionParams& params);

  std::string name() const override {
    return "GlassRefractionFragmentProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  size_t onCountTextureSamplers() const override;

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t index) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GlassRefractionFragmentProcessor(std::shared_ptr<TextureProxy> source,
                                   std::shared_ptr<TextureProxy> fineMask,
                                   std::shared_ptr<TextureProxy> coarseMask,
                                   const GlassRefractionParams& params);

  std::shared_ptr<TextureProxy> sourceProxy;
  std::shared_ptr<TextureProxy> fineMaskProxy;
  std::shared_ptr<TextureProxy> coarseMaskProxy;
  GlassRefractionParams params;
  CoordTransform coordTransform;

  friend class GLSLGlassRefractionFragmentProcessor;
};

}  // namespace tgfx
