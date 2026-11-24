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

#include "base/TGFXTest.h"
#include "utils/Baseline.h"

namespace tgfx {

TGFX_TEST(BaselineTest, MissingCacheFails) {
  EXPECT_FALSE(Baseline::CompareReference("pixels", "version", "version", "pixels", false));
  EXPECT_FALSE(Baseline::CompareReference("pixels", "version", "", "", false));
  EXPECT_FALSE(Baseline::CompareReference("pixels", "", "", "", false));
}

TGFX_TEST(BaselineTest, MissingBaselineFails) {
  EXPECT_FALSE(Baseline::CompareReference("pixels", "", "", "", true));
  EXPECT_FALSE(Baseline::CompareReference("pixels", "", "version", "pixels", true));
}

TGFX_TEST(BaselineTest, NewKeyInAvailableCachePasses) {
  EXPECT_TRUE(Baseline::CompareReference("pixels", "version", "", "", true));
}

TGFX_TEST(BaselineTest, MatchingVersionsComparePixels) {
  EXPECT_TRUE(Baseline::CompareReference("pixels", "version", "version", "pixels", true));
  EXPECT_FALSE(Baseline::CompareReference("different", "version", "version", "pixels", true));
  EXPECT_FALSE(Baseline::CompareReference("pixels", "version", "version", "", true));
  EXPECT_FALSE(Baseline::CompareReference("", "version", "version", "", true));
}

TGFX_TEST(BaselineTest, KnownVersionChangeSkipsPixelComparison) {
  EXPECT_TRUE(Baseline::CompareReference("different", "new", "old", "pixels", true));
  EXPECT_TRUE(Baseline::CompareReference("different", "new", "old", "", true));
}

}  // namespace tgfx
