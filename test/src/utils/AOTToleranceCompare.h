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

#include "tgfx/core/Pixmap.h"

namespace tgfx {

/**
 * Tolerance thresholds for comparing a reference render against an AOT-path render. Thresholds must
 * be declared explicitly per effect class; there is no unmeasured default (see design §12.5.3).
 * Tolerance applies only to migration-era cross-validation of semantically equivalent paths; it
 * must never mask structural differences (different size, bounds, or significant color shifts).
 */
struct AOTToleranceSpec {
  /// Maximum allowed per-channel absolute difference in 8-bit units [0,255].
  int maxChannelDiff = 0;
  /// Maximum allowed fraction of pixels that differ at all, in [0,1].
  double maxDiffPixelRatio = 0.0;
  /// A per-channel difference above this counts as a structural (non-microdiff) difference and
  /// always fails, regardless of the ratios above. Guards against a few catastrophically wrong
  /// pixels hiding under a low overall ratio.
  int structuralChannelDiff = 64;
};

/**
 * Structured result of a tolerance comparison. Reports raw measurements so callers can log them
 * even when the comparison passes.
 */
struct AOTToleranceResult {
  bool sizeMismatch = false;
  bool passed = false;
  bool structuralDifference = false;
  int width = 0;
  int height = 0;
  int maxChannelDiff = 0;
  uint64_t diffPixelCount = 0;
  uint64_t totalPixelCount = 0;

  double diffPixelRatio() const {
    return totalPixelCount == 0
               ? 0.0
               : static_cast<double>(diffPixelCount) / static_cast<double>(totalPixelCount);
  }
};

/**
 * AOTToleranceCompare compares two renders of the same scene (reference path vs AOT path) under a
 * constrained tolerance. It measures the maximum per-channel difference and the fraction of
 * differing pixels, and passes only when both stay within the spec AND no single pixel exceeds the
 * structural threshold. It performs no baseline acceptance and writes no files; it is a pure
 * measurement used for cross-validation and quantification.
 */
class AOTToleranceCompare {
 public:
  /**
   * Compares two pixmaps of the same scene under the given tolerance spec.
   *
   * @param reference  The reference-path render (e.g. ProgramBuilder path).
   * @param candidate  The AOT-path render to validate.
   * @param spec       The per-effect-class tolerance thresholds.
   * @return           Structured measurements and the pass/fail decision.
   */
  static AOTToleranceResult Compare(const Pixmap& reference, const Pixmap& candidate,
                                    const AOTToleranceSpec& spec);
};

}  // namespace tgfx
