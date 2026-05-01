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

#include "RasterizedImageCacheProbe.h"
#include <unordered_map>
#include <unordered_set>

namespace tgfx {

// File-scope state. Safe under the single-threaded WeChat Mini Program WASM runtime. If this code
// is ever linked into a multi-threaded build, the history map and counters must be guarded by a
// mutex or promoted to thread_local storage.
namespace {

// History of scales ever observed per image domain. Used to decide whether a miss is the first
// time we see the image, a new scale level for a known image, or a rebuild at a known scale.
using ScaleSet = std::unordered_set<uint32_t>;
using HistoryMap = std::unordered_map<uint64_t, ScaleSet>;

HistoryMap& GetHistory() {
  static HistoryMap history;
  return history;
}

CacheProbeStats& GetStats() {
  static CacheProbeStats stats;
  return stats;
}

// Encode (domainID, mipmapped) into a single 64-bit history key so the two mipmap variants of the
// same image do not share scale histories (they are distinct entries in the texture cache too).
uint64_t MakeHistoryKey(uint32_t domainID, bool mipmapped) {
  return (static_cast<uint64_t>(mipmapped ? 1u : 0u) << 32) | static_cast<uint64_t>(domainID);
}

// Quantize scale to a stable integer bucket to avoid hash collisions caused by float
// representation drift. NextCacheScaleLevel only produces a small set of values (1/8, 1/4, 1/2, 1)
// plus the mipmapped=true path which is always 1.0, so a 1/1024 bucket is more than precise
// enough and keeps the key comparison exact.
uint32_t BucketizeScale(float scale) {
  if (scale <= 0.0f) {
    return 0;
  }
  return static_cast<uint32_t>(scale * 1024.0f + 0.5f);
}

}  // namespace

CacheMissReason RecordCacheMiss(uint32_t domainID, float scale, bool mipmapped) {
  auto key = MakeHistoryKey(domainID, mipmapped);
  auto bucket = BucketizeScale(scale);
  auto& history = GetHistory();
  auto& stats = GetStats();

  auto it = history.find(key);
  if (it == history.end()) {
    history[key].insert(bucket);
    stats.first++;
    return CacheMissReason::First;
  }
  auto inserted = it->second.insert(bucket).second;
  if (inserted) {
    stats.newScale++;
    return CacheMissReason::NewScale;
  }
  stats.rebuild++;
  return CacheMissReason::Rebuild;
}

CacheProbeStats FetchAndResetCacheProbeStats() {
  auto& stats = GetStats();
  auto snapshot = stats;
  stats = {};
  return snapshot;
}

}  // namespace tgfx
