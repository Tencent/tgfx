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

#include "tgfx/layers/VectorLayer.h"
#include "core/utils/Log.h"
#include "tgfx/layers/LayerRecorder.h"
#include "vectors/VectorContext.h"

namespace tgfx {

std::shared_ptr<VectorLayer> VectorLayer::Make() {
  return std::shared_ptr<VectorLayer>(new VectorLayer());
}

VectorLayer::~VectorLayer() {
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    detachProperty(element.get());
  }
  if (_textPath != nullptr) {
    detachProperty(_textPath.get());
  }
  for (const auto& modifier : _textModifiers) {
    DEBUG_ASSERT(modifier != nullptr);
    detachProperty(modifier.get());
  }
}

void VectorLayer::setContents(std::vector<std::shared_ptr<VectorElement>> value) {
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    detachProperty(element.get());
  }
  _contents.clear();
  for (auto& element : value) {
    if (element == nullptr) {
      continue;
    }
    attachProperty(element.get());
    _contents.push_back(std::move(element));
  }
  invalidateContent();
}

void VectorLayer::setTextPath(std::shared_ptr<TextPath> value) {
  if (_textPath == value) {
    return;
  }
  if (_textPath != nullptr) {
    detachProperty(_textPath.get());
  }
  _textPath = std::move(value);
  if (_textPath != nullptr) {
    attachProperty(_textPath.get());
  }
  invalidateContent();
}

void VectorLayer::setTextModifiers(std::vector<std::shared_ptr<TextModifier>> value) {
  for (const auto& modifier : _textModifiers) {
    DEBUG_ASSERT(modifier != nullptr);
    detachProperty(modifier.get());
  }
  _textModifiers.clear();
  for (auto& modifier : value) {
    if (modifier == nullptr) {
      continue;
    }
    attachProperty(modifier.get());
    _textModifiers.push_back(std::move(modifier));
  }
  invalidateContent();
}

void VectorLayer::onUpdateContent(LayerRecorder* recorder) {
  if (_contents.empty()) {
    return;
  }
  VectorContext context = {};
  // 1. Apply all elements in contents (geometries, path modifiers, styles)
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    if (element->enabled()) {
      element->apply(&context);
    }
  }
  // 2. Apply layer-level TextModifiers in order (transforms like position, scale, rotation)
  for (const auto& modifier : _textModifiers) {
    DEBUG_ASSERT(modifier != nullptr);
    modifier->apply(&context);
  }
  // 3. Apply layer-level TextPath (snaps transformed glyphs onto the curve)
  if (_textPath != nullptr) {
    _textPath->apply(&context);
  }
  // 4. Render all painters
  for (const auto& painter : context.painters) {
    painter->draw(recorder);
  }
}

}  // namespace tgfx
