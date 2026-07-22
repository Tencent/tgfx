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
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/Uniform.h"
#include "gpu/UniformData.h"
#include "gtest/gtest.h"

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

}  // namespace tgfx
