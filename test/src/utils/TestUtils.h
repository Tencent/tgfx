/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#pragma once

#include "base/TGFXTest.h"
#include "core/PixelBuffer.h"
#include "gtest/gtest.h"
#include "tgfx/core/Clock.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Pixmap.h"
#include "utils/Baseline.h"
#include "utils/ContextScope.h"
#include "utils/DevicePool.h"
#include "utils/ProjectPath.h"

namespace tgfx {

#ifdef TGFX_TEST_ACCESS_PRIVATE
#define TGFX_TEST_PRIVATE(suite, name) TGFX_TEST(suite, name)
#define TGFX_PRIVATE_ACCESS(...) __VA_ARGS__
#else
#define TGFX_TEST_PRIVATE(suite, name) static void Disabled_##suite##_##name()
#define TGFX_PRIVATE_ACCESS(...) /* skipped on MSVC */
#endif

std::shared_ptr<ImageCodec> MakeImageCodec(const std::string& path);

TGFX_PRIVATE_ACCESS(std::shared_ptr<ImageCodec> MakeNativeCodec(const std::string& path);)

std::shared_ptr<Image> MakeImage(const std::string& path);

std::shared_ptr<Typeface> MakeTypeface(const std::string& path);

std::shared_ptr<Data> ReadFile(const std::string& path);

void SaveFile(std::shared_ptr<Data> data, const std::string& key);

void SaveWebpFile(std::shared_ptr<Data> data, const std::string& key);

void SaveImage(std::shared_ptr<PixelBuffer> pixelBuffer, const std::string& key);

void SaveImage(const Bitmap& bitmap, const std::string& key);

void SaveImage(const Pixmap& pixmap, const std::string& key);

void RemoveImage(const std::string& key);

void RemoveFile(const std::string& key);

std::shared_ptr<Image> ScaleImage(const std::shared_ptr<Image>& image, float scale,
                                  const SamplingOptions& options = {});

class Context;
class GlobalCache;
class PrecompiledShaderCache;
class Bitmap;

/**
 * Premultiplied-alpha legality of a bitmap's pixels: every pixel must satisfy RGB <= A and
 * A == 0 => RGB == 0. The channel byte order (RGBA vs BGRA) does not matter: the alpha is the
 * top byte of the packed value in both, and the check bounds all three color bytes against it.
 * Illegal-premul fixtures make the reference semantics undefined at alpha boundaries (audit
 *), so new fixtures assert this and existing ones were
 * fixed. The result carries up to four offending samples for the failure message.
 */
testing::AssertionResult BitmapPremulLegal(const Bitmap& bitmap);

/**
 * Counts pixels with strictly fractional alpha (0 < a < 255): the observable footprint of an AA
 * edge (or a fractional mask) in a rendered result. A coverage test that claims to probe AA
 * edges must show this count is non-zero on its reference (non-vacuity) —
 * an all-or-nothing image proves nothing about fractional coverage handling.
 */
size_t CountFractionalAlphaPixels(const Bitmap& bitmap);

/**
 * True when the context's renderer is SwiftShader (the software-GL validation environment).
 * SwiftShader's software compiler has a fixed internal pool that large AOT variants exhaust
 * ("memory exhausted") — those variants compile fine on every hardware GLES driver, so the
 * limitation is environmental, not a product defect. Tests that assert the AOT routing of the
 * affected variants skip on this environment (SKIP_ON_SWIFTSHADER below); the plain-path and
 * byte-parity coverage they can still provide stays active.
 */
bool IsSwiftShaderContext(Context* context);

/**
 * Environment-level SwiftShader detection (no live context needed): the process's rendering
 * backend is fixed, so one probe through the shared device pool decides it. For tests whose
 * context is created inside render helpers.
 */
bool IsSwiftShaderEnvironment();

/**
 * Environment gate for tests that assert the AOT routing of variants SwiftShader cannot
 * compile (the software-compiler pool, not the shader sources, is the blocker — the same
 * tests run green on hardware GL/Metal backends). Skipping is recorded with the reason so
 * the excluded count stays auditable in the suite output.
 */
#define SKIP_ON_SWIFTSHADER(context)                                   \
  do {                                                                 \
    if (IsSwiftShaderContext(context)) {                               \
      GTEST_SKIP() << "SwiftShader software-compiler pool: large AOT " \
                      "variants do not compile (environment limit)";   \
    }                                                                  \
  } while (false)

/**
 * Same gate for tests whose context lives inside a render helper rather than a local scope.
 */
#define SKIP_ON_SWIFTSHADER_ENV()                                      \
  do {                                                                 \
    if (IsSwiftShaderEnvironment()) {                                  \
      GTEST_SKIP() << "SwiftShader software-compiler pool: large AOT " \
                      "variants do not compile (environment limit)";   \
    }                                                                  \
  } while (false)

/**
 * Pauses AOT statistics recording and program-creation counting on the given context for the
 * scope's lifetime when active is true. Wrap intentional JIT reference renders (bundle unloaded)
 * with this so they do not enter the AOT hit-rate accounting; the pause always unwinds even if
 * an assertion aborts the helper.
 */
class ScopedAOTStatsPause {
 public:
  ScopedAOTStatsPause(Context* context, bool active);
  ~ScopedAOTStatsPause();

 private:
  PrecompiledShaderCache* cache = nullptr;
  GlobalCache* globalCache = nullptr;
  bool prevCachePaused = false;
  bool prevGlobalPaused = false;
};

// Unlike a stats pause, deliberate marking preserves raw counters for local JIT assertions.
class ScopedAOTDeliberateMiss {
 public:
  explicit ScopedAOTDeliberateMiss(Context* context, bool active = true);
  ~ScopedAOTDeliberateMiss();
  ScopedAOTDeliberateMiss(const ScopedAOTDeliberateMiss&) = delete;
  ScopedAOTDeliberateMiss& operator=(const ScopedAOTDeliberateMiss&) = delete;

 private:
  PrecompiledShaderCache* cache = nullptr;
  bool previous = false;
};

}  // namespace tgfx
