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
                                                       std::shared_ptr<Image> fineMask,
                                                       std::shared_ptr<Image> coarseMask)
    : params(params), fineMask(std::move(fineMask)), coarseMask(std::move(coarseMask)) {
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

  std::shared_ptr<TextureProxy> fineMaskProxy = nullptr;
  std::shared_ptr<TextureProxy> coarseMaskProxy = nullptr;
  if (fineMask != nullptr) {
    fineMaskProxy = MakeTextureProxy(args.context, fineMask);
    if (fineMaskProxy == nullptr) {
      return nullptr;
    }
  }
  if (coarseMask != nullptr) {
    coarseMaskProxy = MakeTextureProxy(args.context, coarseMask);
    if (coarseMaskProxy == nullptr) {
      return nullptr;
    }
  }

  auto allocator = args.context->drawingAllocator();
  GlassShapeGeometryParams geometryParams = {};
  geometryParams.glassWidth = params.glassWidth;
  geometryParams.glassHeight = params.glassHeight;
  geometryParams.halfW = params.halfW;
  geometryParams.halfH = params.halfH;
  geometryParams.cornerRadius = params.cornerRadius;
  geometryParams.minHalf = params.minHalf;
  geometryParams.glassThickness = params.glassThickness;
  geometryParams.refractionFactor = params.refractionFactor;
  geometryParams.splay = params.splay;
  geometryParams.depthRatio = params.depthRatio;
  geometryParams.origMinHalf = params.origMinHalf;
  geometryParams.udfPixelToLayerPixel = params.udfPixelToLayerPixel;

  float sourceWidth = static_cast<float>(sourceProxy->width());
  float sourceHeight = static_cast<float>(sourceProxy->height());
  PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry = nullptr;
  if (params.shapeType == GlassShapeType::AlphaMask) {
    geometry = GlassUDFGeometryFragmentProcessor::Make(
        allocator, std::move(fineMaskProxy), std::move(coarseMaskProxy), geometryParams,
        sourceWidth, sourceHeight, params.lightIntensity > 0.0f);
  } else {
    geometry = GlassSDFGeometryFragmentProcessor::Make(allocator, params.shapeType, geometryParams,
                                                       sourceWidth, sourceHeight);
  }
  if (geometry == nullptr) {
    return nullptr;
  }

  auto coordMatrix = uvMatrix != nullptr ? *uvMatrix : Matrix::I();
  auto localParams = params;
  localParams.renderOffsetX = 0.0f;
  localParams.renderOffsetY = 0.0f;
  return GlassRefractionFragmentProcessor::Make(allocator, std::move(sourceProxy),
                                                std::move(geometry), localParams, coordMatrix);
}

}  // namespace tgfx
