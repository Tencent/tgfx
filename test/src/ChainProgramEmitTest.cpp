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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The emit's varying contract: the vertex stage is the stored text with its varying set bent to
// what the fragment reads, and the chain hook writes exactly those varyings. These tests pin the
// contract both stages share, which is the only thing the two texts must agree on.

#include <string>
#include "base/TGFXTest.h"
#include "gpu/aot/chain/ChainProgramEmit.h"

namespace tgfx {
namespace {

const char* StoredVertex = R"(void Chain_vertex(inout Vertex v) {
}
 out vec3 TransformedCoords_0;
 out vec3 TransformedCoords_1;
 out vec3 SourceCoords;
)";

}  // namespace

TGFX_TEST(ChainProgramEmitTest, DropsTheLeafVaryingsTheCompositionDoesNotRead) {
  ChainBindings bindings = {};
  bindings.leafVaryings = {0};
  std::string code = {};
  ASSERT_TRUE(AssembleChainVertexGLSL(bindings, StoredVertex, &code));
  EXPECT_NE(code.find(" out vec3 TransformedCoords_0;"), std::string::npos);
  EXPECT_EQ(code.find(" out vec3 TransformedCoords_1;"), std::string::npos);
  EXPECT_EQ(code.find(" out vec3 SourceCoords;"), std::string::npos);
}

TGFX_TEST(ChainProgramEmitTest, WritesTheContractIntoTheChainHook) {
  ChainBindings bindings = {};
  bindings.leafVaryings = {0, 2};
  bindings.needsSourceCoords = true;
  std::string code = {};
  ASSERT_TRUE(AssembleChainVertexGLSL(bindings, StoredVertex, &code));
  EXPECT_NE(code.find("  TransformedCoords_0 = (CoordTransformMatrix_0 *"), std::string::npos);
  EXPECT_NE(code.find("  TransformedCoords_2 = (CoordTransformMatrix_2 *"), std::string::npos);
  EXPECT_NE(code.find("  SourceCoords = (CoordTransformMatrix_0 *"), std::string::npos);
  EXPECT_NE(code.find(" out vec3 SourceCoords;"), std::string::npos);
}

TGFX_TEST(ChainProgramEmitTest, DeclinesATextWithoutTheChainHook) {
  ChainBindings bindings = {};
  std::string code = {};
  EXPECT_FALSE(AssembleChainVertexGLSL(bindings, "void main() {\n}\n", &code));
}

}  // namespace tgfx
