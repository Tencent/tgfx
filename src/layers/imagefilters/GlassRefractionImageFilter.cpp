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

#include "layers/imagefilters/GlassRefractionImageFilter.h"
#include "core/images/TextureImage.h"
#include "gpu/TPArgs.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"

namespace tgfx {

GlassRefractionImageFilter::GlassRefractionImageFilter(const GlassRefractionParams& params,
                                                       const GlassSDFGeometryParams& sdfParams,
                                                       const GlassUDFGeometryParams& udfParams,
                                                       std::shared_ptr<Image> mask,
                                                       std::shared_ptr<Image> edgeMask)
    : params(params), sdfParams(sdfParams), udfParams(udfParams), mask(std::move(mask)),
      edgeMask(std::move(edgeMask)) {
}

static std::shared_ptr<TextureProxy> MakeTextureProxy(Context* context,
                                                      const std::shared_ptr<Image>& image) {
  if (image == nullptr || context == nullptr) {
    return nullptr;
  }
  auto textureImage = image->makeTextureImage(context);
  if (textureImage == nullptr) {
    return nullptr;
  }
  auto textureImageImpl = std::static_pointer_cast<TextureImage>(textureImage);
  return textureImageImpl->getTextureProxy();
}

// Must mirror the shader's displacement clamps: the SDF path displaces by at most
// glassThickness * refractionFactor, the UDF path by 0.999 * minHalf * refractionFactor *
// depthRatio (smoothed near depth 0), and dispersion spreads the channels by (1 + dispersion).
static float GetRefractionOutsetLayerPixels(const GlassRefractionParams& params,
                                            const GlassSDFGeometryParams& sdfParams) {
  float depthRatio = std::clamp(sdfParams.depthRatio / 0.1f, 0.0f, 1.0f);
  float depthScale = depthRatio * depthRatio * (3.0f - 2.0f * depthRatio);
  float minHalf = std::min(params.origWidth, params.origHeight) * 0.5f;
  float udfOutset = 0.999f * minHalf * sdfParams.refractionFactor * sdfParams.depthRatio * depthScale;
  float sdfOutset = sdfParams.glassThickness * sdfParams.refractionFactor;
  return std::max(std::max(sdfOutset, udfOutset) * (1.0f + params.dispersion), 1.0f);
}

Rect GlassRefractionImageFilter::onFilterBounds(const Rect& rect, MapDirection) const {
  // The outset covers both mapping directions: forward keeps the output bounds large enough for
  // the direct-attach branch, reverse keeps the sampled input bounds wide enough for the
  // displaced reads when the filter is baked offscreen.
  auto outsetLayer = GetRefractionOutsetLayerPixels(params, sdfParams);
  return rect.makeOutset(outsetLayer * params.layerPixelToSourcePixelX,
                         outsetLayer * params.layerPixelToSourcePixelY);
}

PlacementPtr<FragmentProcessor> GlassRefractionImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& /*sampling*/,
    SrcRectConstraint /*constraint*/, const Matrix* uvMatrix) const {
  if (source == nullptr || args.context == nullptr) {
    return nullptr;
  }

  auto sourceProxy = MakeTextureProxy(args.context, source);
  if (sourceProxy == nullptr) {
    return nullptr;
  }

  std::shared_ptr<TextureProxy> maskProxy = nullptr;
  if (mask != nullptr) {
    maskProxy = MakeTextureProxy(args.context, mask);
    if (maskProxy == nullptr) {
      return nullptr;
    }
  }
  std::shared_ptr<TextureProxy> edgeMaskProxy = nullptr;
  if (edgeMask != nullptr) {
    edgeMaskProxy = MakeTextureProxy(args.context, edgeMask);
    if (edgeMaskProxy == nullptr) {
      return nullptr;
    }
  }

  auto allocator = args.context->drawingAllocator();
  PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry = nullptr;
  if (params.shapeType == GlassShapeType::AlphaMask) {
    geometry = GlassUDFGeometryFragmentProcessor::Make(allocator, std::move(maskProxy),
                                                       std::move(edgeMaskProxy), udfParams,
                                                       params.lightIntensity > 0.0f);
  } else {
    geometry = GlassSDFGeometryFragmentProcessor::Make(allocator, params.shapeType, sdfParams);
  }
  if (geometry == nullptr) {
    return nullptr;
  }

  auto coordMatrix = uvMatrix != nullptr ? *uvMatrix : Matrix::I();
  return GlassRefractionFragmentProcessor::Make(allocator, std::move(sourceProxy),
                                                std::move(geometry), params, coordMatrix);
}

}  // namespace tgfx
