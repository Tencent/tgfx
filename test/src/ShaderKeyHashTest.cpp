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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Fixed test vectors for the shared shader key/blob encoding (src/gpu/ShaderKeyHash.h). The
// encoding is the wire contract between offline bundles and the runtime loader: any accidental
// change to the serialization order, the length prefixes, or the FNV-1a constants silently
// invalidates every existing bundle, so the golden hashes below pin the exact bytes. Values were
// computed independently of the implementation (dual-seed FNV-1a over the documented layout) and
// must not be regenerated from the implementation itself.

#include <cstdint>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/ShaderKeyHash.h"

namespace tgfx {

static bool HashEquals(const ShaderKeyHash& hash, uint64_t hi, uint64_t lo) {
  return hash.hi == hi && hash.lo == lo;
}

TGFX_TEST(ShaderKeyHashTest, FixedKeyVectors) {
  // ("SolidColorFillShader", 5, "metal")
  EXPECT_TRUE(HashEquals(ComputeShaderKeyHash("SolidColorFillShader", 5, "metal"),
                         0xDEE4F0983B534653ULL, 0xCB9782F546AC6F0AULL));
  // ("PointwiseChainShader", 39, "opengl")
  EXPECT_TRUE(HashEquals(ComputeShaderKeyHash("PointwiseChainShader", 39, "opengl"),
                         0x61245B7849A5B482ULL, 0x8C2FF028C801CA1DULL));
  // Empty name/index/tag still hashes the four zero length prefixes.
  EXPECT_TRUE(
      HashEquals(ComputeShaderKeyHash("", 0, ""), 0x22FCAEB077AE4EA2ULL, 0xA0586AD2763A6E7DULL));
}

TGFX_TEST(ShaderKeyHashTest, FixedBlobVector) {
  const std::vector<uint8_t> blob = {0x00, 0x01, 0x02, 0xFF, 0x80, 0x07};
  EXPECT_TRUE(HashEquals(ComputeBlobHash(blob), 0x509FF68E5D100BB7ULL, 0xC1B6E092E5B00610ULL));
}

TGFX_TEST(ShaderKeyHashTest, StageSuffixDistinct) {
  const auto plain = ComputeShaderKeyHash("TextureFillShader", 3, "metal");
  const auto vert = ComputeVertexKeyHash("TextureFillShader", 3, "metal");
  const auto frag = ComputeFragmentKeyHash("TextureFillShader", 3, "metal");
  EXPECT_FALSE(HashEquals(plain, vert.hi, vert.lo));
  EXPECT_FALSE(HashEquals(plain, frag.hi, frag.lo));
  EXPECT_FALSE(HashEquals(vert, frag.hi, frag.lo));
}

}  // namespace tgfx
