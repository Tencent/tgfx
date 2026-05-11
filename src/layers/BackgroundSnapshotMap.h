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

#include <memory>
#include <unordered_map>
#include "layers/LayerStyleSource.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"

namespace tgfx {

class Layer;
class LayerStyle;

/**
 * A snapshot of the background image and its offset captured during the capture pass for a
 * specific (Layer, LayerStyle) pair. Consumed during the consume pass by
 * LayerStyle::drawWithExtraSource.
 */
struct BackgroundSnapshotEntry {
  std::shared_ptr<Image> image = nullptr;
  Point offset = Point::Zero();
};

/**
 * Identifies which background snapshot belongs to which layer style. Layer* and LayerStyle* are
 * raw pointers; both are owned by shared_ptr in the layer tree and the map is built and consumed
 * within a single render invocation, so the keys are valid for the map's entire lifetime.
 */
struct BackgroundSnapshotKey {
  Layer* layer = nullptr;
  LayerStyle* style = nullptr;

  bool operator==(const BackgroundSnapshotKey& other) const {
    return layer == other.layer && style == other.style;
  }
};

struct BackgroundSnapshotKeyHash {
  std::size_t operator()(const BackgroundSnapshotKey& key) const noexcept {
    auto h1 = std::hash<const void*>{}(key.layer);
    auto h2 = std::hash<const void*>{}(key.style);
    // boost::hash_combine style mix: avoids the trivial collisions of h1 ^ (h2 << 1) when the
    // two pointers share low bits (common for pool-allocated objects). Pick a platform-sized
    // golden ratio so 32-bit builds do not warn about a 64-bit literal narrowing into size_t.
    constexpr std::size_t GoldenRatio = sizeof(std::size_t) >= 8
                                            ? static_cast<std::size_t>(0x9e3779b97f4a7c15ULL)
                                            : static_cast<std::size_t>(0x9e3779b9UL);
    return h1 ^ (h2 + GoldenRatio + (h1 << 6) + (h1 >> 2));
  }
};

/**
 * Concrete map type used to carry background snapshots from the capture pass to the consume
 * pass. Declared as a named struct (instead of a using-alias over std::unordered_map) so that
 * callers which only need a pointer or reference can forward-declare the type in public headers
 * without pulling in this internal header.
 *
 * Also caches LayerStyleSource per layer: capture and consume passes walk the same tree, so
 * each layer's content/contour images are identical between passes. Building the source once in
 * capture and reusing it in consume avoids redundant intermediate renders.
 */
struct BackgroundSnapshotMap
    : public std::unordered_map<BackgroundSnapshotKey, BackgroundSnapshotEntry,
                                BackgroundSnapshotKeyHash> {
  std::unordered_map<Layer*, std::unique_ptr<LayerStyleSource>> layerStyleSources = {};
};

}  // namespace tgfx
