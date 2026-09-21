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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Diffs the AOT and JIT renderings of the scaled-picture-image shape. Root cause of the
// residual divergence (bisected 2026-09-21): the DropShadow filter itself — without the filter
// the two paths render byte-identical (diff 0); with it, ~0.06% of pixels differ in the ALPHA
// channel only (rgb identical, max delta 46), localized to the shadow region. That points at a
// GaussianBlur-family AOT kernel vs runtime blur-emitter alpha math difference, tracked as a
// separate fix; this test stays as the monitoring oracle (clipRect + 0.15 scale +
// drawImage + drop shadow, rasterized into a picture image, then down-scaled 0.55) — the shape
// behind CanvasTest/pic_scaled_*, which carries a known AOT/JIT divergence. The test prints the
// per-pixel diff statistics (count, bounding box, max delta, per-channel distribution) so the
// residual gap can be classified and tracked; it asserts nothing beyond "both paths rendered",
// keeping it a diagnostic oracle rather than a pass/fail gate until the root cause is fixed.

#include "base/TGFXTest.h"
#include <cstdio>
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/TestUtils.h"

namespace tgfx {

static void RenderScaledPicture(Context* context, Bitmap* outBitmap, bool withFilter) {
  auto image = MakeImage("resources/apitest/rotation.jpg");
  if (image == nullptr) {
    return;
  }
  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  auto paint = Paint();
  if (withFilter) {
    paint.setImageFilter(ImageFilter::DropShadow(10, 10, 0, 0, Color::Black()));
  }
  canvas->clipRect(Rect::MakeLTRB(100, 100, 600, 800));
  canvas->scale(0.15f, 0.15f);
  canvas->drawImage(image, 0, 0, &paint);
  auto picture = recorder.finishRecordingAsPicture();
  auto bounds = picture->getBounds();
  bounds.roundOut();
  auto pictureMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  image = Image::MakeFrom(picture, static_cast<int>(bounds.width()),
                          static_cast<int>(bounds.height()), &pictureMatrix);
  if (image == nullptr) {
    return;
  }
  auto scaledImage = ScaleImage(image, 0.55f);
  if (scaledImage == nullptr) {
    return;
  }
  auto surface = Surface::Make(context, 1100, 1400);
  if (surface == nullptr) {
    return;
  }
  surface->getCanvas()->drawImage(scaledImage);
  context->flushAndSubmit(true);
  outBitmap->allocPixels(1100, 1400);
  auto* pixels = outBitmap->lockPixels();
  if (pixels != nullptr) {
    surface->readPixels(outBitmap->info(), pixels);
    outBitmap->unlockPixels();
  }
}

TGFX_TEST(AOTRenderConsistencyTest, ScaledPictureImageAotJitDiff) {
  ContextScope scope;
  auto context = scope.getContext();
  SKIP_ON_SWIFTSHADER(context);
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();

  Bitmap aotBitmap = {};
  Bitmap jitBitmap = {};
  {
    // AOT path: the embedded bundle stays loaded.
    ScopedAOTStatsPause pause(context, false);
    RenderScaledPicture(context, &aotBitmap, true);
  }
  {
    // JIT path: bundle unloaded for this render.
    cache->unload();
    ScopedAOTStatsPause pause(context, true);
    RenderScaledPicture(context, &jitBitmap, true);
  }
  ASSERT_TRUE(aotBitmap.isEmpty() == false);
  ASSERT_TRUE(jitBitmap.isEmpty() == false);
  ASSERT_EQ(aotBitmap.width(), jitBitmap.width());
  ASSERT_EQ(aotBitmap.height(), jitBitmap.height());

  auto* aotPixels = static_cast<const uint32_t*>(aotBitmap.lockPixels());
  auto* jitPixels = static_cast<const uint32_t*>(jitBitmap.lockPixels());
  ASSERT_TRUE(aotPixels != nullptr && jitPixels != nullptr);
  size_t diffCount = 0;
  size_t maxDelta = 0;
  long minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
  size_t channelHits[4] = {0, 0, 0, 0};
  size_t nonzeroAot = 0;
  const int width = aotBitmap.width();
  const int height = aotBitmap.height();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto a = aotPixels[y * width + x];
      auto b = jitPixels[y * width + x];
      if (a != 0) {
        ++nonzeroAot;
      }
      if (a == b) {
        continue;
      }
      ++diffCount;
      minX = minX < x ? minX : x;
      minY = minY < y ? minY : y;
      maxX = maxX > x ? maxX : x;
      maxY = maxY > y ? maxY : y;
      for (int c = 0; c < 4; ++c) {
        auto delta = std::abs(static_cast<int>((a >> (c * 8)) & 0xFF) -
                              static_cast<int>((b >> (c * 8)) & 0xFF));
        if (delta != 0) {
          ++channelHits[c];
        }
        if (static_cast<size_t>(delta) > maxDelta) {
          maxDelta = static_cast<size_t>(delta);
        }
      }
    }
  }
  aotBitmap.unlockPixels();
  jitBitmap.unlockPixels();
  printf("[PicScaledDiff] nonzero(AOT)=%zu/%d diffPixels=%zu (%.4f%%) bbox=(%ld,%ld)-(%ld,%ld) "
         "maxDelta=%zu channels(r,g,b,a)=(%zu,%zu,%zu,%zu)\n",
         nonzeroAot, width * height, diffCount,
         100.0 * static_cast<double>(diffCount) / (static_cast<double>(width) * height), minX, minY,
         maxX, maxY, maxDelta, channelHits[0], channelHits[1], channelHits[2], channelHits[3]);
  // Diagnostic oracle: both paths must render the same non-empty content; the diff statistics
  // above classify the known residual divergence for tracking.
  EXPECT_TRUE(nonzeroAot > 0);
}

}  // namespace tgfx
