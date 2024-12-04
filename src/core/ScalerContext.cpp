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

#include "ScalerContext.h"

namespace tgfx {
class EmptyScalerContext : public ScalerContext {
 public:
  explicit EmptyScalerContext(float size) : ScalerContext(nullptr, size) {
  }

  FontMetrics getFontMetrics() const override {
    return {};
  }

  Rect getBounds(GlyphID, bool, bool) const override {
    return Rect::MakeEmpty();
  }

  float getAdvance(GlyphID, bool) const override {
    return 0.0f;
  }

  Point getVerticalOffset(GlyphID) const override {
    return Point::Zero();
  }

  bool generatePath(GlyphID, bool, bool, Path*) const override {
    return false;
  }

  Size getImageTransform(GlyphID, Matrix*) const override {
    return Size::MakeEmpty();
  }

  std::shared_ptr<ImageBuffer> generateImage(GlyphID, bool) const override {
    return nullptr;
  }
};

std::shared_ptr<ScalerContext> ScalerContext::MakeEmpty(float size) {
  if (size < 0) {
    size = 0;
  }
  if (size == 0) {
    static auto EmptyContext = std::make_shared<EmptyScalerContext>(0.0f);
    return EmptyContext;
  }
  return std::make_shared<EmptyScalerContext>(size);
}

std::shared_ptr<ScalerContext> ScalerContext::Make(std::shared_ptr<Typeface> typeface, float size) {
  if (typeface == nullptr || typeface->glyphsCount() <= 0 || size < 0.0f) {
    return MakeEmpty(size);
  }
  std::lock_guard<std::mutex> autoLock(typeface->locker);
  auto& scalerContexts = typeface->scalerContexts;
  auto result = scalerContexts.find(size);
  if (result != scalerContexts.end()) {
    auto context = result->second.lock();
    if (context != nullptr) {
      return context;
    }
  }

  auto context = typeface->createScalerContext(size);
  if (context == nullptr) {
    return MakeEmpty(size);
  }
  scalerContexts[size] = context;
  return context;
}

ScalerContext::ScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : typeface(std::move(typeface)), textSize(size) {
}
}  // namespace tgfx
