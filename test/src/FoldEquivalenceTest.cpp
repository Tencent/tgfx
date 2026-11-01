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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Verifies the pixel-equivalence contract behind OpsCompositor's coverage fold fallback: when the
// fold moves the coverage processors into the color list and the chain task build later fails,
// the plain route renders the folded tree — the mask premultiplied into the source with the XP's
// coverage pinned to 1. That is only equivalent to the true coverage composite for blend modes
// where f(S*m, D, 1) == f(S, D, m) in the kernel's coverage math (xpBlendWithCoverage in
// xp_blend_colors.inc, mirrored below for the coefficient family, modes 0..14). The fold's gate
// is !BlendModeNeedDstTexture(mode, true); this test enumerates every mode the gate admits and
// fails on any mode whose math breaks the equivalence, so a gate/mismatch regression (or a new
// mode slipping in) surfaces here instead of as a wrong-pixel fallback.

#include <cmath>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/BlendFormula.h"
#include "gpu/glsl/GLSLBlend.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

namespace {

struct FoldVec {
  float r, g, b, a;
};

static FoldVec operator*(const FoldVec& x, const FoldVec& y) {
  return {x.r * y.r, x.g * y.g, x.b * y.b, x.a * y.a};
}

static FoldVec operator*(const FoldVec& x, float s) {
  return {x.r * s, x.g * s, x.b * s, x.a * s};
}

static FoldVec operator*(float s, const FoldVec& x) {
  return {x.r * s, x.g * s, x.b * s, x.a * s};
}

static FoldVec operator+(const FoldVec& x, const FoldVec& y) {
  return {x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a};
}

static FoldVec operator-(const FoldVec& x, const FoldVec& y) {
  return {x.r - y.r, x.g - y.g, x.b - y.b, x.a - y.a};
}

static FoldVec OneMinus(const FoldVec& x) {
  return {1.0f - x.r, 1.0f - x.g, 1.0f - x.b, 1.0f - x.a};
}

static FoldVec Clamped(const FoldVec& x) {
  auto c = [](float v) { return std::min(1.0f, std::max(0.0f, v)); };
  return {c(x.r), c(x.g), c(x.b), c(x.a)};
}

// CPU mirror of xpBlendWithCoverage's coefficient-family branches (xp_blend_colors.inc,
// modes 0..14). The generic tail (C*blended + (1-C)*D for the separable modes) is never
// fold-equivalent — fold pins C to 1, which drops the (1-C)*D term entirely — so those modes
// must not pass the gate in the first place; the test asserts that too.
static bool BlendWithCoverage(const FoldVec& S, const FoldVec& D, const FoldVec& C, int mode,
                              FoldVec* out) {
  switch (mode) {
    case 0:
      *out = Clamped(D - C * D);
      return true;
    case 1:
      *out = Clamped(S * C + D * (1.0f - C.a));
      return true;
    case 2:
      *out = Clamped(D);
      return true;
    case 3:
      *out = Clamped(S * C + D * (1.0f - (S * C).a));
      return true;
    case 4:
      *out = Clamped(S * C * (1.0f - D.a) + D);
      return true;
    case 5:
      *out = Clamped(S * C * D.a + D * (1.0f - C.a));
      return true;
    case 6:
      *out = Clamped(D - (1.0f - S.a) * C * D);
      return true;
    case 7:
      *out = Clamped(S * C * (1.0f - D.a) + D * (1.0f - C.a));
      return true;
    case 8:
      *out = Clamped(D * (1.0f - (S * C).a));
      return true;
    case 9:
      *out = Clamped(S * C * D.a + D * (1.0f - (S * C).a));
      return true;
    case 10:
      *out = Clamped(S * C * (1.0f - D.a) + D * OneMinus((1.0f - S.a) * C));
      return true;
    case 11:
      *out = Clamped(S * C * (1.0f - D.a) + D * (1.0f - (S * C).a));
      return true;
    case 12:
      *out = Clamped(S * C + D);
      return true;
    case 13:
      *out = Clamped(D - OneMinus(S) * C * D);
      return true;
    case 14:
      *out = Clamped(S * C + D * OneMinus(S * C));
      return true;
    default:
      // The generic separable tail: C * blended + (1 - C) * D. Fold pins C to 1, which drops
      // the (1-C)*D term — not equivalent for any mode whose blend reads D.
      return false;
  }
}

static bool NearlyEqual(const FoldVec& x, const FoldVec& y, float epsilon) {
  return std::fabs(x.r - y.r) <= epsilon && std::fabs(x.g - y.g) <= epsilon &&
         std::fabs(x.b - y.b) <= epsilon && std::fabs(x.a - y.a) <= epsilon;
}

}  // namespace

TGFX_TEST(FoldEquivalenceTest, GateAdmittedModesKeepCoverageEquivalence) {
  const FoldVec samplesS[] = {{1.0f, 0.5f, 0.25f, 0.75f},
                              {0.2f, 0.9f, 0.4f, 1.0f},
                              {0.6f, 0.1f, 0.85f, 0.3f}};
  const FoldVec samplesD[] = {{0.2f, 0.8f, 0.6f, 1.0f},
                              {0.9f, 0.3f, 0.5f, 0.45f},
                              {0.0f, 1.0f, 0.7f, 0.85f}};
  const FoldVec samplesM[] = {{0.5f, 0.5f, 0.5f, 0.5f},
                              {0.25f, 0.25f, 0.25f, 0.25f},
                              {0.85f, 0.85f, 0.85f, 0.85f}};
  const FoldVec one = {1.0f, 1.0f, 1.0f, 1.0f};

  std::vector<std::string> failures;
  size_t admitted = 0;
  for (int mode = 0; mode <= static_cast<int>(BlendMode::Screen); ++mode) {
    auto blendMode = static_cast<BlendMode>(mode);
    if (BlendModeNeedDstTexture(blendMode, true)) {
      continue;
    }
    ++admitted;
    bool modeFailed = false;
    for (const auto& S : samplesS) {
      for (const auto& D : samplesD) {
        for (const auto& M : samplesM) {
          FoldVec covered;
          FoldVec folded;
          if (!BlendWithCoverage(S, D, M, mode, &covered) ||
              !BlendWithCoverage(S * M, D, one, mode, &folded) ||
              !NearlyEqual(covered, folded, 1e-5f)) {
            failures.push_back(std::string("mode ") + BlendModeName(blendMode) + " (" +
                               std::to_string(mode) +
                               "): fold(S*m, D, 1) != f(S, D, m) — the plain-route fallback of "
                               "the coverage fold renders different pixels");
            modeFailed = true;
            break;
          }
        }
        if (modeFailed) {
          break;
        }
      }
      if (modeFailed) {
        break;
      }
    }
  }
  EXPECT_TRUE(failures.empty());
  for (const auto& failure : failures) {
    printf("[FoldEquivalence] %s\n", failure.c_str());
  }
  // A mode beyond the coefficient family would take the generic C*blended + (1-C)*D tail, which
  // fold can never reproduce (C pinned to 1 drops the dst term). Assert the gate never admits
  // one: if this fires, a new non-equivalent mode entered the fold.
  for (int mode = static_cast<int>(BlendMode::Screen) + 1;
       mode <= static_cast<int>(BlendMode::Luminosity); ++mode) {
    EXPECT_TRUE(BlendModeNeedDstTexture(static_cast<BlendMode>(mode), true))
        << "mode " << mode << " must not enter the coverage fold (generic blend tail)";
  }
  printf("[FoldEquivalence] gate-admitted modes: %zu\n", admitted);
}

}  // namespace tgfx
