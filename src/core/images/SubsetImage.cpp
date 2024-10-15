/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/NeedMipmaps.h"
#include "gpu/TPArgs.h"
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

std::unique_ptr<FragmentProcessor> SubsetImage::asFragmentProcessor(const FPArgs& args,
                                                                    TileMode tileModeX,
                                                                    TileMode tileModeY,
                                                                    const SamplingOptions& sampling,
                                                                    const Matrix* uvMatrix) const {
  auto matrix = concatUVMatrix(uvMatrix);
  auto drawBounds = args.drawRect;
  if (matrix) {
    matrix->mapRect(&drawBounds);
  }
  if (bounds.contains(drawBounds)) {
    return FragmentProcessor::Make(source, args, tileModeX, tileModeY, sampling, AddressOf(matrix));
  }
  auto mipmapped = source->hasMipmaps() && NeedMipmaps(sampling, args.viewMatrix, uvMatrix);
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
  auto textureProxy = lockTextureProxy(tpArgs, sampling);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return TiledTextureEffect::Make(textureProxy, tileModeX, tileModeY, sampling, uvMatrix);
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
}  // namespace tgfx
