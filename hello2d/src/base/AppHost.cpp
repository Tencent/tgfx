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

#include "hello2d/AppHost.h"
#include "hello2d/SampleBuilder.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/platform/Print.h"

namespace hello2d {
AppHost::AppHost(int width, int height, float density)
    : _width(width), _height(height), _density(density) {
}

bool AppHost::isDirty() const {
  return _dirty;
}

void AppHost::markDirty() const {
  _dirty = true;
}

void AppHost::resetDirty() const {
  _dirty = false;
}

void AppHost::setTileMode() const {
  _isTileMode = false;
}

std::shared_ptr<tgfx::Image> AppHost::getImage(const std::string& name) const {
  auto result = images.find(name);
  if (result != images.end()) {
    return result->second;
  }
  return nullptr;
}

std::shared_ptr<tgfx::Typeface> AppHost::getTypeface(const std::string& name) const {
  auto result = typefaces.find(name);
  if (result != typefaces.end()) {
    return result->second;
  }
  return nullptr;
}

bool AppHost::updateScreen(int width, int height, float density) {
  markDirty();
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (density < 1.0) {
    return false;
  }
  if (width == _width && height == _height && density == _density) {
    return false;
  }
  _width = width;
  _height = height;
  _density = density;
  updateRootMatrix();
  return true;
}

bool AppHost::updateZoomAndOffset(float zoomScale, const tgfx::Point& contentOffset) {
  markDirty();
  if (zoomScale == _zoomScale && contentOffset == _contentOffset) {
    return false;
  }
  _zoomScale = zoomScale;
  _contentOffset = contentOffset;
  updateRootMatrix();
  return true;
}

void AppHost::addImage(const std::string& name, std::shared_ptr<tgfx::Image> image) {
  if (name.empty()) {
    return;
  }
  if (image == nullptr) {
    return;
  }
  if (images.count(name) > 0) {
    return;
  }
  images[name] = std::move(image);
}

void AppHost::addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface) {
  markDirty();
  if (name.empty()) {
    return;
  }
  if (typeface == nullptr) {
    return;
  }
  if (typefaces.count(name) > 0) {
    return;
  }
  typefaces[name] = std::move(typeface);
}
void AppHost::draw(tgfx::Canvas* canvas, int drawIndex, bool isNeedBackground) const {
  if (drawIndex < 0 || drawIndex >= static_cast<int>(SampleManager::Count())) {
    return;
  }
  canvas->clear();

  if (isNeedBackground) {
    SampleManager::DrawBackground(canvas, this);
  }

  auto currentBuilder = SampleManager::GetByIndex(drawIndex);
  if (!currentBuilder) {
    return;
  }

  if (drawIndex != lastDrawIndex || !root) {
    displayList.root()->removeChildren();

    root = currentBuilder->buildLayerTree(this);

    if (root) {
      displayList.root()->addChild(root);
      if (_isTileMode) {
        displayList.setRenderMode(tgfx::RenderMode::Tiled);
        displayList.setAllowZoomBlur(true);
        displayList.setMaxTileCount(512);
      }
    }
    lastDrawIndex = drawIndex;
    updateRootMatrix();
  }
  auto builder = SampleManager::GetByIndex(drawIndex % SampleManager::Count());
  if (builder == nullptr) {
    return;
  }
  displayList.setZoomScale(zoomScale());
  displayList.setContentOffset(contentOffset().x, contentOffset().y);
  displayList.render(canvas->getSurface(), false);
}

void AppHost::updateRootMatrix() const {
  if (!root) {
    return;
  }
  auto bounds = root->getBounds(nullptr, true);
  if (bounds.isEmpty()) {
    return;
  }
  constexpr float padding = 30.0f;
  float width = static_cast<float>(this->width());
  float height = static_cast<float>(this->height());

  if (bounds.width() <= 0 || bounds.height() <= 0) {
    return;
  }

  const float totalScale = std::min(width / (padding * 2.0f + bounds.width()),
                                    height / (padding * 2.0f + bounds.height()));

  tgfx::Matrix rootMatrix = tgfx::Matrix::MakeScale(totalScale);
  rootMatrix.postTranslate((width - bounds.width() * totalScale) * 0.5f,
                           (height - bounds.height() * totalScale) * 0.5f);

  root->setMatrix(rootMatrix);
}

std::vector<std::shared_ptr<tgfx::Layer>> AppHost::getLayersUnderPoint(float x, float y) const {
  return displayList.root()->getLayersUnderPoint(x, y);
}

}  // namespace hello2d
