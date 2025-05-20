/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "WebPathRasterizer.h"
#include <emscripten/val.h>
#include "WebMask.h"
#include "tgfx/core/Font.h"

using namespace emscripten;

namespace tgfx {
std::shared_ptr<PathRasterizer> PathRasterizer::Make(std::shared_ptr<Shape> shape, bool antiAlias,
                                                     bool needsGammaCorrection) {
  auto bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    return nullptr;
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  return std::make_shared<WebPathRasterizer>(width, height, shape, antiAlias, needsGammaCorrection);
}

bool WebPathRasterizer::readPixels(const ImageInfo&, void*) const {
  return false;
}

}  // namespace tgfx
