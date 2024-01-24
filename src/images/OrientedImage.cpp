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

static Matrix OrientationToMatrix(Orientation orientation) {
  switch (orientation) {
    case Orientation::TopRight:
      return TopRightMatrix;
    case Orientation::BottomRight:
      return BottomRightMatrix;
    case Orientation::BottomLeft:
      return BottomLeftMatrix;
    case Orientation::LeftTop:
      return LeftTopMatrix;
    case Orientation::RightTop:
      return RightTopMatrix;
    case Orientation::RightBottom:
      return RightBottomMatrix;
    case Orientation::LeftBottom:
      return LeftBottomMatrix;
    default:
      break;
  }
  return TopLeftMatrix;
}

std::shared_ptr<Image> OrientedImage::MakeFrom(std::shared_ptr<Image> source,
                                               Orientation orientation) {
  if (source == nullptr) {
    return nullptr;
  }
  if (orientation == Orientation::TopLeft) {
    return source;
  }
  auto image = std::shared_ptr<OrientedImage>(new OrientedImage(std::move(source), orientation));
  image->weakThis = image;
  return image;
}

OrientedImage::OrientedImage(std::shared_ptr<Image> source, Orientation orientation)
    : NestedImage(std::move(source)), orientation(orientation) {
}

static bool NeedSwapWH(Orientation orientation) {
  switch (orientation) {
    case Orientation::LeftTop:
    case Orientation::RightTop:
    case Orientation::RightBottom:
    case Orientation::LeftBottom:
      return true;
    default:
      break;
  }
  return false;
}

int OrientedImage::width() const {
  return NeedSwapWH(orientation) ? source->height() : source->width();
}

int OrientedImage::height() const {
  return NeedSwapWH(orientation) ? source->width() : source->height();
}

std::shared_ptr<Image> OrientedImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return OrientedImage::MakeFrom(std::move(newSource), orientation);
}

std::shared_ptr<Image> OrientedImage::onMakeMipMapped() const {
  auto newSource = source->makeMipMapped();
  if (newSource == source) {
    return nullptr;
  }
  return onCloneWith(std::move(newSource));
}

std::shared_ptr<Image> OrientedImage::onMakeSubset(const Rect& subset) const {
  return SubsetImage::MakeFrom(source, orientation, subset);
}

std::shared_ptr<Image> OrientedImage::onMakeOriented(Orientation newOrientation) const {
  newOrientation = concatOrientation(newOrientation);
  if (newOrientation == Orientation::TopLeft) {
    return source;
  }
  return OrientedImage::MakeFrom(source, newOrientation);
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
  auto matrix = OrientationToMatrix(orientation, source->width(), source->height());
  matrix.invert(&matrix);
  return matrix;
}

Orientation OrientedImage::concatOrientation(Orientation newOrientation) const {
  auto oldMatrix = OrientationToMatrix(orientation);
  auto newMatrix = OrientationToMatrix(newOrientation);
  oldMatrix.postConcat(newMatrix);
  if (oldMatrix == TopRightMatrix) {
    return Orientation::TopRight;
  }
  if (oldMatrix == BottomRightMatrix) {
    return Orientation::BottomRight;
  }
  if (oldMatrix == BottomLeftMatrix) {
    return Orientation::BottomLeft;
  }
  if (oldMatrix == LeftTopMatrix) {
    return Orientation::LeftTop;
  }
  if (oldMatrix == RightTopMatrix) {
    return Orientation::RightTop;
  }
  if (oldMatrix == RightBottomMatrix) {
    return Orientation::RightBottom;
  }
  if (oldMatrix == LeftBottomMatrix) {
    return Orientation::LeftBottom;
  }
  return Orientation::TopLeft;
}
}  // namespace tgfx
