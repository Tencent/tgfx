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

enum class GlassShapeType {
  RoundedRect,
  Ellipse,
  AlphaMask,
};

struct GlassGeometryParams {
  float halfW = 0.0f;
  float halfH = 0.0f;
  float refractionFactor = 0.0f;
  float splay = 0.0f;
  float depthRatio = 0.0f;
};

struct GlassSDFGeometryParams : public GlassGeometryParams {
  float cornerRadius = 0.0f;
  float glassThickness = 0.0f;
};

struct GlassUDFGeometryParams : public GlassGeometryParams {
  float udfPixelToLayerPixelX = 1.0f;
  float udfPixelToLayerPixelY = 1.0f;
};

/**
 * Computes shape-dependent refraction geometry from source UV coordinates supplied in inputColor.
 * The output is vec4(refractDirection.xy, displacementDistance, edgeWeight).
 */
class GlassShapeGeometryFragmentProcessor : public FragmentProcessor {
 protected:
  explicit GlassShapeGeometryFragmentProcessor(uint32_t classID) : FragmentProcessor(classID) {
  }
};

class GlassSDFGeometryFragmentProcessor : public GlassShapeGeometryFragmentProcessor {
 public:
  static PlacementPtr<GlassSDFGeometryFragmentProcessor> Make(BlockAllocator* allocator,
                                                              GlassShapeType shapeType,
                                                              const GlassSDFGeometryParams& params);

  std::string name() const override {
    return "GlassSDFGeometryFragmentProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GlassSDFGeometryFragmentProcessor(GlassShapeType shapeType, const GlassSDFGeometryParams& params);

  GlassShapeType shapeType = GlassShapeType::RoundedRect;
  GlassSDFGeometryParams params = {};

  friend class GLSLGlassSDFGeometryFragmentProcessor;
};

class GlassUDFGeometryFragmentProcessor : public GlassShapeGeometryFragmentProcessor {
 public:
  static PlacementPtr<GlassUDFGeometryFragmentProcessor> Make(
      BlockAllocator* allocator, std::shared_ptr<TextureProxy> fineMask,
      std::shared_ptr<TextureProxy> coarseMask, const GlassUDFGeometryParams& params,
      bool enableEdgeLighting);

  std::string name() const override {
    return "GlassUDFGeometryFragmentProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  size_t onCountTextureSamplers() const override;

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t index) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GlassUDFGeometryFragmentProcessor(std::shared_ptr<TextureProxy> fineMask,
                                    std::shared_ptr<TextureProxy> coarseMask,
                                    const GlassUDFGeometryParams& params, bool enableEdgeLighting);

  std::shared_ptr<TextureProxy> fineMaskProxy;
  std::shared_ptr<TextureProxy> coarseMaskProxy;
  GlassUDFGeometryParams params = {};
  bool enableEdgeLighting = false;

  friend class GLSLGlassUDFGeometryFragmentProcessor;
};

}  // namespace tgfx
