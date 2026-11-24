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

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include "layers/BackgroundSnapshotMap.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * The rasterized output of a background-sourced style (BackgroundBlurStyle, GlassStyle, ...) and
 * what it was produced under.
 */
struct StyleOutputEntry {
  std::shared_ptr<Image> image = nullptr;
  // Maps the image's pixels into the layer's local coordinates.
  Matrix localMatrix = Matrix::I();
  // Device pixels per local unit the output was rasterized at.
  float contentScale = 1.0f;
  float alpha = 1.0f;
  // Global area the style read its backdrop from: renderBounds outset by maxBackgroundOutset.
  Rect sampleBounds = {};
};

/**
 * Keeps the outputs of background-sourced styles across frames, per (layer, style) at one zoom
 * scale. An entry stays valid while no dirty region touches its sample bounds: the backdrop behind
 * the layer and the layer itself are then unchanged, so the output is the same.
 */
class StyleOutputCache {
 public:
  explicit StyleOutputCache(size_t maxBytes);

  /**
   * Called once per frame before any lookup. Drops every entry when the zoom scale changed, then
   * every entry whose sample bounds a dirty region touches. Removing a layer or replacing its styles
   * always dirties the layer's old bounds, so no entry outlives the Layer* / LayerStyle* in its key.
   */
  void beginFrame(int64_t zoomScaleInt, const std::vector<Rect>& dirtyRegions);

  /**
   * Returns the entry for key if it was produced at contentScale and alpha, or nullptr. Marks the
   * entry as recently used.
   */
  const StyleOutputEntry* find(const BackgroundSnapshotKey& key, float contentScale, float alpha);

  // Same as find() without touching recency or counters.
  const StyleOutputEntry* peek(const BackgroundSnapshotKey& key, float contentScale,
                               float alpha) const;

  bool contains(const BackgroundSnapshotKey& key) const;

  /**
   * Stores an entry, replacing any previous one for key, and evicts least recently used entries
   * past the byte budget.
   */
  void store(const BackgroundSnapshotKey& key, StyleOutputEntry entry);

  void clear();

  size_t bytes() const {
    return totalBytes;
  }

  size_t size() const {
    return entries.size();
  }

 private:
  struct Item {
    BackgroundSnapshotKey key = {};
    StyleOutputEntry entry = {};
    size_t bytes = 0;
  };

  void erase(std::list<Item>::iterator item);

  size_t maxBytes = 0;
  size_t totalBytes = 0;
  int64_t zoomScaleInt = 0;
  // Most recently used first.
  std::list<Item> items = {};
  std::unordered_map<BackgroundSnapshotKey, std::list<Item>::iterator, BackgroundSnapshotKeyHash>
      entries = {};
};

}  // namespace tgfx
