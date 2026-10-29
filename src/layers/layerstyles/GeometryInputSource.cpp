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

#include <memory>
#include <utility>
#include "layers/GeometryShapeContext.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Picture.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

std::shared_ptr<GeometryInputSource> GeometryInputSource::Make(
    std::shared_ptr<Picture> picture, float contentScale, std::shared_ptr<ColorSpace> colorSpace) {
  if (picture == nullptr) {
    return nullptr;
  }
  // Read the shape before the conversion consumes the picture, so both describe one recording.
  auto shape = GeometryShapeContext::Extract(picture, contentScale);
  Point offset = Point::Zero();
  auto image = ToImageWithOffset(std::move(picture), &offset, nullptr, std::move(colorSpace));
  if (image == nullptr) {
    return nullptr;
  }
  // A private constructor is only reachable through the class' own access, so the source is built
  // here instead of through make_shared.
  return std::shared_ptr<GeometryInputSource>(
      new GeometryInputSource(std::move(image), offset, std::move(shape)));
}

std::shared_ptr<GeometryInputSource> GeometryInputSource::makeRasterized() const {
  if (image() == nullptr) {
    return nullptr;
  }
  // Only the raster changes; the offset and the shape describe the same recording as before.
  return std::shared_ptr<GeometryInputSource>(
      new GeometryInputSource(image()->makeRasterized(), imageOffset(), shape()));
}

}  // namespace tgfx
