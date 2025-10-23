/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "SubsetImage.h"
#include "core/images/ScaledImage.h"
#include "core/utils/AddressOf.h"
#include "core/utils/MathExtra.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
std::shared_ptr<Image> SubsetImage::MakeFrom(std::shared_ptr<Image> source, const Rect& bounds) {
  if (source == nullptr || bounds.isEmpty()) {
    return nullptr;
  }
  auto image = std::shared_ptr<SubsetImage>(new SubsetImage(std::move(source), bounds));
  image->weakThis = image;
  return image;
}

SubsetImage::SubsetImage(std::shared_ptr<Image> source, const Rect& bounds)
    : TransformImage(std::move(source)), bounds(bounds) {
}

std::shared_ptr<Image> SubsetImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return SubsetImage::MakeFrom(std::move(newSource), bounds);
}

std::shared_ptr<Image> SubsetImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset.makeOffset(bounds.x(), bounds.y());
  return SubsetImage::MakeFrom(source, newBounds);
}

std::shared_ptr<Image> SubsetImage::onMakeScaled(int newWidth, int newHeight,
                                                 const SamplingOptions& sampling) const {
  float scaleX = static_cast<float>(newWidth) / static_cast<float>(width());
  float scaleY = static_cast<float>(newHeight) / static_cast<float>(height());
  auto sourceScaledWidth = scaleX * static_cast<float>(source->width());
  auto sourceScaledHeight = scaleY * static_cast<float>(source->height());
  if (!IsInteger(sourceScaledWidth) || !IsInteger(sourceScaledHeight)) {
    return Image::onMakeScaled(newWidth, newHeight, sampling);
  }
  auto newSource = source->makeScaled(static_cast<int>(sourceScaledWidth),
                                      static_cast<int>(sourceScaledHeight), sampling);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newBounds = Rect::MakeXYWH(bounds.x() * scaleX, bounds.y() * scaleY,
                                  static_cast<float>(newWidth), static_cast<float>(newHeight));
  return MakeFrom(std::move(newSource), newBounds);
}

PlacementPtr<FragmentProcessor> SubsetImage::asFragmentProcessor(
    const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix) const {
  auto matrix = concatUVMatrix(uvMatrix);
  auto drawBounds = args.drawRect;
  if (matrix) {
    matrix->mapRect(&drawBounds);
  }
  auto newSamplingArgs = samplingArgs;
  if (bounds.contains(drawBounds)) {
    if (samplingArgs.constraint != SrcRectConstraint::Strict && !newSamplingArgs.sampleArea) {
      // if samplingArgs has sampleArea, means the area is already subsetted
      newSamplingArgs.sampleArea = getSubset(drawBounds);
    }
    return FragmentProcessor::Make(source, args, newSamplingArgs, AddressOf(matrix));
  }
  if (!drawBounds.intersect(bounds)) {
    return nullptr;
  }
  drawBounds.offset(-bounds.x(), -bounds.y());
  drawBounds.roundOut();
  auto mipmapped = source->hasMipmaps() && samplingArgs.sampling.mipmapMode != MipmapMode::None;
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, args.drawScale);
  auto textureProxy = lockTextureProxySubset(tpArgs, drawBounds);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  newSamplingArgs.sampleArea = std::nullopt;
  auto fpMatrix = Matrix::MakeTrans(-drawBounds.left, -drawBounds.top);
  fpMatrix.preScale(static_cast<float>(textureProxy->width()) / drawBounds.width(),
                    static_cast<float>(textureProxy->height()) / drawBounds.height());
  if (uvMatrix) {
    fpMatrix.preConcat(*uvMatrix);
  }
  return TiledTextureEffect::Make(textureProxy, newSamplingArgs, &fpMatrix, source->isAlphaOnly());
}

std::optional<Matrix> SubsetImage::concatUVMatrix(const Matrix* uvMatrix) const {
  std::optional<Matrix> matrix = std::nullopt;
  if (bounds.x() != 0 || bounds.y() != 0) {
    matrix = Matrix::MakeTrans(bounds.x(), bounds.y());
  }
  if (uvMatrix != nullptr) {
    if (matrix) {
      matrix->preConcat(*uvMatrix);
    } else {
      matrix = *uvMatrix;
    }
  }
  return matrix;
}

std::optional<Rect> SubsetImage::getSubset(const Rect& drawRect) const {
  auto saftBounds = bounds;
  saftBounds.inset(0.5f, 0.5f);
  if (saftBounds.contains(drawRect)) {
    return std::nullopt;
  }
  return {bounds};
}

}  // namespace tgfx
