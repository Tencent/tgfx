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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/ShapeKey.h"
#include "gtest/gtest.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

// A leaf that needs no proxy: the planner's stand-in form (see addTextureSource's contract).
AOTTextureParameters IntermediateLeaf() {
  AOTTextureParameters params = {};
  params.isIntermediate = true;
  params.uvMatrix = Matrix::I();
  return params;
}

AOTEffectGraph TextureGraph(const AOTTextureParameters& leaf, bool textureFromMatrix) {
  AOTNodeBuilder builder = {};
  AOTEffectGraph graph = {};
  AOTNodeID color = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&color)) {
    return graph;
  }
  AOTNodeID matrix = AOTNodeID::Invalid();
  AOTColorMatrixParameters matrixParams = {};
  if (!builder.addColorMatrix(color, matrixParams, &matrix)) {
    return graph;
  }
  AOTNodeID texture = AOTNodeID::Invalid();
  if (!builder.addTextureSource(textureFromMatrix ? matrix : color, leaf, &texture)) {
    return graph;
  }
  builder.finish(texture, &graph);
  return graph;
}

AOTEffectGraph UnaryGraph(bool luma) {
  AOTNodeBuilder builder = {};
  AOTEffectGraph graph = {};
  AOTNodeID color = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&color)) {
    return graph;
  }
  AOTNodeID node = AOTNodeID::Invalid();
  if (luma) {
    ChainISA::LumaData params = {};
    builder.addLuma(color, params, &node);
  } else {
    AOTColorMatrixParameters params = {};
    builder.addColorMatrix(color, params, &node);
  }
  builder.finish(node, &graph);
  return graph;
}

BytesKey GraphKey(const AOTEffectGraph& graph) {
  BytesKey key = {};
  HashGraphStructure(graph, &key);
  return key;
}

const std::array<float, 20> kIdentity = {1, 0, 0, 0, 0,  //
                                         0, 1, 0, 0, 0,  //
                                         0, 0, 1, 0, 0,  //
                                         0, 0, 0, 1, 0};

}  // namespace

TGFX_TEST(AOTShapeKeyTest, StructureIgnoresValues) {
  auto first = IntermediateLeaf();
  auto second = IntermediateLeaf();
  second.uvMatrix = Matrix::MakeScale(3.0f, 2.0f);
  second.alphaStart = Point{0.25f, 0.5f};
  second.samplerState = SamplerState(TileMode::Repeat, TileMode::Mirror);
  auto firstGraph = TextureGraph(first, true);
  auto secondGraph = TextureGraph(second, true);
  ASSERT_EQ(firstGraph.nodeCount(), 3u);
  ASSERT_EQ(secondGraph.nodeCount(), 3u);
  EXPECT_TRUE(GraphKey(firstGraph) == GraphKey(secondGraph));
}

TGFX_TEST(AOTShapeKeyTest, StructureDistinguishesSamplingFacts) {
  const auto base = IntermediateLeaf();
  const auto baseKey = GraphKey(TextureGraph(base, false));

  auto tiled = base;
  tiled.samplingKind = AOTTextureSamplingKind::Tiled;
  EXPECT_FALSE(baseKey == GraphKey(TextureGraph(tiled, false)));

  auto device = base;
  device.samplingKind = AOTTextureSamplingKind::Device;
  EXPECT_FALSE(baseKey == GraphKey(TextureGraph(device, false)));

  auto alpha = base;
  alpha.isAlphaOnly = true;
  EXPECT_FALSE(baseKey == GraphKey(TextureGraph(alpha, false)));

  auto perspective = base;
  perspective.hasPerspective = true;
  EXPECT_FALSE(baseKey == GraphKey(TextureGraph(perspective, false)));

  // The presence of a subset is structural; its rectangle is a value.
  auto subset = base;
  subset.subset = Rect::MakeXYWH(1, 2, 3, 4);
  EXPECT_FALSE(baseKey == GraphKey(TextureGraph(subset, false)));

  auto yuv = base;
  yuv.isYUV = true;
  yuv.yuvFormat = 1;
  EXPECT_FALSE(baseKey == GraphKey(TextureGraph(yuv, false)));
}

TGFX_TEST(AOTShapeKeyTest, StructureDistinguishesEdges) {
  // Identical node kinds and count: only the texture's input edge differs.
  auto fromMatrix = TextureGraph(IntermediateLeaf(), true);
  auto fromColor = TextureGraph(IntermediateLeaf(), false);
  ASSERT_EQ(fromMatrix.nodeCount(), fromColor.nodeCount());
  EXPECT_FALSE(GraphKey(fromMatrix) == GraphKey(fromColor));
}

TGFX_TEST(AOTShapeKeyTest, StructureDistinguishesKinds) {
  auto matrix = UnaryGraph(false);
  auto luma = UnaryGraph(true);
  ASSERT_EQ(matrix.nodeCount(), luma.nodeCount());
  EXPECT_FALSE(GraphKey(matrix) == GraphKey(luma));
}

TGFX_TEST(AOTShapeKeyTest, StructureIgnoresColorMatrixValues) {
  AOTNodeBuilder firstBuilder = {};
  AOTEffectGraph first = {};
  AOTNodeID firstColor = AOTNodeID::Invalid();
  ASSERT_TRUE(firstBuilder.addGeometryColor(&firstColor));
  AOTColorMatrixParameters firstParams = {};
  firstParams.matrix = kIdentity;
  AOTNodeID firstNode = AOTNodeID::Invalid();
  ASSERT_TRUE(firstBuilder.addColorMatrix(firstColor, firstParams, &firstNode));
  ASSERT_TRUE(firstBuilder.finish(firstNode, &first));

  AOTNodeBuilder secondBuilder = {};
  AOTEffectGraph second = {};
  AOTNodeID secondColor = AOTNodeID::Invalid();
  ASSERT_TRUE(secondBuilder.addGeometryColor(&secondColor));
  AOTColorMatrixParameters secondParams = {};
  secondParams.matrix.fill(0.5f);
  AOTNodeID secondNode = AOTNodeID::Invalid();
  ASSERT_TRUE(secondBuilder.addColorMatrix(secondColor, secondParams, &secondNode));
  ASSERT_TRUE(secondBuilder.finish(secondNode, &second));

  EXPECT_TRUE(GraphKey(first) == GraphKey(second));
}

}  // namespace tgfx
