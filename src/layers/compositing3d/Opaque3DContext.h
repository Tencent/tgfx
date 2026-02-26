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

#pragma once

#include <stack>
#include <vector>
#include "Layer3DContext.h"
#include "layers/OpaqueContext.h"

namespace tgfx {

struct OpaqueImageEntry {
  std::shared_ptr<Image> image = nullptr;
  Matrix3D transform = {};
};

/**
 * Simplified 3D context for opaque content/contour rendering. Unlike Render3DContext, this class
 * does not perform complex depth sorting or clipping. It simply applies 3D transforms to each
 * layer and draws them in order.
 */
class Opaque3DContext : public Layer3DContext {
 public:
  Opaque3DContext(const Rect& renderRect, float contentScale,
                  std::shared_ptr<ColorSpace> colorSpace);

  OpaqueContext* currentOpaqueContext() override;
  void finishAndDrawTo(Canvas* canvas, bool antialiasing) override;

 protected:
  Canvas* onBeginRecording() override;
  std::shared_ptr<Picture> onFinishRecording() override;
  void onImageReady(std::shared_ptr<Image> image, const Matrix3D& imageTransform,
                    const Point& pictureOffset, int depth, bool antialiasing) override;

 private:
  std::stack<OpaqueContext> _opaqueStack = {};
  std::vector<OpaqueImageEntry> _opaqueImages = {};
};

}  // namespace tgfx
