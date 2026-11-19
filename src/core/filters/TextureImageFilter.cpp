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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/filters/TextureImageFilter.h"
#include <algorithm>
#include <cmath>
#include "core/images/TextureImage.h"
#include "core/utils/EffectTextureLimits.h"
#include "core/utils/TextureGrainNoise.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/TextureGrainFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/Resource.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

namespace {

// A quantized stored gradient has magnitude at most about 1.008. Its dot with a unit-square offset
// is therefore at most sqrt(2) * 1.008. The three FBM weights sum to 1 + 0.5 + 0.25 = 1.75,
// keeping the displacement magnitude below 2.5. Keep this in sync with the octave weights and
// gradient encoding used by TextureGrainNoise.
constexpr float TextureGrainDisplacementScale = 2.5f;
constexpr float MaximumTextureGrainOctaveScale = 4.0f;

float MaximumDisplacement(const TextureGrainParams& params) {
  return std::abs(params.radius) * params.pixelsPerLocalUnit * TextureGrainDisplacementScale;
}

bool HasRepresentableGrainCoordinates(const TextureGrainParams& params, const Rect& sourceBounds) {
  auto checkAxis = [](float low, float high, float origin, float center, float frequency) {
    auto lowGrain = (origin + low - center) * frequency;
    auto highGrain = (origin + high - center) * frequency;
    return std::isfinite(lowGrain) && std::isfinite(highGrain) &&
           std::isfinite(lowGrain * MaximumTextureGrainOctaveScale) &&
           std::isfinite(highGrain * MaximumTextureGrainOctaveScale);
  };
  auto localLeft = sourceBounds.left / params.pixelsPerLocalUnit;
  auto localRight = sourceBounds.right / params.pixelsPerLocalUnit;
  auto localTop = sourceBounds.top / params.pixelsPerLocalUnit;
  auto localBottom = sourceBounds.bottom / params.pixelsPerLocalUnit;
  return checkAxis(localLeft, localRight, params.sourceOrigin.x, params.nodeCenter.x,
                   params.frequencyX) &&
         checkAxis(localTop, localBottom, params.sourceOrigin.y, params.nodeCenter.y,
                   params.frequencyY);
}

std::shared_ptr<TextureProxy> MakeTextureProxy(Context* context,
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

Rect MakeSourceCrop(const Rect& drawRect, const Matrix* uvMatrix, float outset) {
  auto crop = drawRect;
  if (uvMatrix != nullptr) {
    crop = uvMatrix->mapRect(crop);
  }
  crop.outset(outset, outset);
  crop.roundOut();
  return crop;
}

std::shared_ptr<TextureProxy> MakeCroppedTextureProxy(const std::shared_ptr<Image>& source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling, Rect* crop,
                                                      const Point& sourceScale) {
  auto scaledCrop = *crop;
  scaledCrop.scale(sourceScale.x, sourceScale.y);
  scaledCrop.roundOut();
  // Align the raster grid before clipping so adjacent tiles share source samples at this scale.
  *crop = scaledCrop;
  crop->scale(1.0f / sourceScale.x, 1.0f / sourceScale.y);
  auto renderTarget = RenderTargetProxy::Make(
      args.context, static_cast<int>(scaledCrop.width()), static_cast<int>(scaledCrop.height()),
      source->isAlphaOnly(), 1, false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  FPArgs sourceArgs(args.context, args.renderFlags,
                    Rect::MakeWH(renderTarget->width(), renderTarget->height()),
                    std::max(sourceScale.x, sourceScale.y));
  auto sourceMatrix = Matrix::MakeTrans(crop->left, crop->top);
  sourceMatrix.preScale(1.0f / sourceScale.x, 1.0f / sourceScale.y);
  // The copy target covers exactly this crop. Clamp supports YUV texture views, while the Decal
  // specialization cannot sample buffers exposed as separate Y/U/V textures.
  auto sourceFP = FragmentProcessor::Make(source, sourceArgs, TileMode::Clamp, TileMode::Clamp,
                                          sampling, SrcRectConstraint::Fast, &sourceMatrix);
  if (!args.context->drawingManager()->fillRTWithFP(renderTarget, std::move(sourceFP),
                                                    args.renderFlags)) {
    return nullptr;
  }
  return renderTarget->asTextureProxy();
}

Point ComputeSourceScale(const Rect& crop, int sizeLimit) {
  auto safeLimit = std::max(1.0f, static_cast<float>(sizeLimit) - 2.0f);
  auto maxDimension = std::max(crop.width(), crop.height());
  auto scale = maxDimension > safeLimit ? safeLimit / maxDimension : 1.0f;
  return Point::Make(scale, scale);
}

std::shared_ptr<TextureView> GetTextureGrainGradientView(Context* context) {
  static const UniqueKey GradientViewKey = UniqueKey::Make();
  auto gradientView = Resource::Find<TextureView>(context, GradientViewKey);
  if (gradientView != nullptr) {
    return gradientView;
  }
  const auto& table = TextureGrainGradientTable();
  gradientView =
      TextureView::MakeRGBA(context, TextureGrainTableSize, TextureGrainTableSize, table.data(),
                            static_cast<size_t>(TextureGrainTableSize) * 4);
  if (gradientView != nullptr) {
    gradientView->assignUniqueKey(GradientViewKey);
  }
  return gradientView;
}

}  // namespace

TextureGrainParams MakeTextureGrainParams(float sizeX, float sizeY, float radius,
                                          const Point& nodeCenter, uint32_t seed,
                                          const Point& sourceOrigin, float pixelsPerLocalUnit,
                                          bool clipToShape) {
  TextureGrainParams params = {};
  auto frequencyX = std::isfinite(sizeX) && sizeX > 0.0f ? 1.0f / sizeX : 0.0f;
  auto frequencyY = std::isfinite(sizeY) && sizeY > 0.0f ? 1.0f / sizeY : 0.0f;
  params.frequencyX = std::isfinite(frequencyX) ? frequencyX : 0.0f;
  params.frequencyY = std::isfinite(frequencyY) ? frequencyY : 0.0f;
  params.radius = radius;
  params.seed = seed;
  params.nodeCenter = nodeCenter;
  params.sourceOrigin = sourceOrigin;
  params.pixelsPerLocalUnit = pixelsPerLocalUnit;
  params.clipToShape = clipToShape;
  return params;
}

TextureImageFilter::TextureImageFilter(const TextureGrainParams& params) : params(params) {
  if (!std::isfinite(this->params.pixelsPerLocalUnit) || this->params.pixelsPerLocalUnit <= 0.0f) {
    this->params.pixelsPerLocalUnit = 1.0f;
  }
}

Rect TextureImageFilter::onFilterBounds(const Rect& rect, MapDirection) const {
  if (params.frequencyX <= 0.0f || params.frequencyY <= 0.0f ||
      !HasRepresentableGrainCoordinates(params, rect)) {
    return rect;
  }
  auto maximumDisplacement = MaximumDisplacement(params);
  if (!std::isfinite(maximumDisplacement) || maximumDisplacement <= 0.0f) {
    return rect;
  }
  auto outset = maximumDisplacement + 2.0f;
  return rect.makeOutset(outset, outset);
}

PlacementPtr<FragmentProcessor> TextureImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  if (source == nullptr || args.context == nullptr) {
    return nullptr;
  }
  auto sourceBounds =
      Rect::MakeWH(static_cast<float>(source->width()), static_cast<float>(source->height()));
  auto maximumDisplacement = MaximumDisplacement(params);
  if (params.radius == 0.0f || params.frequencyX <= 0.0f || params.frequencyY <= 0.0f ||
      !std::isfinite(maximumDisplacement) ||
      !HasRepresentableGrainCoordinates(params, sourceBounds)) {
    return FragmentProcessor::Make(source, args, sampling, constraint, uvMatrix);
  }
  auto crop = MakeSourceCrop(args.drawRect, uvMatrix, maximumDisplacement + 3.0f);
  if (crop.isEmpty()) {
    return nullptr;
  }

  auto sizeLimit = ResolveEffectTextureDimension(
      args.context->gpu()->limits()->maxTextureDimension2D, TextureFilterTextureDimension);
  auto grainParams = params;
  std::shared_ptr<TextureProxy> sourceProxy;
  if (source->width() <= sizeLimit && source->height() <= sizeLimit) {
    sourceProxy = MakeTextureProxy(args.context, source);
    grainParams.sourceRect = Rect::MakeWH(source->width(), source->height());
  } else {
    if (!crop.intersect(Rect::MakeWH(source->width(), source->height()))) {
      return nullptr;
    }
    crop.roundOut();
    grainParams.sourceScale = ComputeSourceScale(crop, sizeLimit);
    sourceProxy = MakeCroppedTextureProxy(source, args, sampling, &crop, grainParams.sourceScale);
    grainParams.sourceRect = crop;
  }
  if (sourceProxy == nullptr) {
    return nullptr;
  }
  auto gradientView = GetTextureGrainGradientView(args.context);
  if (gradientView == nullptr) {
    return nullptr;
  }

  auto coordMatrix = uvMatrix != nullptr ? *uvMatrix : Matrix::I();
  auto useHardwareDecal = args.context->gpu()->features()->clampToBorder;
  return TextureGrainFragmentProcessor::Make(args.context->drawingAllocator(),
                                             std::move(sourceProxy), std::move(gradientView),
                                             grainParams, sampling, useHardwareDecal, coordMatrix);
}

}  // namespace tgfx
