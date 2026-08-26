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

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/Uniform.h"
#include "gpu/UniformData.h"
#include "gtest/gtest.h"
#include "utils/TestUtils.h"

namespace tgfx {

// Reads the int32 stored at the field named `name` in the data's buffer. Uses the private field map
// (accessible in tests via -fno-access-control) to locate the offset.
static int32_t ReadInt(const UniformData& data, uint8_t* buffer, const std::string& name) {
  const auto* field = data.findField(name);
  EXPECT_NE(field, nullptr);
  int32_t value = 0;
  std::memcpy(&value, buffer + field->offset, sizeof(value));
  return value;
}

TGFX_TEST(UniformDataTest, SetDataOptionalBehavior) {
  std::vector<Uniform> uniforms = {Uniform("Present", UniformFormat::Int),
                                   Uniform("Sentinel", UniformFormat::Int)};
  UniformData data(uniforms);
  std::array<uint8_t, 256> buffer = {};
  ASSERT_GE(buffer.size(), data.size());
  data.setBuffer(buffer.data());

  // A sentinel field lets us prove that a missing optional write does not corrupt other fields.
  data.setData("Sentinel", static_cast<int32_t>(0x5A5A5A5A));

  // 1. Optional uniform that exists is written normally.
  data.setDataOptional("Present", static_cast<int32_t>(42));
  EXPECT_EQ(ReadInt(data, buffer.data(), "Present"), 42);

  // 2. Optional uniform that is absent is silently ignored: no crash, no error log, and no other
  //    field is touched.
  data.setDataOptional("Missing", static_cast<int32_t>(99));
  EXPECT_EQ(ReadInt(data, buffer.data(), "Sentinel"), 0x5A5A5A5A);
  EXPECT_EQ(data.hasField("Missing"), false);

  // 3. Required setData for an existing uniform still writes (baseline behavior unchanged).
  data.setData("Present", static_cast<int32_t>(7));
  EXPECT_EQ(ReadInt(data, buffer.data(), "Present"), 7);
}

// Verifies that the std140 layout rules applied by UniformData match the GPU-side uniform block:
// scalar fields are aligned by their own size, while array fields align every element to a
// 16-byte boundary and occupy elementSize * count bytes.
TGFX_TEST_PRIVATE(UniformDataTest, Std140Layout) {
  TGFX_PRIVATE_ACCESS(
      std::vector<Uniform> uniforms; uniforms.emplace_back("scale", UniformFormat::Float);
      uniforms.emplace_back("kernel", UniformFormat::Float4, 3);
      uniforms.emplace_back("offset", UniformFormat::Float2);
      auto data = std::unique_ptr<UniformData>(new UniformData(std::move(uniforms)));

      ASSERT_TRUE(data->fieldMap.find("scale") != data->fieldMap.end());
      ASSERT_TRUE(data->fieldMap.find("kernel") != data->fieldMap.end());
      ASSERT_TRUE(data->fieldMap.find("offset") != data->fieldMap.end());

      // Float is 4-byte aligned and starts at offset 0.
      EXPECT_EQ(data->fieldMap["scale"].offset, 0u); EXPECT_EQ(data->fieldMap["scale"].size, 4u);
      EXPECT_EQ(data->fieldMap["scale"].align, 4u);

      // The Float4 array follows std140: the whole array is aligned to 16 bytes and each of the 3
      // elements occupies 16 bytes contiguously.
      EXPECT_EQ(data->fieldMap["kernel"].offset, 16u);
      EXPECT_EQ(data->fieldMap["kernel"].size, 48u); EXPECT_EQ(data->fieldMap["kernel"].align, 16u);

      // Float2 is 8-byte aligned and starts right after the array.
      EXPECT_EQ(data->fieldMap["offset"].offset, 64u); EXPECT_EQ(data->fieldMap["offset"].size, 8u);

      // The buffer size is rounded up to 16 bytes.
      EXPECT_EQ(data->size(), 80u);)
}

// Verifies that setData() writes the array contiguously at the expected byte offsets: the
// CPU-side float array maps 1:1 onto the std140 vec4 array in the GPU uniform block, because
// std140 lays out vec4 array elements back to back with no padding between them.
TGFX_TEST_PRIVATE(UniformDataTest, SetArrayData) {
  TGFX_PRIVATE_ACCESS(
      std::vector<Uniform> uniforms; uniforms.emplace_back("kernel", UniformFormat::Float4, 3);
      auto data = std::unique_ptr<UniformData>(new UniformData(std::move(uniforms)));
      ASSERT_EQ(data->size(), 48u);

      std::array<float, 12> kernel = {}; for (size_t i = 0; i < kernel.size(); ++i) {
        kernel[i] = static_cast<float>(i + 1);
      } std::vector<uint8_t>
          buffer(data->size(), 0);
      data->setBuffer(buffer.data()); data->setData("kernel", kernel);

      // The 12 floats are written back to back, float i occupying bytes [i * 4, i * 4 + 4).
      for (size_t i = 0; i < kernel.size(); ++i) {
        float value = 0.0f;
        memcpy(&value, buffer.data() + i * 4, sizeof(float));
        EXPECT_FLOAT_EQ(value, kernel[i]);
      })
}

// Verifies the array layout for the gaussian blur kernel table: 17 vec4 slots hold the
// half-kernel capacity of 65 floats, and the radius scalar is packed right after the array.
TGFX_TEST_PRIVATE(UniformDataTest, BlurKernelArrayLayout) {
  TGFX_PRIVATE_ACCESS(
      std::vector<Uniform> uniforms; uniforms.emplace_back("Kernel", UniformFormat::Float4, 17);
      uniforms.emplace_back("Radius", UniformFormat::Int);
      auto data = std::unique_ptr<UniformData>(new UniformData(std::move(uniforms)));

      ASSERT_TRUE(data->fieldMap.find("Kernel") != data->fieldMap.end());
      ASSERT_TRUE(data->fieldMap.find("Radius") != data->fieldMap.end());

      EXPECT_EQ(data->fieldMap["Kernel"].offset, 0u);
      EXPECT_EQ(data->fieldMap["Kernel"].size, 272u);
      EXPECT_EQ(data->fieldMap["Kernel"].align, 16u);
      EXPECT_EQ(data->fieldMap["Radius"].offset, 272u);
      EXPECT_EQ(data->fieldMap["Radius"].size, 4u); EXPECT_EQ(data->size(), 288u);)
}

// Verifies the boundary case of count = 1, which must behave like a scalar field of the same
// format: Float4 is still aligned to 16 bytes but occupies only a single element.
TGFX_TEST_PRIVATE(UniformDataTest, SingleElementArray) {
  TGFX_PRIVATE_ACCESS(
      std::vector<Uniform> uniforms; uniforms.emplace_back("color", UniformFormat::Float4, 1);
      auto data = std::unique_ptr<UniformData>(new UniformData(std::move(uniforms)));

      ASSERT_TRUE(data->fieldMap.find("color") != data->fieldMap.end());
      EXPECT_EQ(data->fieldMap["color"].offset, 0u); EXPECT_EQ(data->fieldMap["color"].size, 16u);
      EXPECT_EQ(data->fieldMap["color"].align, 16u); EXPECT_EQ(data->size(), 16u);)
}

// Verifies the std140 alignment of Float3, whose element size is 12 bytes but whose alignment is
// 16 bytes: the Float3 field starts at a 16-byte boundary, and a following scalar is placed right
// after it because std140 only requires each field to satisfy its own alignment.
TGFX_TEST_PRIVATE(UniformDataTest, Float3Alignment) {
  TGFX_PRIVATE_ACCESS(
      std::vector<Uniform> uniforms; uniforms.emplace_back("normal", UniformFormat::Float3);
      uniforms.emplace_back("scale", UniformFormat::Float);
      auto data = std::unique_ptr<UniformData>(new UniformData(std::move(uniforms)));

      ASSERT_TRUE(data->fieldMap.find("normal") != data->fieldMap.end());
      ASSERT_TRUE(data->fieldMap.find("scale") != data->fieldMap.end());
      EXPECT_EQ(data->fieldMap["normal"].offset, 0u); EXPECT_EQ(data->fieldMap["normal"].size, 12u);
      EXPECT_EQ(data->fieldMap["normal"].align, 16u);
      EXPECT_EQ(data->fieldMap["scale"].offset, 12u); EXPECT_EQ(data->fieldMap["scale"].size, 4u);
      EXPECT_EQ(data->size(), 16u);)
}

// Verifies that a Float4 field following a Float3 is pushed to the next 16-byte boundary, the
// std140 padding branch where a field's own alignment exceeds the previous field's rounded size.
TGFX_TEST_PRIVATE(UniformDataTest, Float3FollowedByHighAlignment) {
  TGFX_PRIVATE_ACCESS(
      std::vector<Uniform> uniforms; uniforms.emplace_back("normal", UniformFormat::Float3);
      uniforms.emplace_back("color", UniformFormat::Float4);
      auto data = std::unique_ptr<UniformData>(new UniformData(std::move(uniforms)));

      ASSERT_TRUE(data->fieldMap.find("normal") != data->fieldMap.end());
      ASSERT_TRUE(data->fieldMap.find("color") != data->fieldMap.end());
      EXPECT_EQ(data->fieldMap["normal"].offset, 0u); EXPECT_EQ(data->fieldMap["normal"].size, 12u);
      EXPECT_EQ(data->fieldMap["color"].offset, 16u); EXPECT_EQ(data->fieldMap["color"].size, 16u);
      EXPECT_EQ(data->size(), 32u);)
}

}  // namespace tgfx
