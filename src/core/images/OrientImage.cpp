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
#include "core/images/RasterizedImage.h"
#include "core/images/SubsetImage.h"
#include "core/utils/AddressOf.h"
#include "gpu/ops/DrawOp.h"

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
  TRACE_EVENT;
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
  TRACE_EVENT;
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
  TRACE_EVENT;
  return MakeFrom(std::move(newSource), orientation);
}

std::shared_ptr<Image> OrientImage::onMakeOriented(Orientation newOrientation) const {
  TRACE_EVENT;
  newOrientation = concatOrientation(newOrientation);
  if (newOrientation == Orientation::TopLeft) {
    return source;
  }
  return MakeFrom(source, newOrientation);
}

std::unique_ptr<FragmentProcessor> OrientImage::asFragmentProcessor(const FPArgs& args,
                                                                    TileMode tileModeX,
                                                                    TileMode tileModeY,
                                                                    const SamplingOptions& sampling,
                                                                    const Matrix* uvMatrix) const {
  TRACE_EVENT;
  std::optional<Matrix> matrix = std::nullopt;
  if (orientation != Orientation::TopLeft) {
    matrix = OrientationToMatrix(orientation, source->width(), source->height());
    matrix->invert(AddressOf(matrix));
  }
  if (uvMatrix) {
    if (matrix) {
      matrix->preConcat(*uvMatrix);
    } else {
      matrix = *uvMatrix;
    }
  }
  if (OrientationSwapsWidthHeight(orientation)) {
    std::swap(tileModeX, tileModeY);
  }
  return FragmentProcessor::Make(source, args, tileModeX, tileModeY, sampling, AddressOf(matrix));
}

Orientation OrientImage::concatOrientation(Orientation newOrientation) const {
  TRACE_EVENT;
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
