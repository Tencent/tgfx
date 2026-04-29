/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/vectors/ImagePattern.h"
#include <algorithm>
#include "core/utils/Log.h"

namespace tgfx {
std::shared_ptr<ImagePattern> ImagePattern::Make(std::shared_ptr<Image> image, TileMode tileModeX,
                                                 TileMode tileModeY,
                                                 const SamplingOptions& sampling) {
  if (image == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ImagePattern>(
      new ImagePattern(std::move(image), tileModeX, tileModeY, sampling));
}

ImagePattern::ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
                           const SamplingOptions& sampling)
    : _image(std::move(image)), _tileModeX(tileModeX), _tileModeY(tileModeY), _sampling(sampling) {
}

void ImagePattern::setMatrix(const Matrix& matrix) {
  if (_matrix == matrix) {
    return;
  }
  _matrix = matrix;
  invalidateContent();
}

void ImagePattern::setScaleMode(ScaleMode mode) {
  if (_scaleMode == mode) {
    return;
  }
  _scaleMode = mode;
  invalidateContent();
}

std::shared_ptr<Shader> ImagePattern::getShader() const {
  auto shader = Shader::MakeImageShader(_image, _tileModeX, _tileModeY, _sampling);
  if (shader == nullptr || _matrix.isIdentity()) {
    return shader;
  }
  return shader->makeWithMatrix(_matrix);
}

Matrix ImagePattern::getFitMatrix(const Rect& bounds) const {
  DEBUG_ASSERT(_scaleMode != ScaleMode::None);
  if (bounds.isEmpty()) {
    return Matrix::I();
  }
  auto imageRect =
      Rect::MakeWH(static_cast<float>(_image->width()), static_cast<float>(_image->height()));
  if (imageRect.isEmpty()) {
    return Matrix::I();
  }
  // Fit is applied to the user-transformed image rect so the matrix set via setMatrix() is
  // honored before the scale-mode fit maps it into the geometry bounds.
  auto transformed = _matrix.mapRect(imageRect);
  if (transformed.isEmpty()) {
    return Matrix::I();
  }
  float sx = bounds.width() / transformed.width();
  float sy = bounds.height() / transformed.height();
  switch (_scaleMode) {
    case ScaleMode::None:
      // Unreachable in practice: fitsToGeometry() returns false for ScaleMode::None so callers
      // skip getFitMatrix(), and the DEBUG_ASSERT above catches misuse in debug builds. Returning
      // identity here is only a safe fallback for release builds.
      break;
    case ScaleMode::Stretch: {
      auto matrix = Matrix::MakeTrans(-transformed.left, -transformed.top);
      matrix.postScale(sx, sy);
      matrix.postTranslate(bounds.left, bounds.top);
      return matrix;
    }
    case ScaleMode::LetterBox: {
      float scale = std::min(sx, sy);
      float tx = bounds.left + (bounds.width() - transformed.width() * scale) * 0.5f;
      float ty = bounds.top + (bounds.height() - transformed.height() * scale) * 0.5f;
      auto matrix = Matrix::MakeTrans(-transformed.left, -transformed.top);
      matrix.postScale(scale, scale);
      matrix.postTranslate(tx, ty);
      return matrix;
    }
    case ScaleMode::Zoom: {
      float scale = std::max(sx, sy);
      float tx = bounds.left + (bounds.width() - transformed.width() * scale) * 0.5f;
      float ty = bounds.top + (bounds.height() - transformed.height() * scale) * 0.5f;
      auto matrix = Matrix::MakeTrans(-transformed.left, -transformed.top);
      matrix.postScale(scale, scale);
      matrix.postTranslate(tx, ty);
      return matrix;
    }
  }
  return Matrix::I();
}

}  // namespace tgfx
