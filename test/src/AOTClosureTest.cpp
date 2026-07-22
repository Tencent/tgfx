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

#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/AOTClosureVerifier.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "utils/TestUtils.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

static std::string ClosureBundlePath() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  return "resources/shaders/shader_bundle." + backend + ".bin";
}

// Stage 1 closure scope: the four straight-line (直筒) Kernel families. Every structural Key these
// shaders can emit must exist in the bundle, otherwise the runtime matcher could produce a Key with
// no artifact and silently fall back. Verifying missing == 0 is the standing definition of "the AOT
// path is verifiably complete" for this Kernel set.
static const std::vector<std::string>& Stage1Kernels() {
  static const std::vector<std::string> kernels = {"TextureFillShader", "QuadColorFillShader",
                                                   "TexturedColorMatrixShader",
                                                   "TexturedLumaShader", "SolidColorFillShader"};
  return kernels;
}

TGFX_TEST(AOTClosureTest, Stage1StraightLineKernelsAreClosed) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ClosureBundlePath())));

  auto result = AOTClosureVerifier::Verify(cache, Stage1Kernels());
  EXPECT_GT(result.expectedCount, 0u);
  for (const auto& entry : result.missing) {
    ADD_FAILURE() << "closure hole: " << entry.shaderName << (entry.isVertex ? " vert#" : " frag#")
                  << entry.permutationIndex;
  }
  EXPECT_EQ(result.missingCount, 0u);
  cache->unload();
}

}  // namespace tgfx
