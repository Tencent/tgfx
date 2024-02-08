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

#include "OrientImage.h"
#include "gpu/ops/DrawOp.h"
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

std::shared_ptr<Image> OrientImage::MakeFrom(std::shared_ptr<Image> source,
                                             Orientation orientation) {
  if (source == nullptr) {
    return nullptr;
  }
  if (orientation == Orientation::TopLeft) {
    return source;
  }
  auto image = std::shared_ptr<OrientImage>(new OrientImage(std::move(source), orientation));
  image->weakThis = image;
  return image;
}

OrientImage::OrientImage(std::shared_ptr<Image> source, Orientation orientation)
    : TransformImage(std::move(source)), orientation(orientation) {
}

int OrientImage::width() const {
  return OrientationSwapsWidthHeight(orientation) ? source->height() : source->width();
}

int OrientImage::height() const {
  return OrientationSwapsWidthHeight(orientation) ? source->width() : source->height();
}

std::shared_ptr<Image> OrientImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return OrientImage::MakeFrom(std::move(newSource), orientation);
}

std::shared_ptr<Image> OrientImage::onMakeSubset(const Rect& subset) const {
  return SubsetImage::MakeFrom(source, orientation, subset);
}

std::shared_ptr<Image> OrientImage::onMakeOriented(Orientation newOrientation) const {
  newOrientation = concatOrientation(newOrientation);
  if (newOrientation == Orientation::TopLeft) {
    return source;
  }
  return OrientImage::MakeFrom(source, newOrientation);
}

std::unique_ptr<DrawOp> OrientImage::makeDrawOp(const DrawArgs& args, const Matrix* localMatrix,
                                                TileMode tileModeX, TileMode tileModeY) const {
  auto matrix = concatLocalMatrix(localMatrix);
  return source->makeDrawOp(args, AddressOf(matrix), tileModeX, tileModeY);
}

std::unique_ptr<FragmentProcessor> OrientImage::asFragmentProcessor(const DrawArgs& args,
                                                                    const Matrix* localMatrix,
                                                                    TileMode tileModeX,
                                                                    TileMode tileModeY) const {
  auto matrix = concatLocalMatrix(localMatrix);
  return source->asFragmentProcessor(args, AddressOf(matrix), tileModeX, tileModeY);
}

std::optional<Matrix> OrientImage::concatLocalMatrix(const Matrix* localMatrix) const {
  std::optional<Matrix> matrix = std::nullopt;
  if (orientation != Orientation::TopLeft) {
    matrix = OrientationToMatrix(orientation, source->width(), source->height());
    matrix->invert(AddressOf(matrix));
  }
  if (localMatrix != nullptr) {
    if (matrix) {
      matrix->preConcat(*localMatrix);
    } else {
      matrix = *localMatrix;
    }
  }
  return matrix;
}

Orientation OrientImage::concatOrientation(Orientation newOrientation) const {
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
