//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

// The entry of ShaderBuildToolTest, the unit tests of tools/shader_build_tool.
//
// These tests exercise the tool's modules and the contracts it shares with the runtime. They need
// no GPU, no window and no baseline environment, which is why they have their own main instead of
// the TGFXTest fixture the render tests of test/src use. The target links shader_build_tool_core
// (every source of the tool except its main) next to the runtime library, so a test can write a
// bundle with the tool's writer and read it back with the runtime's loader.

#include "gtest/gtest.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
