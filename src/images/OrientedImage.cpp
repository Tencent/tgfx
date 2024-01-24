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

#include "OrientedImage.h"
#include "images/SubsetImage.h"

namespace tgfx {
static const auto TopLeftMatrix = Matrix::I();
static const auto TopRightMatrix = Matrix::MakeAll(-1, 0, 1, 0, 1, 0);
static const auto BottomRightMatrix = Matrix::MakeAll(-1, 0, 1, 0, -1, 1);
static const auto BottomLeftMatrix = Matrix::MakeAll(1, 0, 0, 0, -1, 1);
static const auto LeftTopMatrix = Matrix::MakeAll(0, 1, 0, 1, 0, 0);
static const auto RightTopMatrix = Matrix::MakeAll(0, -1, 1, 1, 0, 0);
static const auto RightBottomMatrix = Matrix::MakeAll(0, -1, 1, -1, 0, 1);
static const auto LeftBottomMatrix = Matrix::MakeAll(0, 1, 0, -1, 0, 1);

static Matrix OriginToMatrix(EncodedOrigin origin) {
  switch (origin) {
    case EncodedOrigin::TopRight:
      return TopRightMatrix;
    case EncodedOrigin::BottomRight:
      return BottomRightMatrix;
    case EncodedOrigin::BottomLeft:
      return BottomLeftMatrix;
    case EncodedOrigin::LeftTop:
      return LeftTopMatrix;
    case EncodedOrigin::RightTop:
      return RightTopMatrix;
    case EncodedOrigin::RightBottom:
      return RightBottomMatrix;
    case EncodedOrigin::LeftBottom:
      return LeftBottomMatrix;
    default:
      break;
  }
  return TopLeftMatrix;
}

std::shared_ptr<Image> OrientedImage::MakeFrom(std::shared_ptr<Image> source,
                                               EncodedOrigin origin) {
  if (source == nullptr) {
    return nullptr;
  }
  if (origin == EncodedOrigin::TopLeft) {
    return source;
  }
  auto image = std::shared_ptr<OrientedImage>(new OrientedImage(std::move(source), origin));
  image->weakThis = image;
  return image;
}

OrientedImage::OrientedImage(std::shared_ptr<Image> source, EncodedOrigin origin)
    : NestedImage(std::move(source)), origin(origin) {
}

static bool NeedSwapWH(EncodedOrigin origin) {
  switch (origin) {
    case EncodedOrigin::LeftTop:
    case EncodedOrigin::RightTop:
    case EncodedOrigin::RightBottom:
    case EncodedOrigin::LeftBottom:
      return true;
    default:
      break;
  }
  return false;
}

int OrientedImage::width() const {
  return NeedSwapWH(origin) ? source->height() : source->width();
}

int OrientedImage::height() const {
  return NeedSwapWH(origin) ? source->width() : source->height();
}

std::shared_ptr<Image> OrientedImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return OrientedImage::MakeFrom(std::move(newSource), origin);
}

std::shared_ptr<Image> OrientedImage::onMakeMipMapped() const {
  auto newSource = source->makeMipMapped();
  if (newSource == source) {
    return nullptr;
  }
  return onCloneWith(std::move(newSource));
}

std::shared_ptr<Image> OrientedImage::onMakeSubset(const Rect& subset) const {
  return SubsetImage::MakeFrom(source, origin, subset);
}

std::shared_ptr<Image> OrientedImage::onApplyOrigin(EncodedOrigin newOrigin) const {
  newOrigin = concatOrigin(newOrigin);
  if (newOrigin == EncodedOrigin::TopLeft) {
    return source;
  }
  return OrientedImage::MakeFrom(source, newOrigin);
}

std::unique_ptr<FragmentProcessor> OrientedImage::asFragmentProcessor(
    Context* context, TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling,
    const Matrix* localMatrix, uint32_t renderFlags) {
  auto totalMatrix = computeLocalMatrix();
  if (localMatrix != nullptr) {
    totalMatrix.preConcat(*localMatrix);
  }
  return FragmentProcessor::MakeImage(context, source, tileModeX, tileModeY, sampling, &totalMatrix,
                                      renderFlags);
}

Matrix OrientedImage::computeLocalMatrix() const {
  auto matrix = EncodedOriginToMatrix(origin, source->width(), source->height());
  matrix.invert(&matrix);
  return matrix;
}

EncodedOrigin OrientedImage::concatOrigin(EncodedOrigin newOrigin) const {
  auto oldMatrix = OriginToMatrix(origin);
  auto newMatrix = OriginToMatrix(newOrigin);
  oldMatrix.postConcat(newMatrix);
  if (oldMatrix == TopRightMatrix) {
    return EncodedOrigin::TopRight;
  }
  if (oldMatrix == BottomRightMatrix) {
    return EncodedOrigin::BottomRight;
  }
  if (oldMatrix == BottomLeftMatrix) {
    return EncodedOrigin::BottomLeft;
  }
  if (oldMatrix == LeftTopMatrix) {
    return EncodedOrigin::LeftTop;
  }
  if (oldMatrix == RightTopMatrix) {
    return EncodedOrigin::RightTop;
  }
  if (oldMatrix == RightBottomMatrix) {
    return EncodedOrigin::RightBottom;
  }
  if (oldMatrix == LeftBottomMatrix) {
    return EncodedOrigin::LeftBottom;
  }
  return EncodedOrigin::TopLeft;
}
}  // namespace tgfx
