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
  /**
   * Width of the edge light falloff in layer pixels. The shader measures the edge distance in layer
   * pixels, so a band of one layer pixel covers less than one screen pixel once the layer is scaled
   * down and the falloff collapses into a hard threshold. Callers rendering at a reduced scale must
   * widen the band accordingly. Values below one are ignored.
   */
  float edgeBandLayerPixels = 1.0f;
};

struct GlassSDFGeometryParams : public GlassGeometryParams {
  float cornerRadius = 0.0f;
  float glassThickness = 0.0f;
};

struct GlassUDFGeometryParams : public GlassGeometryParams {
  float udfPixelToLayerPixelX = 1.0f;
  float udfPixelToLayerPixelY = 1.0f;
  /**
   * Layer-space distance between the two taps of the edge field's center difference. It must match
   * the tent radius used to build that field, otherwise the reconstructed edge distance is scaled.
   */
  float edgeSpanX = 1.0f;
  float edgeSpanY = 1.0f;
  // Top-left position of the physical texture in the full UDF texel coordinate space.
  float textureOriginX = 0.0f;
  float textureOriginY = 0.0f;
  // The edge-light field lives in a separate texture whose density is anchored to the on-screen
  // size; the sampling transform differs from the refraction field's, hence the separate origin.
  float edgeTextureOriginX = 0.0f;
  float edgeTextureOriginY = 0.0f;
  float edgePixelToLayerPixelX = 1.0f;
  float edgePixelToLayerPixelY = 1.0f;
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
  /**
   * @description Creates the processor.
   * @param mask RGBA8 texture where RGB carries the 24-bit refraction height.
   * @param edgeMask RGBA8 texture whose A carries the edge light height; only required when
   * enableEdgeLighting is true.
   */
  static PlacementPtr<GlassUDFGeometryFragmentProcessor> Make(
      BlockAllocator* allocator, std::shared_ptr<TextureProxy> mask,
      std::shared_ptr<TextureProxy> edgeMask, const GlassUDFGeometryParams& params,
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

  GlassUDFGeometryFragmentProcessor(std::shared_ptr<TextureProxy> mask,
                                    std::shared_ptr<TextureProxy> edgeMask,
                                    const GlassUDFGeometryParams& params, bool enableEdgeLighting);

  std::shared_ptr<TextureProxy> maskProxy;
  std::shared_ptr<TextureProxy> edgeMaskProxy;
  GlassUDFGeometryParams params = {};
  bool enableEdgeLighting = false;

  friend class GLSLGlassUDFGeometryFragmentProcessor;
};

}  // namespace tgfx
