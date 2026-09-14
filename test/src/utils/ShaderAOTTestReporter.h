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
  bool consistent = true;

  bool passed() const {
    return consistent && noMatchingRule == 0 && runtimeCompiles == 0;
  }
};

AOTCoverageGateResult EvaluateAOTCoverageGate(uint64_t rawNoMatchingRule,
                                             uint64_t deliberateNoMatchingRule,
                                             uint64_t rawBuilderCreations,
                                             uint64_t excludedBuilderCreations);

void InstallShaderAOTTestReporter();

}  // namespace tgfx
