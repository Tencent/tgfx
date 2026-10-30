/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tgfx {

/**
 * One scene's outcome on the production route (bundle loaded, decomposition and dedicated kernels
 * on): which shader families served it and how, plus the fallback counters. The IR-unification
 * plan's V-ledger check diffs the JSON written from these across commits, so a commit that changes
 * routing produces an explicit ledger diff instead of a discovered regression.
 */
struct RouteLedgerSceneStats {
  std::string scene = {};
  std::vector<std::pair<std::string, uint32_t>> shaderHits = {};
  uint64_t runtimeCompiles = 0;
  uint64_t noMatchingRule = 0;
  uint64_t draws = 0;
  uint64_t completeAOTDraws = 0;
  uint64_t droppedDraws = 0;
  uint64_t directChainDraws = 0;
  uint64_t offscreenPlanDraws = 0;
  uint64_t atomicFallbacks = 0;
  uint64_t planExecutionFailures = 0;
  uint64_t materializedEdges = 0;
  std::array<uint64_t, 4> planPassHistogram = {};
};

/**
 * One case of the analytic-clip coverage-port probe matrix: the chain route's agreement with the
 * JIT reference for a draw whose only coverage is analytic. Recorded whether or not the case
 * passes, so the pre-fix state of the port routing is measurable before it changes.
 */
struct ClipPortProbeResult {
  std::string geometry = {};
  std::string clip = {};
  std::string blend = {};
  bool chainServed = false;
  uint64_t droppedDraws = 0;
  uint64_t runtimeCompiles = 0;
  int maxChannelDiff = 0;
  uint64_t diffBytes = 0;
  uint64_t totalBytes = 0;
};

/** Writes the route ledger as deterministic JSON (scenes in the given order, shaders sorted). */
bool SaveRouteLedgerJSON(const std::string& path,
                         const std::vector<RouteLedgerSceneStats>& entries);

/** Writes the coverage-port probe report as deterministic JSON. */
bool SaveClipPortProbeJSON(const std::string& path,
                           const std::vector<ClipPortProbeResult>& results);

}  // namespace tgfx
