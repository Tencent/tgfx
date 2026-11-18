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

// Temporary tile probe switch shared by DisplayList.cpp, Context.cpp, DrawingManager and the
// background handlers. Off by default: the probes compile out completely. Turn them on with the
// CMake option TGFX_TILE_PROBE, or by defining TILE_PROBE_ENABLED=1 for the tgfx target.
#ifndef TILE_PROBE_ENABLED
#define TILE_PROBE_ENABLED 0
#endif

// Experiment: record the background capture as usual, then drop its snapshots before any tile is
// drawn. The tiles then draw without backdrop blur, so the picture differs; the point is to see
// whether capture work nobody consumes still costs encode time. Leave at 0 for normal runs.
#ifndef TILE_PROBE_DISCARD_CAPTURE
#define TILE_PROBE_DISCARD_CAPTURE 0
#endif

#if TILE_PROBE_ENABLED
#define TILE_PROBE(...) __VA_ARGS__

#include <chrono>
#include <cstdint>

#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * Work recorded inside BackgroundCapturer::drawBackgroundStyle, accumulated over one
 * DisplayList::captureBackgrounds call. DisplayList resets it before the call and reads it after,
 * so the rest of the capture is the layer content drawn onto the background surface.
 */
struct CaptureStyleProbe {
  uint32_t styles = 0;
  uint32_t drawCalls = 0;
  uint32_t ops = 0;
  uint32_t copies = 0;
  uint32_t switches = 0;
};

inline CaptureStyleProbe& CaptureProbe() {
  static CaptureStyleProbe probe = {};
  return probe;
}

/**
 * Shape of the background styles that touch one cluster of tiles, filled by
 * DisplayList::collectBackgroundFootprints. It answers whether the capture could be done inline
 * while the tile is drawn: footprints that overlap each other would sample each other's output,
 * and the outset decides how much larger than the tile the drawing area has to be.
 */
struct FootprintProbe {
  uint32_t count = 0;
  uint32_t overlapPairs = 0;
  uint32_t overlapped = 0;
  uint32_t nested = 0;
  // Styles whose sampled area (footprint) touches the bounds of a style that is drawn before
  // them. Only these would see a blurred backdrop if the tile content were used as the backdrop,
  // so they are the ones that still need a clean capture.
  uint32_t dirty = 0;
  uint32_t dirtyByNested = 0;
  // Capture cost weight: draw calls of the capture are dominated by how many styles it serves, so
  // the share of dirty styles bounds how much of the capture an inline path could skip.
  uint32_t checked = 0;
  float maxOutsetWorld = 0;
  float minOutsetWorld = 0;
  float sumOutsetWorld = 0;
  // Styles bucketed by the longer side of their content bounds, in the pixels of the tile they are
  // drawn into: < 0.25, < 0.5, < 1, < 2. A style in the first three paints less than one pixel, so
  // leaving it out changes at most that pixel's coverage.
  uint32_t subPixel[4] = {};
  // Styles left when the sub-pixel ones are dropped, and the union box of their footprints. A count
  // within the cull limit is what decides whether the capture can cull per footprint again.
  uint32_t kept = 0;
  Rect keptBox = Rect::MakeEmpty();
  // Every background blur style of the region, whether or not the current radius skips it, bucketed
  // by its blur radius in the pixels of the tile: < 0.3, < 0.35, < 0.4, < 0.45, < 0.5, >= 0.5.
  uint32_t sigma[6] = {};
  uint32_t sigmaCount = 0;
};

inline FootprintProbe& FootprintInfo() {
  static FootprintProbe probe = {};
  return probe;
}

/**
 * What one tile draw did with the layer tree: how many layers reached the culling test, how many it
 * dropped, how many the subtree cache served, and which conditions forced an offscreen buffer. A
 * capture or a blur is a small part of a slow tile, so this is what says where the rest goes.
 */
