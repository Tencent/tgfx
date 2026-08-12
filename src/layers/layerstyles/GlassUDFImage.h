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

#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

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
   */
  static std::shared_ptr<Image> Make(std::shared_ptr<Image> source, int coreWidth, int coreHeight,
                                     const Rect& textureRect, const Point& fineRadius,
                                     const Point& coarseRadius);

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
    return source->colorSpace();
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
  int _width = 0;
  int _height = 0;

  GlassUDFImage(std::shared_ptr<Image> source, int coreWidth, int coreHeight,
                const Rect& textureRect, const Point& fineRadius, const Point& coarseRadius);
};

}  // namespace tgfx
