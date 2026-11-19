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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include <limits>
#include "layers/layerstyles/GlassUDF.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST_PRIVATE(GlassDepthTest, MaxDisplacementUsesLayerPixelThickness) {
  auto style = GlassStyle::Make(100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  TGFX_PRIVATE_ACCESS({
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(180.0f), 100.0f);
    style->setDepth(150.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(180.0f), 150.0f);
    style->setDepth(250.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(180.0f), 180.0f);
    style->setRefraction(50.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(180.0f), 90.0f);
    style->setDepth(-30.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(180.0f), 0.0f);
    EXPECT_TRUE(std::isfinite(style->getRefractionMaxDisplacement(180.0f)));
  })
}

TGFX_TEST_PRIVATE(GlassDepthTest, NonFiniteDepthNeverReachesRendering) {
  auto style = GlassStyle::Make(100.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f,
                                0.0f, 0.0f);
  TGFX_PRIVATE_ACCESS({
    EXPECT_TRUE(std::isnan(style->depth()));
    EXPECT_FLOAT_EQ(style->getGlassThickness(90.0f), 0.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(90.0f), 0.0f);
  })

  style->setDepth(std::numeric_limits<float>::infinity());
  TGFX_PRIVATE_ACCESS({
    EXPECT_TRUE(std::isinf(style->depth()));
    EXPECT_FLOAT_EQ(style->getGlassThickness(90.0f), 90.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(90.0f), 90.0f);
  })

  style->setDepth(-std::numeric_limits<float>::infinity());
  TGFX_PRIVATE_ACCESS({
    EXPECT_TRUE(std::isinf(style->depth()));
    EXPECT_FLOAT_EQ(style->getGlassThickness(90.0f), 0.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(90.0f), 0.0f);
  })

  style->setDepth(60.0f);
  TGFX_PRIVATE_ACCESS({
    EXPECT_FLOAT_EQ(style->getGlassThickness(std::numeric_limits<float>::infinity()), 0.0f);
    EXPECT_FLOAT_EQ(style->getRefractionMaxDisplacement(std::numeric_limits<float>::quiet_NaN()),
                    0.0f);
  })
}

TGFX_TEST(GlassDepthTest, UDFScaleRespectsTextureLimit) {
  constexpr float MaxAllowedDimension = 1908.0f;
  constexpr float MinShortSideTexels = 256.0f;

  auto small = CalculateGlassUDFScale(100.0f, 100.0f, MaxAllowedDimension, MinShortSideTexels);
  EXPECT_FLOAT_EQ(small, 2.56f);
  EXPECT_FLOAT_EQ(100.0f * small, MinShortSideTexels);

  auto narrow = CalculateGlassUDFScale(600.0f, 20.0f, MaxAllowedDimension, MinShortSideTexels);
  EXPECT_NEAR(600.0f * narrow, MaxAllowedDimension, 0.001f);
  EXPECT_LT(20.0f * narrow, MinShortSideTexels);

  auto extreme = CalculateGlassUDFScale(9000.0f, 20.0f, MaxAllowedDimension, MinShortSideTexels);
  EXPECT_NEAR(9000.0f * extreme, MaxAllowedDimension, 0.001f);
  EXPECT_LE(9000.0f * extreme, MaxAllowedDimension);

  EXPECT_FLOAT_EQ(CalculateGlassUDFScale(0.0f, 20.0f, MaxAllowedDimension, MinShortSideTexels),
                  0.0f);
  EXPECT_FLOAT_EQ(CalculateGlassUDFScale(600.0f, 20.0f, 0.0f, MinShortSideTexels), 0.0f);
}

}  // namespace tgfx
