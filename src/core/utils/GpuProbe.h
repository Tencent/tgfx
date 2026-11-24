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
#include <map>
#include <string>
#include <unordered_map>

namespace tgfx {

// TEMP probe: GPU work recorded between two flushes, printed by DrawingManager::flush().
struct GpuProbe {
  int passes = 0;
  double passPx = 0.0;
  int drawOps = 0;
  int runtimeTasks = 0;
  int mipmapTasks = 0;
  int copyTasks = 0;
  double copyPx = 0.0;
  int cow = 0;
  double cowPx = 0.0;
  int newTargets = 0;
  double newTargetPx = 0.0;
  int blurs = 0;
  double blurPx = 0.0;
  int rasterized = 0;
  int snapshots = 0;
  // Passes per render target, to tell how often drawing into one target is split into several
  // passes by work on other targets in between.
  std::unordered_map<const void*, int> passesPerTarget = {};
  // The innermost drawing step running when GPU work is recorded, set by GpuProbeTag.
  const char* tag = "other";
  struct TagStat {
    int newTargets = 0;
    double newTargetPx = 0.0;
    int cow = 0;
    double cowPx = 0.0;
    int blurs = 0;
    int snapshots = 0;
  };
  std::map<std::string, TagStat> tags = {};

  TagStat& current() {
    return tags[tag];
  }

  void reset() {
    auto currentTag = tag;
    *this = {};
    tag = currentTag;
  }
};

inline GpuProbe gGpuProbe = {};

class GpuProbeTag {
 public:
  explicit GpuProbeTag(const char* tag) : previous(gGpuProbe.tag) {
    gGpuProbe.tag = tag;
  }
  ~GpuProbeTag() {
    gGpuProbe.tag = previous;
  }

 private:
  const char* previous = nullptr;
};

}  // namespace tgfx
