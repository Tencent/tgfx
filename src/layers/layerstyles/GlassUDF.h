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

#include "gpu/proxies/TextureProxy.h"
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

// Cap of the tent blur radius in UDF pixels, shared by the callers that clamp their requested
// radii and the generation that applies them, so the two sides can never drift apart.
constexpr int GlassUDFMaxTentRadius = 64;

/**
 * Everything GenerateGlassUDFTexture needs to build one UDF field. GlassStyle assembles this while
 * recording, where no GPU context exists yet, and the refraction filter turns it into a texture at
 * playback time.
 */
struct GlassUDFRequest {
  /**
   * The coverage source image to blur into the UDF.
   */
  std::shared_ptr<Image> source = nullptr;

  /**
   * Size of the UDF core in content pixels. The source is resampled to it before the window below
   * is taken, so every window shares one grid.
   */
  int coreWidth = 0;
  int coreHeight = 0;

  /**
   * The window of the full UDF space to generate, in UDF pixels.
   */
  Rect textureRect = {};

  /**
   * The tent blur radii in UDF pixels. Only the pair belonging to the requested field is read.
   */
  Point fineRadius = {};
  Point coarseRadius = {};

  /**
   * Which field the generated texture carries.
   */
  GlassUDFField field = GlassUDFField::Refraction;

  /**
   * Returns true when the parameters can produce a texture. Checked while recording so an
   * unusable request never reaches playback.
   */
  bool isValid() const;
};

/**
 * Blurs the requested window of the coverage image with tent kernels and returns one RGBA8 texture
 * carrying the requested field. Coordinates remain in the full UDF space. Returns nullptr when the
 * request is unusable or a render target cannot be allocated.
 */
std::shared_ptr<TextureProxy> GenerateGlassUDFTexture(Context* context,
                                                      const GlassUDFRequest& request);

}  // namespace tgfx