struct TileContentProbe {
  uint32_t layers = 0;
  uint32_t culled = 0;
  uint32_t cacheEligible = 0;
  uint32_t cacheHits = 0;
  uint32_t offscreen = 0;
  uint32_t offBlend = 0;
  uint32_t offGroupAlpha = 0;
  uint32_t offFilter = 0;
  uint32_t offMask = 0;
  uint32_t offPassThrough = 0;
  uint32_t masks = 0;
  uint32_t groups = 0;
  uint32_t leaves = 0;
  uint32_t emptyLeaves = 0;
  // Shape of the tree the tile walks: how deep the layers sit (1..7 exact, 8 = 8 or deeper), how
  // many children a container has, and how many containers are pure pass-through (no content, no
  // layer styles, no mask, no filters).
  uint32_t depth = 0;
  uint32_t depthHist[8] = {};
  uint32_t maxDepth = 0;
  uint32_t childHist[5] = {};
  uint32_t pureGroups = 0;
  // Timings of the per-layer work, in microseconds, taken from every sixteenth layer so that the
  // clock reads stay under a few percent of the tile cost. A layer's own content and children are
  // measured separately, so the numbers do not overlap.
  double maskUs = 0;
  double cacheUs = 0;
  double sourceUs = 0;
  double contentUs = 0;
  uint32_t samples = 0;
  bool sampling = false;
  std::chrono::steady_clock::time_point mark = {};
};

inline TileContentProbe& TileContent();

inline double TileProbeMicros(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

inline void TileProbeBegin() {
  auto& probe = TileContent();
  ++probe.depth;
  if (probe.depth > probe.maxDepth) {
    probe.maxDepth = probe.depth;
  }
  auto depthSlot = probe.depth >= 8 ? 7 : probe.depth - 1;
  ++probe.depthHist[depthSlot];
  probe.sampling = (++probe.layers & 15) == 0;
  if (probe.sampling) {
    ++probe.samples;
    probe.mark = std::chrono::steady_clock::now();
  }
}

inline void TileProbeSegment(double* slot) {
  auto& probe = TileContent();
  if (!probe.sampling) {
    return;
  }
  *slot += TileProbeMicros(probe.mark);
  probe.mark = std::chrono::steady_clock::now();
}

inline void TileProbeEnd() {
  auto& probe = TileContent();
  probe.sampling = false;
  if (probe.depth > 0) {
    --probe.depth;
  }
}

inline TileContentProbe& TileContent() {
  static TileContentProbe probe = {};
  return probe;
}

/**
 * Work recorded around the background styles consumed while a tile is drawn: the part of drawing a
 * tile that belongs to the blur itself, as opposed to the layer content around it.
 */
struct ConsumeStyleProbe {
  uint32_t styles = 0;
  uint32_t drawCalls = 0;
  uint32_t ops = 0;
  uint32_t runtime = 0;
  uint32_t copies = 0;
  uint32_t switches = 0;
};

inline ConsumeStyleProbe& ConsumeProbe() {
  static ConsumeStyleProbe probe = {};
  return probe;
}

/**
 * Which part of DisplayList::renderTiled a render task is recorded in. Offscreen work is recorded
 * as it is drawn, while the atlas compositor stays open until the flush, so atlas draws of the
 * tiles land in Other.
 */
enum class ProbePhase : uint8_t { Other = 0, Capture = 1, Tiles = 2, Screen = 3 };

inline ProbePhase& CurrentProbePhase() {
  static ProbePhase phase = ProbePhase::Other;
  return phase;
}

/**
 * Encode time of the render tasks of one Context::submit, by the phase they were recorded in.
 * Context resets it before encoding and prints it with the [TileGPU] line; what the render tasks
 * do not account for of the encode time is resource creation, atlas upload and finishing.
 */
struct EncodeProbe {
  double taskMs[4] = {};
  uint32_t tasks[4] = {};
};

inline EncodeProbe& EncodeStats() {
  static EncodeProbe probe = {};
  return probe;
}
}  // namespace tgfx
#else
#define TILE_PROBE(...)
#endif
