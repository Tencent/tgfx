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

#include "StyleOutputCache.h"
#include <algorithm>
#include "core/utils/MathExtra.h"

namespace tgfx {

static size_t ImageBytes(const std::shared_ptr<Image>& image) {
  if (image == nullptr) {
    return 0;
  }
  return static_cast<size_t>(image->width()) * static_cast<size_t>(image->height()) * 4;
}

StyleOutputCache::StyleOutputCache(size_t maxBytes) : maxBytes(maxBytes) {
}

void StyleOutputCache::beginFrame(int64_t newZoomScaleInt, const std::vector<Rect>& dirtyRegions) {
  if (newZoomScaleInt != zoomScaleInt) {
    clear();
    zoomScaleInt = newZoomScaleInt;
    return;
  }
  if (dirtyRegions.empty()) {
    return;
  }
  for (auto item = items.begin(); item != items.end();) {
    auto& sampleBounds = item->entry.sampleBounds;
    auto touched = std::any_of(dirtyRegions.begin(), dirtyRegions.end(), [&](const Rect& rect) {
      return Rect::Intersects(rect, sampleBounds);
    });
    if (touched) {
      auto next = std::next(item);
      erase(item);
      item = next;
    } else {
      ++item;
    }
  }
}

const StyleOutputEntry* StyleOutputCache::find(const BackgroundSnapshotKey& key, float contentScale,
                                               float alpha) {
  auto result = entries.find(key);
  if (result == entries.end() ||
      !FloatNearlyEqual(result->second->entry.contentScale, contentScale) ||
      !FloatNearlyEqual(result->second->entry.alpha, alpha)) {
    return nullptr;
  }
  items.splice(items.begin(), items, result->second);
  return &result->second->entry;
}

const StyleOutputEntry* StyleOutputCache::peek(const BackgroundSnapshotKey& key, float contentScale,
                                               float alpha) const {
  auto result = entries.find(key);
  if (result == entries.end() ||
      !FloatNearlyEqual(result->second->entry.contentScale, contentScale) ||
      !FloatNearlyEqual(result->second->entry.alpha, alpha)) {
    return nullptr;
  }
  return &result->second->entry;
}

bool StyleOutputCache::contains(const BackgroundSnapshotKey& key) const {
  return entries.count(key) > 0;
}

void StyleOutputCache::store(const BackgroundSnapshotKey& key, StyleOutputEntry entry) {
  auto bytes = ImageBytes(entry.image);
  if (bytes == 0 || bytes > maxBytes) {
    return;
  }
  auto result = entries.find(key);
  if (result != entries.end()) {
    erase(result->second);
  }
  items.push_front({key, std::move(entry), bytes});
  entries[key] = items.begin();
  totalBytes += bytes;
  while (totalBytes > maxBytes && !items.empty()) {
    erase(std::prev(items.end()));
  }
}

void StyleOutputCache::clear() {
  items.clear();
  entries.clear();
  totalBytes = 0;
}

void StyleOutputCache::erase(std::list<Item>::iterator item) {
  totalBytes -= item->bytes;
  entries.erase(item->key);
  items.erase(item);
}

}  // namespace tgfx
