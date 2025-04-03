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

#include "TextAtlas.h"
#include <unordered_set>
#include "Rasterizer.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Mask.h"
#include "tgfx/core/Paint.h"

namespace tgfx {
class Atlas {
 public:
  static std::unique_ptr<Atlas> Make(std::shared_ptr<GlyphRunList> glyphRunList, int maxPageSize,
                                     float scale, const Stroke* stroke);
  bool getLocator(GlyphID glyphId, AtlasLocator* locator) const;

  size_t memoryUsage() const;

 private:
  Atlas(std::vector<std::shared_ptr<Image>> images,
        std::unordered_map<GlyphID, AtlasLocator> glyphLocators)
      : images(std::move(images)), glyphLocators(std::move(glyphLocators)) {
  }

  std::vector<std::shared_ptr<Image>> images;
  std::unordered_map<GlyphID, AtlasLocator> glyphLocators;

  friend class TextAtlas;
};

size_t Atlas::memoryUsage() const {
  if (images.empty()) {
    return 0;
  }
  int usage = 0;
  for (auto& image : images) {
    usage += image->width() * image->height();
  }
  auto bytesPerPixels = images[0]->isAlphaOnly() ? 1 : 4;
  return (size_t)(usage * bytesPerPixels);
}

struct AtlasTextRun {
  Paint paint;
  std::shared_ptr<GlyphFace> glyphFace = nullptr;
  std::vector<GlyphID> glyphIDs;
  std::vector<Point> positions;
};

static AtlasTextRun CreateTextRun(std::shared_ptr<GlyphFace> glyphFace) {
  AtlasTextRun textRun;
  textRun.glyphFace = std::move(glyphFace);
  return textRun;
}

struct Page {
  std::vector<AtlasTextRun> textRuns;
  int width = 0;
  int height = 0;
  std::unordered_map<GlyphID, AtlasLocator> locators;
};

static constexpr int kDefaultPadding = 2;

class RectanglePack {
 public:
  explicit RectanglePack(int padding = kDefaultPadding) : padding(padding) {
    reset();
  }

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  Point addRect(int w, int h) {
    w += padding;
    h += padding;
    auto area = (_width - x) * (_height - y);
    if ((x + w - _width) * y > area || (y + h - _height) * x > area) {
      if (_width <= _height) {
        x = _width;
        y = padding;
        _width += w;
      } else {
        x = padding;
        y = _height;
        _height += h;
      }
    }
    auto point = Point::Make(x, y);
    if (x + w - _width < y + h - _height) {
      x += w;
      _width = std::max(_width, x);
      _height = std::max(_height, y + h);
    } else {
      y += h;
      _height = std::max(_height, y);
      _width = std::max(_width, x + w);
    }
    return point;
  }

  void reset() {
    _width = padding;
    _height = padding;
    x = padding;
    y = padding;
  }

