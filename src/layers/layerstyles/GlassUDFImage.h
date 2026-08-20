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

#include "gpu/resources/ResourceKey.h"
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

// Cap of the tent blur radius in UDF pixels, shared by the callers that clamp their requested
// radii and the generation that applies them, so the two sides can never drift apart.
constexpr int GlassUDFMaxTentRadius = 64;

/**
 * GlassUDFImage defers the GPU generation of the UDF coverage texture until the image is actually
 * drawn, so GlassStyle works on canvases without a GPU surface at record time (e.g. picture
 * recording). The UDF generation requires a GPU context; storing only the parameters here keeps
 * the constructor GPU-free.
 */
class GlassUDFImage : public Image {
 public:
  /**
   * Creates a GlassUDFImage that generates the UDF texture lazily.
   * @param source The coverage source image to blur into the UDF.
   * @param coreWidth The width of the UDF core in content pixels.
   * @param coreHeight The height of the UDF core in content pixels.
   * @param textureRect The window of the full UDF space to generate, in UDF pixels.
   * @param fineRadius The tent blur radii for the refraction field, in UDF pixels.
   * @param coarseRadius The tent blur radii for the edge-light field, in UDF pixels.
   * @param field Which fields the generated texture carries.
   * @param uniqueKey Cache key of the generated texture. The caller must encode every parameter
   *                  that changes the generated pixels, including the identity of the source
   *                  content. An empty key disables reuse and regenerates the texture per draw.
   */
  static std::shared_ptr<Image> Make(std::shared_ptr<Image> source, int coreWidth, int coreHeight,
                                     const Rect& textureRect, const Point& fineRadius,
                                     const Point& coarseRadius,
                                     GlassUDFField field = GlassUDFField::Both,
                                     UniqueKey uniqueKey = {});

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    // The UDF texture packs the refraction field in RGB and the edge-light field in A.
    return false;
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    // The UDF texture packs the refraction field in RGB and the edge-light field in A; it is not
    // color data, so it carries no color space.
    static const std::shared_ptr<ColorSpace> kNoColorSpace = nullptr;
    return kNoColorSpace;
  }

 protected:
  Type type() const override {
    return Type::Texture;
  }

  std::shared_ptr<Image> onMakeMipmapped(bool) const override {
    return nullptr;
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

 private:
  std::shared_ptr<Image> source = nullptr;
  int coreWidth = 0;
  int coreHeight = 0;
  Rect textureRect = {};
  Point fineRadius = {};
  Point coarseRadius = {};
  GlassUDFField field = GlassUDFField::Both;
  int _width = 0;
  int _height = 0;
  // Cache key of the generated texture. The texture itself lives in the context's resource cache,
  // so instances of this class stay cheap and can be rebuilt per draw.
  UniqueKey uniqueKey = {};

  GlassUDFImage(std::shared_ptr<Image> source, int coreWidth, int coreHeight,
                const Rect& textureRect, const Point& fineRadius, const Point& coarseRadius,
                GlassUDFField field, UniqueKey uniqueKey);
};

}  // namespace tgfx
