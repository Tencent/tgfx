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

#include "core/images/RasterizedImage.h"
#include "gpu/ResourceCache.h"
#include "gpu/TPArgs.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(ImageRenderTest, RasterizedScaledProxySize) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // GenMesh.png is 1563x1563, an odd size so that width * 0.5 is not an integer. The decoder rounds
  // the downscaled cache texture to round(1563 * 0.5) = 782. getRasterizedSize() must report that
  // same 782, and the wrapped proxy served from the resource cache on a later draw must keep the
  // real texture size instead of a recomputed one, otherwise a cached draw would sample a different
  // number of texels than the first draw.
  auto image = MakeImage("resources/assets/GenMesh.png");
  ASSERT_TRUE(image != nullptr);
  image = image->makeRasterized();
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 800, 800);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(Matrix::MakeScale(0.4f));
  canvas->drawImage(image);
  context->flushAndSubmit();
  auto raster = std::static_pointer_cast<RasterizedImage>(image);
  auto scaledKey = raster->getTextureKey(0.5f);
  EXPECT_TRUE(context->resourceCache()->hasUniqueResource(scaledKey));
  auto contentSize = raster->source->getRasterizedSize(0.5f);
  EXPECT_EQ(contentSize.width, 782);
  EXPECT_EQ(contentSize.height, 782);
  TPArgs tpArgs(context, 0, false, 0.5f, BackingFit::Exact);
  auto proxy = raster->lockTextureProxy(tpArgs);
  ASSERT_TRUE(proxy != nullptr);
  EXPECT_EQ(proxy->width(), 782);
  EXPECT_EQ(proxy->height(), 782);
}

namespace {
// An ImageBuffer whose GPU texture is taller than its logical content: the extra rows are red
// padding below the green content, mimicking a hardware-decoded buffer aligned by the decoder.
class PaddedImageBuffer : public ImageBuffer {
 public:
  PaddedImageBuffer(int width, int height) : _width(width), _height(height) {
  }

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return false;
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    auto bufferHeight = _height + PADDING;
    std::vector<uint32_t> pixels(static_cast<size_t>(_width) * static_cast<size_t>(bufferHeight));
    for (int y = 0; y < bufferHeight; y++) {
      auto color = y < _height ? 0xFF00FF00u : 0xFFFF0000u;
      for (int x = 0; x < _width; x++) {
        pixels[static_cast<size_t>(y) * static_cast<size_t>(_width) + static_cast<size_t>(x)] =
            color;
      }
    }
    return TextureView::MakeRGBA(context, _width, bufferHeight, pixels.data(),
                                 static_cast<size_t>(_width) * sizeof(uint32_t), mipmapped);
  }

 private:
  static constexpr int PADDING = 8;
  int _width = 0;
  int _height = 0;
};
}  // namespace

TGFX_TEST(ImageRenderTest, RasterizedPaddedBuffer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // The buffer reports 16x16 content but its texture is 16x24, the extra 8 rows being red padding.
  // RasterizedImage must keep the content size (16) after the proxy is released and rewrapped from
  // the cache at the full 24-row texture size, otherwise the draw stretches the content and samples
  // the red padding.
  constexpr int w = 16;
  constexpr int h = 16;
  auto buffer = std::make_shared<PaddedImageBuffer>(w, h);
  auto image = Image::MakeFrom(buffer);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, w, h);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  context->flushAndSubmit();
  canvas->clear();
  canvas->drawImage(image);
  context->flushAndSubmit();
  auto info = ImageInfo::Make(w, h, ColorType::RGBA_8888, AlphaType::Premultiplied);
  Buffer readback(info.byteSize());
  ASSERT_TRUE(surface->readPixels(info, readback.data()));
  auto pixels = reinterpret_cast<const uint32_t*>(readback.data());
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      ASSERT_EQ(pixels[y * w + x], 0xFF00FF00u) << "mismatch at " << x << "," << y;
    }
  }
}

}  // namespace tgfx
