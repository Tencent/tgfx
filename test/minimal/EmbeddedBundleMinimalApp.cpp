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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// A minimal consumer of the precompiled-shader embedding (audit F07). It links nothing but the
// tgfx target itself: no forced loading, no whole archive, no test-support libraries. The build
// passes exactly because the bundle objects now travel with the library through ordinary
// symbol resolution (Context -> EmbeddedShaderBundles::GetBundle -> RegisterAllEmbeddedBundles
// -> per-backend registrations). If that chain breaks, this binary fails to link or fails the
// startup assertion below. Currently GL-only because it constructs the device directly.

#include <array>
#include <cstdio>
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLDevice.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  auto device = tgfx::GLDevice::Make();
  if (device == nullptr) {
    printf("FAIL: no GL device\n");
    return 1;
  }
  auto* context = device->lockContext();
  if (context == nullptr) {
    printf("FAIL: no context\n");
    return 1;
  }
  auto* cache = context->precompiledShaderCache();
  if (!cache->isLoaded()) {
    printf("FAIL: the embedded bundle is not loaded at startup\n");
    device->unlock();
    return 1;
  }
  // Draw one AOT-servable frame and confirm the precompiled path actually executed. The scene
  // (image + color matrix filter) is the same shape the consistency suite proves is served by a
  // precompiled artifact; a plain solid rect would be rejected by the coverage contract.
  auto surface = tgfx::Surface::Make(context, 64, 64);
  if (surface == nullptr) {
    printf("FAIL: no surface\n");
    device->unlock();
    return 1;
  }
  tgfx::Bitmap bitmap = {};
  if (!bitmap.allocPixels(64, 64)) {
    printf("FAIL: no bitmap\n");
    device->unlock();
    return 1;
  }
  auto* pixels = static_cast<uint32_t*>(bitmap.lockPixels());
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      pixels[y * 64 + x] =
          static_cast<uint32_t>((255u << 24) | (y * 4u << 16) | (x * 4u << 8) | 128u);
    }
  }
  bitmap.unlockPixels();
  auto image = tgfx::Image::MakeFrom(bitmap);
  if (image == nullptr) {
    printf("FAIL: no image\n");
    device->unlock();
    return 1;
  }
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
                                       0, 0, 0, 1, 0};
  tgfx::Paint paint = {};
  paint.setColorFilter(tgfx::ColorFilter::Matrix(swapRedBlue));
  surface->getCanvas()->drawImage(image, 0, 0, &paint);
  context->flushAndSubmit(true);
  const auto& stats = context->globalCache()->programStats();
  if (stats.precompiledArtifactCreations < 1) {
    printf("FAIL: no precompiled artifact was created (creations=%u)\n",
           static_cast<unsigned>(stats.precompiledArtifactCreations));
    device->unlock();
    return 1;
  }
  if (stats.programBuilderCreations != 0) {
    printf("FAIL: a runtime-built program appeared (creations=%u)\n",
           static_cast<unsigned>(stats.programBuilderCreations));
    device->unlock();
    return 1;
  }
  printf("PASS: embedded bundle linked through tgfx only, loaded and serving\n");
  device->unlock();
  return 0;
}
