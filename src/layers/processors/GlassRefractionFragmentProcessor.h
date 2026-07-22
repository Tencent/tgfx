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

namespace tgfx {

struct GlassRefractionParams {
  // Glass geometry (in layer pixel space).
  float glassWidth = 0.0f;
  float glassHeight = 0.0f;
  float halfW = 0.0f;
  float halfH = 0.0f;
  float minHalf = 0.0f;

  // Refraction parameters.
  float glassThickness = 0.0f;
  float refractionFactor = 0.0f;
  float dispersion = 0.0f;
  float splay = 0.0f;
  float depthRatio = 0.0f;

  // Lighting.
  float lightAngle = 0.0f;
  float lightIntensity = 0.0f;

  // UDF and scale factors.
  float origMinHalf = 0.0f;
  float origWidth = 0.0f;
  float origHeight = 0.0f;
  float udfPixelToLayerPixel = 1.0f;

  // Render-time state. In the inline asFragmentProcessor path, renderOffset is 0 because
  // coordinate translation is handled by coordMatrix. The base class lockTextureProxy fallback
  // passes uvMatrix = MakeTrans(renderBounds.left, renderBounds.top) to compensate.
  float renderOffsetX = 0.0f;
  float renderOffsetY = 0.0f;
};

class GlassRefractionFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<GlassRefractionFragmentProcessor> Make(
      BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
      std::shared_ptr<TextureProxy> fineMask, std::shared_ptr<TextureProxy> coarseMask,
      const GlassRefractionParams& params, const Matrix& coordMatrix = Matrix::I());

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
                                   const GlassRefractionParams& params, const Matrix& coordMatrix);

  std::shared_ptr<TextureProxy> sourceProxy;
  std::shared_ptr<TextureProxy> fineMaskProxy;
  std::shared_ptr<TextureProxy> coarseMaskProxy;
  GlassRefractionParams params;
  CoordTransform coordTransform;

  friend class GLSLGlassRefractionFragmentProcessor;
};

}  // namespace tgfx
