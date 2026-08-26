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
#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"

namespace tgfx {

struct GlassRefractionParams {
  float dispersion = 0.0f;
  float lightAngle = 0.0f;
  float lightIntensity = 0.0f;
  float origWidth = 0.0f;
  float origHeight = 0.0f;
  float maxDisplacement = 0.0f;
  float glassUVScaleX = 0.0f;
  float glassUVScaleY = 0.0f;
  float glassUVOffsetX = 0.0f;
  float glassUVOffsetY = 0.0f;
  float layerPixelToSourcePixelX = 0.0f;
  float layerPixelToSourcePixelY = 0.0f;
  float frost = 0.0f;
  GlassShapeType shapeType = GlassShapeType::AlphaMask;
};

class GlassRefractionFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<GlassRefractionFragmentProcessor> Make(
      BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
      PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry,
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
                                   PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry,
                                   const GlassRefractionParams& params, const Matrix& coordMatrix);

  std::shared_ptr<TextureProxy> sourceProxy;
  GlassRefractionParams params = {};
  CoordTransform coordTransform;
  size_t geometryIndex = 0;

  friend class GLSLGlassRefractionFragmentProcessor;
};

}  // namespace tgfx
