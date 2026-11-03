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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// FamilyExclusivityTest: proves the dedicated families' acceptance sets are pairwise disjoint
// over the DrawShape field space, which is what makes the kernel table's row order carry no
// semantics ("first acceptor" = "only acceptor"). The enumeration walks the same family list the
// production dispatch uses (TGFX_FOR_EACH_SHADER_FAMILY, the interpreter filtered out by the same
// FamilyHasExtractFacts gate MatchFromFacts applies), and mirrors the table's alpha-only gate, so
// the count below is exactly the count the production lookup would see.
//
// Known boundary: the enumeration runs with ir = nullptr, so the IR-sensitive families (whose
// acceptance also walks the carried IR — YUV, the textured fills, the gradient-coverage shape)
// reject everything in this layer. Their mutual exclusivity rests on three guards instead: the
// per-family field guards proven disjoint here, the IR structure predicates being leaf-type
// specific (a CollectYUVTail leaf is never a CollectTextureTail leaf), and the runtime debug
// audit's per-shape "at most one acceptor" assertion over the full test corpus.

#include <set>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/PermutationMatch.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/DrawShape.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/ShaderFamily.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// The walk's alpha-only gate, mirrored from the table lookup: a non-serving family is skipped on
// an alpha-only target, so the count must apply the same gate or it would report overlaps the
// production dispatch never takes.
int CountAcceptingFamilies(const DrawShape& shape, const char** first, const char** second) {
  int count = 0;
  PermutationMatchFailure failure = PermutationMatchFailure::None;
#define TGFX_COUNT_FAMILY(F)                                                    \
  if constexpr (FamilyHasExtractFacts<F>::value) {                              \
    if (!shape.alphaOnlyTarget || F::ServesAlphaOnly) {                         \
      if (F::MatchFromFacts(shape, &failure)) {                                 \
        if (count == 0) {                                                       \
          *first = F::Name;                                                     \
        } else if (count == 1) {                                                \
          *second = F::Name;                                                    \
        }                                                                       \
        ++count;                                                                \
      }                                                                         \
    }                                                                           \
  }
  TGFX_FOR_EACH_SHADER_FAMILY(TGFX_COUNT_FAMILY)
#undef TGFX_COUNT_FAMILY
  return count;
}

}  // namespace

TGFX_TEST(FamilyExclusivityTest, DedicatedFamiliesArePairwiseExclusiveOverTheFieldSpace) {
  std::vector<const char*> dedicated;
#define TGFX_COLLECT_DEDICATED(F)          \
  if constexpr (FamilyHasExtractFacts<F>::value) { \
    dedicated.push_back(F::Name);          \
  }
  TGFX_FOR_EACH_SHADER_FAMILY(TGFX_COLLECT_DEDICATED)
#undef TGFX_COLLECT_DEDICATED
  ASSERT_FALSE(dedicated.empty());

  const GeometryKind kinds[] = {GeometryKind::Other,
                                GeometryKind::Default,
                                GeometryKind::QuadPerEdgeAA,
                                GeometryKind::NonAARRect,
                                GeometryKind::ComplexNonAARRect,
                                GeometryKind::Ellipse,
                                GeometryKind::ComplexEllipse,
                                GeometryKind::HairlineLine,
                                GeometryKind::HairlineQuad,
                                GeometryKind::Mesh,
                                GeometryKind::RoundStrokeRect,
                                GeometryKind::ShapeInstanced,
                                GeometryKind::AtlasText};
  const Coverage coverages[] = {Coverage::None, Coverage::AARect,  Coverage::RRect,
                                Coverage::DeviceMask, Coverage::LocalMask};
  const XferKind xfers[] = {XferKind::Unsupported, XferKind::None, XferKind::DstTexture,
                            XferKind::FrameBufferFetch};
  // fragmentCount >= colorFPCount holds (the color FPs are a prefix of the list); the (1, 0)
  // form is the "sole FP is the clip" shape the direct-clip fills key off.
  const size_t countPairs[][2] = {{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 3}};

  std::set<const char*> everAccepted;
  long long checked = 0;
  for (auto kind : kinds) {
    for (int bits = 0; bits < 128; ++bits) {
      for (int hasCoverageValue = 0; hasCoverageValue <= 1; ++hasCoverageValue) {
        for (auto coverage : coverages) {
          for (auto xfer : xfers) {
            for (int alphaOnly = 0; alphaOnly <= 1; ++alphaOnly) {
              for (const auto& pair : countPairs) {
                DrawShape shape = {};
                shape.geometry.kind = kind;
                shape.geometry.hasCommonColor = (bits & 1) != 0;
                shape.geometry.hasUVMatrix = (bits & 2) != 0;
                shape.geometry.hasSubset = (bits & 4) != 0;
                shape.geometry.hasUVPerspective = (bits & 8) != 0;
                shape.geometry.isStroke = (bits & 16) != 0;
                shape.geometry.hasTexCoords = (bits & 32) != 0;
                shape.geometry.hasCoverage = (bits & 64) != 0;
                shape.xfer = xfer;
                shape.alphaOnlyTarget = alphaOnly != 0;
                shape.colorFPCount = pair[0];
                shape.fragmentCount = pair[1];
                if (hasCoverageValue != 0) {
                  shape.coverage = coverage;
                }
                const char* first = nullptr;
                const char* second = nullptr;
                const int count = CountAcceptingFamilies(shape, &first, &second);
                if (count == 1) {
                  everAccepted.insert(first);
                }
                if (count > 1) {
                  EXPECT_LE(count, 1)
                      << first << " and " << second << " both accept kind="
                      << static_cast<int>(kind) << " bits=" << bits << " coverage="
                      << (hasCoverageValue != 0 ? static_cast<int>(coverage) : -1)
                      << " xfer=" << static_cast<int>(xfer) << " alphaOnly=" << alphaOnly
                      << " colorFPCount=" << pair[0] << " fragmentCount=" << pair[1];
                }
                ++checked;
              }
            }
          }
        }
      }
    }
  }
  // The space is non-degenerate: the field layer alone routes a meaningful subset of shapes.
  EXPECT_GT(static_cast<long long>(everAccepted.size()), 0LL);
  EXPECT_GT(checked, 0LL);
}

}  // namespace tgfx
