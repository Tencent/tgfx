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
#include "ReadPixelsFromCanvasImage.h"
#include "WebMask.h"

using namespace emscripten;

namespace tgfx {
std::shared_ptr<PathRasterizer> PathRasterizer::Make(int width, int height,
                                                     std::shared_ptr<Shape> shape, bool antiAlias,
                                                     bool needsGammaCorrection) {
  if (shape == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::make_shared<WebPathRasterizer>(width, height, std::move(shape), antiAlias,
                                             needsGammaCorrection);
}

static void Iterator(PathVerb verb, const Point points[4], void* info) {
  auto path2D = reinterpret_cast<val*>(info);
  switch (verb) {
    case PathVerb::Move:
      path2D->call<void>("moveTo", points[0].x, points[0].y);
      break;
    case PathVerb::Line:
      path2D->call<void>("lineTo", points[1].x, points[1].y);
      break;
    case PathVerb::Quad:
      path2D->call<void>("quadraticCurveTo", points[1].x, points[1].y, points[2].x, points[2].y);
      break;
    case PathVerb::Cubic:
      path2D->call<void>("bezierCurveTo", points[1].x, points[1].y, points[2].x, points[2].y,
                         points[3].x, points[3].y);
      break;
    case PathVerb::Close:
      path2D->call<void>("closePath");
      break;
  }
}

bool WebPathRasterizer::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstPixels == nullptr || dstInfo.isEmpty()) {
    return false;
  }
  auto path = shape->getPath();
  if (path.isEmpty()) {
    return false;
  }
  auto path2DClass = val::global("Path2D");
  if (!path2DClass.as<bool>()) {
    return false;
  }
  auto targetInfo = dstInfo.makeIntersect(0, 0, width(), height());
  auto canvas = val::module_property("tgfx").call<val>("createCanvas2D", targetInfo.width(),
                                                       targetInfo.height());
  if (!canvas.as<bool>()) {
    return false;
  }
  auto webMaskClass = val::module_property("WebMask");
  if (!webMaskClass.as<bool>()) {
    return false;
  }
  auto webMask = webMaskClass.call<val>("create", canvas);
  if (!webMask.as<bool>()) {
    return false;
  }
  auto path2D = path2DClass.new_();
  path.decompose(Iterator, &path2D);
  webMask.call<void>("fillPath", path2D, path.getFillType());
  auto imageData = webMask.call<val>("getImageData");
  if (!imageData.as<bool>()) {
    return false;
  }
  return ReadPixelsFromCanvasImage(imageData, targetInfo, dstPixels);
}
}  // namespace tgfx
