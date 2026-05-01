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

#include <cstdint>

namespace tgfx {

/**
 * Classification of a RasterizedImage texture cache miss.
 *
 * When RasterizedImage::lockTextureProxy fails to find a matching proxy in the cache, the probe
 * records the miss and classifies it into one of three reasons. This tells us *why* a new texture
 * upload is being scheduled and therefore which optimization strategy is appropriate:
 *   - First:    brand new image never decoded before  -> predecode / async decode.
 *   - NewScale: same image, new scale level           -> mipmap collapses scale levels to one.
 *   - Rebuild:  same image, same scale, seen before   -> LRU/expiry evicted it; tune cache.
 */
enum class CacheMissReason : uint8_t {
  First = 0,
  NewScale = 1,
  Rebuild = 2,
};

/**
 * Per-frame cache miss counters. Populated by RecordCacheMiss() and drained by
 * FetchAndResetCacheProbeStats() at the end of each DrawingBuffer::encode().
 */
struct CacheProbeStats {
  uint32_t first = 0;
  uint32_t newScale = 0;
  uint32_t rebuild = 0;

  uint32_t total() const {
    return first + newScale + rebuild;
  }
};

/**
 * Records one RasterizedImage texture cache miss and classifies it relative to prior misses.
 *
 * The probe keeps a process-wide history of (domainID, scale) pairs ever observed. Entries are
 * never evicted from the history: "ever seen" is exactly the signal we need to distinguish a true
 * LRU-eviction rebuild from a first-time decode. For PAGX workloads the number of distinct images
 * is small and bounded, so unbounded growth is not a concern.
 *
 * The returned reason is also accumulated into the per-frame stats.
 */
CacheMissReason RecordCacheMiss(uint32_t domainID, float scale, bool mipmapped);

/**
 * Returns the accumulated per-frame miss counters and resets them to zero. Call this from
 * DrawingBuffer::encode() at the end of the frame.
 */
CacheProbeStats FetchAndResetCacheProbeStats();

}  // namespace tgfx
