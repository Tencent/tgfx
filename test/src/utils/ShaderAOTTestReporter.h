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

struct AOTCoverageGateResult {
  uint64_t noMatchingRule = 0;
  uint64_t runtimeCompiles = 0;
  // Exclusions are surfaced (never silently dropped): environment compile failures are
  // SwiftShader's compiler memory limit on fully-unrolled AOT artifacts. Each excluded miss
  // pairs with exactly one fallback runtime compilation, so the runtimeCompiles total drops by
  // the same amounts.
  uint64_t excludedEnvironment = 0;
  // Misses that match an entry of the reviewed known-gap table (see AOTKnownGap). They stay
  // visible in the report and the JSON; only the strict residual (noMatchingRule) excludes them.
  uint64_t excludedKnownGaps = 0;
  // Dropped draws attributable to a listed gap: a listed miss implies the draw it belongs to
  // rendered nothing, so those drops are surfaced separately instead of failing the gate.
  uint64_t knownGapDrops = 0;
  // Listed gaps whose test ran in this run but matched no miss. A stale entry is a cleanup debt
  // and fails the gate, exactly like an unlisted gap does; a filtered run that never reached the
  // test does not count its entry, so blocking mode stays usable on filtered runs.
  uint64_t staleKnownGaps = 0;
  uint64_t knownGapEntries = 0;
  // Draws that rendered nothing (AOTDrawStats::droppedDraws, deliberate drops and known-gap
  // attributable drops excluded).
  uint64_t droppedDraws = 0;
  bool consistent = true;

  bool passed() const {
    return consistent && noMatchingRule == 0 && runtimeCompiles == 0 && droppedDraws == 0 &&
           staleKnownGaps == 0;
  }
};

/**
 * Reduces one test's raw counters into the strict gate. knownGapMisses are the misses whose effect
 * signature names an entry of the reviewed known-gap table; they leave the strict residual but stay
 * surfaced through excludedKnownGaps. An exclusion may never exceed what it subtracts from
 * (consistency), so a table entry cannot hide a miss it does not describe.
 */
AOTCoverageGateResult EvaluateAOTCoverageGate(uint64_t rawNoMatchingRule,
                                              uint64_t deliberateNoMatchingRule,
                                              uint64_t environmentCompileFailures,
                                              uint64_t rawBuilderCreations,
                                              uint64_t excludedBuilderCreations,
                                              uint64_t knownGapMisses = 0);

/**
 * Whether a whole run meets the strict coverage targets. Zero misses alone is not enough: a run
 * that drew nothing, or whose draws were all excluded from the metrics, also reports zero, so the
 * run must additionally have served at least one program from a precompiled artifact.
 */
bool AOTCoverageGateMet(const AOTCoverageGateResult& gate, uint64_t precompiledArtifactCreations);

void InstallShaderAOTTestReporter();

}  // namespace tgfx
