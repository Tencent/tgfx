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

}  // namespace tgfx
