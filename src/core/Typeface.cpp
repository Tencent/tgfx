/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Typeface.h"
#include <vector>
#include "core/AdvancedTypefaceInfo.h"
#include "core/ScalerContext.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/UTF.h"

namespace tgfx {
class EmptyTypeface : public Typeface {
 public:
  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override {
    return "";
  }

  std::string fontStyle() const override {
    return "";
  }

  size_t glyphsCount() const override {
    return 0;
  }

  int unitsPerEm() const override {
    return 0;
  }

  bool hasColor() const override {
    return false;
  }

  bool hasOutlines() const override {
    return false;
  }

  GlyphID getGlyphID(Unichar) const override {
    return 0;
  }

  std::unique_ptr<Stream> openStream() const override {
    return nullptr;
  }

  std::shared_ptr<Data> copyTableData(FontTableTag) const override {
    return nullptr;
  }

 protected:
  AdvancedTypefaceInfo getAdvancedInfo() const override {
    return {};
  }

 private:
  std::shared_ptr<ScalerContext> onCreateScalerContext(float size) const override {
    return ScalerContext::MakeEmpty(size);
  }

  uint32_t _uniqueID = UniqueID::Next();
};

std::shared_ptr<Typeface> Typeface::MakeEmpty() {
  static auto emptyTypeface = std::make_shared<EmptyTypeface>();
  return emptyTypeface;
}

GlyphID Typeface::getGlyphID(const std::string& name) const {
  if (name.empty()) {
    return 0;
  }
  auto count = UTF::CountUTF8(name.c_str(), name.size());
  if (count <= 0) {
    return 0;
  }
  const char* start = name.data();
  auto unichar = UTF::NextUTF8(&start, start + name.size());
  return getGlyphID(unichar);
}

bool Typeface::isCustom() const {
  return false;
}

const std::vector<Unichar>& Typeface::getGlyphToUnicodeMap() const {
  std::call_once(glyphToUnicodeOnceFlag,
                 [this] { glyphToUnicodeCache = onCreateGlyphToUnicodeMap(); });
  return glyphToUnicodeCache;
}

AdvancedTypefaceInfo Typeface::getAdvancedInfo() const {
  return {};
}

std::shared_ptr<ScalerContext> Typeface::getScalerContext(float size) {
  if (size <= 0.0f) {
    return ScalerContext::MakeEmpty(size);
  }
  std::lock_guard<std::mutex> autoLock(locker);
  auto result = scalerContexts.find(size);
  if (result != scalerContexts.end()) {
    auto context = result->second.lock();
    if (context != nullptr) {
      return context;
    }
  }
  auto context = onCreateScalerContext(size);
  if (context == nullptr) {
    return ScalerContext::MakeEmpty(size);
  }
  scalerContexts[size] = context;
  return context;
}

Rect Typeface::getBounds() const {
  std::call_once(boundsOnceFlag, [this] {
    if (!onComputeBounds(&bounds)) {
      bounds.setEmpty();
    }
  });
  return bounds;
}

bool Typeface::onComputeBounds(Rect* bounds) const {
  if (hasColor()) {
    // The bounds are only valid for the default outline variation
    // Bitmaps may be any size and placed at any offset.
    return false;
  }
  constexpr float TextSize = 2048.f;
  constexpr float InvTextSize = 1.0f / TextSize;
  auto scaleContext = onCreateScalerContext(TextSize);
  if (scaleContext == nullptr) {
    return false;
  }
  auto metrics = scaleContext->getFontMetrics();
  //Check if the bounds is empty
  if (metrics.xMin >= metrics.xMax || metrics.top >= metrics.bottom) {
    return false;
  }
  bounds->setLTRB(metrics.xMin * InvTextSize, metrics.top * InvTextSize, metrics.xMax * InvTextSize,
                  metrics.bottom * InvTextSize);
  return true;
}

std::vector<Unichar> Typeface::onCreateGlyphToUnicodeMap() const {
  return {};
}
}  // namespace tgfx
