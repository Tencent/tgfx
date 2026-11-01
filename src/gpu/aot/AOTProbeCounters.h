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
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <cstdint>

namespace tgfx {

/**
 * One-off quantification probes (P4.4's decision inputs). Diagnostic only — nothing on the
 * render path reads these; the report is the output (DumpAOTProbes, or the whole-process dump
 * TGFX_PROBE_DUMP=1 installs at exit).
 *
 * noiseLowerings: record-time lowerings whose color graph carries a PerlinNoiseSource node
 * (family-independent, once per lowering). The geometry form is classified at PREPARE time —
 * the record-time GP is null for every canvas draw (the geometry descriptor's premise), so
 * noiseGP* counts the op's real geometry processor when it adopts the carried IR. NOTE: this
 * is the GP CLASS, not the chain's variant dimensions — a QuadPerEdgeAA draw always carries
 * the coverage attribute (non-AA quads emit 1.0), so the chain's HAS_COVERAGE dim is true for
 * every quad; the coverage-FP count is a different fact and intentionally NOT probed here.
 * Prepare-time counting may see one draw more than once (probe re-preparation); the
 * distribution's shape is the input, not the absolute count.
 *
 * noiseSlotDepth*: constructed noise-profile chains by instruction-slot depth, plus the deepest
 * observed. ValidateSlots runs per plan-fit AND per compile, so one chain may count more than
 * once — same caveat. The 32→16 question is answered by the 17-32 bins staying empty.
 *
 * blur*: opaque-kernel blur draws whose input node is a computed expression (the Tier-2 cut
 * point's audience) versus a plain texture leaf.
 */
struct AOTProbeCounters {
  std::atomic<uint64_t> noiseLowerings = {0};
  std::atomic<uint64_t> noiseGPDefault = {0};
  std::atomic<uint64_t> noiseGPQuad = {0};
  std::atomic<uint64_t> noiseGPOther = {0};
  std::atomic<uint64_t> noiseSlotDepth1To8 = {0};
  std::atomic<uint64_t> noiseSlotDepth9To16 = {0};
  std::atomic<uint64_t> noiseSlotDepth17To24 = {0};
  std::atomic<uint64_t> noiseSlotDepth25To32 = {0};
  std::atomic<uint64_t> noiseChainMaxDepth = {0};
  std::atomic<uint64_t> blurComputedInput = {0};
  std::atomic<uint64_t> blurTextureInput = {0};
};

AOTProbeCounters* AOTProbe();

void ResetAOTProbes();

/// Prints the counters (the probe's report).
void DumpAOTProbes();

}  // namespace tgfx
