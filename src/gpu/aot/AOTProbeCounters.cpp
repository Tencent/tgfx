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

#include "gpu/aot/AOTProbeCounters.h"
#include <cstdio>
#include <cstdlib>

namespace tgfx {

AOTProbeCounters& ProbeState() {
  static AOTProbeCounters counters = {};
  return counters;
}

AOTProbeCounters* AOTProbe() {
  return &ProbeState();
}

void ResetAOTProbes() {
  auto* counters = &ProbeState();
  counters->noiseLowerings.store(0, std::memory_order_relaxed);
  counters->noiseGPDefault.store(0, std::memory_order_relaxed);
  counters->noiseGPQuad.store(0, std::memory_order_relaxed);
  counters->noiseGPQuadNoCoverage.store(0, std::memory_order_relaxed);
  counters->noiseGPOther.store(0, std::memory_order_relaxed);
  counters->noiseSlotDepth1To8.store(0, std::memory_order_relaxed);
  counters->noiseSlotDepth9To16.store(0, std::memory_order_relaxed);
  counters->noiseSlotDepth17To24.store(0, std::memory_order_relaxed);
  counters->noiseSlotDepth25To32.store(0, std::memory_order_relaxed);
  counters->noiseChainMaxDepth.store(0, std::memory_order_relaxed);
  counters->blurComputedInput.store(0, std::memory_order_relaxed);
  counters->blurTextureInput.store(0, std::memory_order_relaxed);
}

void DumpAOTProbes() {
  const auto* counters = AOTProbe();
  auto relaxed = std::memory_order_relaxed;
  printf(
      "[Probe] noise draws: lowerings=%llu, GP forms (prepare-time) Default=%llu "
      "QuadPerEdgeAA+coverage=%llu QuadPerEdgeAA-noCoverage=%llu other=%llu\n",
      static_cast<unsigned long long>(counters->noiseLowerings.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseGPDefault.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseGPQuad.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseGPQuadNoCoverage.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseGPOther.load(relaxed)));
  printf(
      "[Probe] noise chains by slot depth: 1-8=%llu 9-16=%llu 17-24=%llu 25-32=%llu "
      "(deepest=%llu)\n",
      static_cast<unsigned long long>(counters->noiseSlotDepth1To8.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseSlotDepth9To16.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseSlotDepth17To24.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseSlotDepth25To32.load(relaxed)),
      static_cast<unsigned long long>(counters->noiseChainMaxDepth.load(relaxed)));
  printf("[Probe] blur opaque draws: computedInput=%llu textureInput=%llu\n",
         static_cast<unsigned long long>(counters->blurComputedInput.load(relaxed)),
         static_cast<unsigned long long>(counters->blurTextureInput.load(relaxed)));
}

namespace {

// The whole-process dump: TGFX_PROBE_DUMP=1 on any binary (the full test suite included) prints
// the counters at exit, so the distributions cover every scene the run exercised.
struct ProbeDumpAtExit {
  ~ProbeDumpAtExit() {
    if (std::getenv("TGFX_PROBE_DUMP") != nullptr) {
      DumpAOTProbes();
    }
  }
};

ProbeDumpAtExit g_probeDumpAtExit = {};

}  // namespace

}  // namespace tgfx
