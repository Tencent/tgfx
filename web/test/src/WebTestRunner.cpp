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

#include <emscripten/emscripten.h>
#include <string>
#include "base/TestEnvironment.h"
#include "gtest/gtest.h"

static bool initialized = false;

// Asyncify (WebGPU only) causes function signature mismatch in FlushSemaphore due to
// instrumentation of async wait chains. Exclude it for WebGPU builds.
#ifdef TGFX_USE_ASYNCIFY
static const char* excludeFilter = "-CanvasTest.FlushSemaphore";
#else
static const char* excludeFilter = "";
#endif

static void initTests() {
  if (initialized) {
    return;
  }
  initialized = true;
  int argc = 1;
  const char* argv[] = {"tgfx-test"};
  testing::AddGlobalTestEnvironment(new tgfx::TestEnvironment());
  testing::InitGoogleTest(&argc, const_cast<char**>(argv));
  if (excludeFilter[0] != '\0') {
    testing::GTEST_FLAG(filter) = excludeFilter;
  }
}

extern "C" {

/**
 * Returns the compiled test mode, backend, and source revision for the browser runner.
 */
EMSCRIPTEN_KEEPALIVE
const char* GetTestConfiguration() {
#ifdef UPDATE_BASELINE
  static const char* mode = "reference";
#else
  static const char* mode = "verify";
#endif
  static const std::string configuration = std::string("{\"mode\":\"") + mode +
                                           "\",\"backend\":\"" TGFX_BACKEND_NAME
                                           "\",\"commit\":\"" TGFX_TEST_COMMIT "\"}";
  return configuration.c_str();
}

EMSCRIPTEN_KEEPALIVE
int RunAllTests() {
  initTests();
  return RUN_ALL_TESTS();
}

EMSCRIPTEN_KEEPALIVE
int RunTest(const char* filter) {
  initTests();
  if (filter != nullptr && filter[0] != '\0') {
    if (excludeFilter[0] != '\0') {
      testing::GTEST_FLAG(filter) = std::string(filter) + ":" + excludeFilter;
    } else {
      testing::GTEST_FLAG(filter) = filter;
    }
  } else if (excludeFilter[0] != '\0') {
    testing::GTEST_FLAG(filter) = excludeFilter;
  }
  return RUN_ALL_TESTS();
}
}
