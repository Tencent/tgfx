/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include "gpu/ir/CoordinateBinder.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(CoordinateBinderTest, NamesFollowTheirIndices) {
  EXPECT_EQ(CoordinateBinder::MatrixUniformName(0), "CoordTransformMatrix_0");
  EXPECT_EQ(CoordinateBinder::MatrixUniformName(3), "CoordTransformMatrix_3");
  EXPECT_EQ(CoordinateBinder::VaryingName(2), "TransformedCoords_2");
}

TEST(CoordinateBinderTest, PerspectiveComesFromEitherSource) {
  Matrix identity = {};
  CoordTransform affine(identity);
  EXPECT_FALSE(CoordinateBinder::HasPerspective(false, affine));
  EXPECT_TRUE(CoordinateBinder::HasPerspective(true, affine));

  Matrix matrix = {};
  matrix.setAll(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 0.2f, 1.0f);
  CoordTransform projective(matrix);
  EXPECT_TRUE(CoordinateBinder::HasPerspective(false, projective));
  EXPECT_EQ(CoordinateBinder::VaryingType(true), SLType::Float3);
  EXPECT_EQ(CoordinateBinder::VaryingType(false), SLType::Float2);
}

TEST(CoordinateBinderTest, CombinedMatrixConcatenatesTransformAndUVMatrix) {
  Matrix transform = {};
  transform.setAll(2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f);
  Matrix uv = {};
  uv.setAll(1.0f, 0.0f, 3.0f, 0.0f, 1.0f, 4.0f);
  const auto combined = CoordinateBinder::CombinedMatrix(CoordTransform(transform), uv);
  EXPECT_FLOAT_EQ(combined[2], 6.0f);
  EXPECT_FLOAT_EQ(combined[5], 8.0f);
}

}  // namespace tgfx