 private:
  int padding = kDefaultPadding;
  int _width = 0;
  int _height = 0;
  int x = 0;
  int y = 0;
};

static std::vector<Page> CreatePages(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                     int maxPageSize, float scale, const Stroke* stroke) {
  std::vector<Page> pages;
  std::vector<AtlasTextRun> textRuns;
  std::vector<size_t> glyphFaceIds;
  int padding = kDefaultPadding;
  RectanglePack pack(padding);
  Page page;
  std::unordered_set<GlyphID> ids;
  for (const auto& glyphRun : glyphRunList->glyphRuns()) {
    const auto& positions = glyphRun.positions;
    const auto& glyphIds = glyphRun.glyphs;
    const auto glypyCount = positions.size();
    Font font;
    size_t glyphFaceId = 0;
    auto glyphFace = glyphRun.glyphFace;
    auto hasScale = !FloatNearlyEqual(scale, 1.0f);
    if (hasScale) {
      glyphFace = glyphFace->makeScaled(scale);
    }
    if (glyphFace->asFont(&font)) {
      glyphFaceId = font.getTypeface()->uniqueID();
    }

    auto finalScale = stroke ? scale : 1.0f;

    for (size_t i = 0; i < glypyCount; ++i) {
      if (ids.find(glyphIds[i]) != ids.end()) {
        continue;
      }

      auto iter = std::find(glyphFaceIds.begin(), glyphFaceIds.end(), glyphFaceId);
      AtlasTextRun* textRun;
      if (iter == glyphFaceIds.end()) {
        glyphFaceIds.push_back(glyphFaceId);
        textRuns.push_back(CreateTextRun(stroke ? glyphRun.glyphFace : glyphFace));
        textRun = &textRuns.back();
      } else {
        size_t index = size_t(iter - glyphFaceIds.begin());
        textRun = &textRuns[index];
      }

      auto bounds = glyphFace->getBounds(glyphIds[i]);
      if (stroke) {
        bounds.scale(1 / scale, 1 / scale);
        stroke->applyToBounds(&bounds);
        bounds.scale(scale, scale);
      }

      bounds.roundOut();
      int width = static_cast<int>(bounds.width());
      int height = static_cast<int>(bounds.height());
      auto packWidth = pack.width();
      auto packHeight = pack.height();
      auto point = pack.addRect(width, height);
      if (pack.width() > maxPageSize || pack.height() > maxPageSize) {
        page.textRuns = std::move(textRuns);
        page.width = packWidth;
        page.height = packHeight;
        pages.push_back(std::move(page));
        glyphFaceIds.clear();
        textRuns.clear();
        page = {};
        pack.reset();
        point = pack.addRect(packWidth, packHeight);
      }
      textRun->glyphIDs.emplace_back(glyphIds[i]);
      textRun->positions.push_back(
          {(-bounds.x() + point.x) / finalScale, (-bounds.y() + point.y) / finalScale});
      AtlasLocator locator;
      locator.imageIndex = pages.size();
      locator.location =
          Rect::MakeXYWH(point.x, point.y, static_cast<float>(width), static_cast<float>(height));
      locator.glyphBounds = bounds;
      page.locators[glyphIds[i]] = locator;
      ids.insert(glyphIds[i]);
    }

    page.textRuns = std::move(textRuns);
    page.width = pack.width();
    page.height = pack.height();
    pages.push_back(std::move(page));
  }
  return pages;
}

std::unique_ptr<Atlas> Atlas::Make(std::shared_ptr<GlyphRunList> glyphRunList, int maxPageSize,
                                   float scale, const Stroke* stroke) {
  if (glyphRunList == nullptr) {
    return nullptr;
  }
  auto pages = CreatePages(glyphRunList, maxPageSize, scale, stroke);
  if (pages.empty()) {
    return nullptr;
  }
  std::unordered_map<GlyphID, AtlasLocator> glyphLocators;
  for (const auto& page : pages) {
    glyphLocators.insert(page.locators.begin(), page.locators.end());
  }
  std::vector<std::shared_ptr<Image>> images;
  for (const auto& page : pages) {
    std::vector<GlyphRun> glyphRuns;
    for (auto& textRun : page.textRuns) {
      Font font;
      if (!textRun.glyphFace->asFont(&font)) {
        continue;
      }
      GlyphRun glyphRun{textRun.glyphFace, std::move(textRun.glyphIDs),
                        std::move(textRun.positions)};
      glyphRuns.emplace_back(std::move(glyphRun));
    }
    auto rasterizeMatrix = stroke ? Matrix::MakeScale(scale) : Matrix::I();
    auto atlasGlyphRunList = std::make_shared<GlyphRunList>(std::move(glyphRuns));
    auto rasterizer = Rasterizer::MakeFrom(page.width, page.height, std::move(atlasGlyphRunList),
                                           true, rasterizeMatrix, stroke);
    auto maskImage = Image::MakeFrom(std::move(rasterizer));
    images.push_back(maskImage);
  }
  return std::unique_ptr<Atlas>(new Atlas(std::move(images), std::move(glyphLocators)));
}

bool Atlas::getLocator(GlyphID glyphId, AtlasLocator* locator) const {
  auto iter = glyphLocators.find(glyphId);
  if (iter == glyphLocators.end()) {
    return false;
  }
  if (locator) {
    *locator = iter->second;
  }
  return true;
}

//static constexpr float kMaxAtlasFontSize = 256.f;
static constexpr int kMaxAtlasSize = 4096;

std::unique_ptr<TextAtlas> TextAtlas::Make(const Context* context,
                                           std::shared_ptr<GlyphRunList> glyphRunList, float scale,
                                           const Stroke* stoke) {
  if (context == nullptr) {
    return nullptr;
  }
  auto maxPageSize = std::min(kMaxAtlasSize, context->caps()->maxTextureSize);
  auto maskAtlas = Atlas::Make(std::move(glyphRunList), maxPageSize, scale, stoke).release();
  if (maskAtlas == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<TextAtlas>(new TextAtlas(maskAtlas));
}

TextAtlas::~TextAtlas() {
  delete maskAtlas;
}

bool TextAtlas::getLocator(GlyphID glyphId, AtlasLocator* locator) const {
  return maskAtlas->getLocator(glyphId, locator);
}

std::shared_ptr<Image> TextAtlas::getAtlasImage(size_t imageIndex) const {
  if (imageIndex < maskAtlas->images.size()) {
    return maskAtlas->images[imageIndex];
  }
  return nullptr;
}

size_t TextAtlas::memoryUsage() const {
  size_t usage = 0;
  if (maskAtlas) {
    usage += maskAtlas->memoryUsage();
  }
  return usage;
}

}  // namespace tgfx
