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
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Surface.h"
#include "tgfx/platform/web/VideoElementReader.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

// Canvas pixels exercise the real upload helpers without video decoding or asynchronous seeks.
// WebGPU's video upload guard requires source dimensions even for a supported canvas source.
// clang-format off
EM_JS(bool, InstallCanvasUploadFixture, (), {
  const source = document.createElement('canvas');
  source.width = 2;
  source.height = 2;
  source.videoWidth = 2;
  source.videoHeight = 2;
  source.readyState = 2;
  source.HAVE_CURRENT_DATA = 2;
  const context = source.getContext('2d');
  if (!context) {
    return false;
  }
  context.fillStyle = '#ff0000';
  context.fillRect(0, 0, 2, 2);
  const helper = Module.tgfx;
  const fixture = {
    source: source,
    uploads: 0,
    uploadToTexture: helper.uploadToTexture,
    uploadVideoToWebGPUTexture: helper.uploadVideoToWebGPUTexture
  };
  if (typeof fixture.uploadToTexture === 'function') {
    helper.uploadToTexture = function() {
      if (arguments[1] === source) {
        fixture.uploads++;
      }
      return fixture.uploadToTexture.apply(this, arguments);
    };
  }
  if (typeof fixture.uploadVideoToWebGPUTexture === 'function') {
    helper.uploadVideoToWebGPUTexture = function() {
      if (arguments[0] === source) {
        fixture.uploads++;
      }
      return fixture.uploadVideoToWebGPUTexture.apply(this, arguments);
    };
  }
  Module.videoElementReadFixture = fixture;
  return true;
});

EM_JS(void, RemoveCanvasUploadFixture, (), {
  const fixture = Module.videoElementReadFixture;
  if (!fixture) {
    return;
  }
  if (typeof fixture.uploadToTexture === 'function') {
    Module.tgfx.uploadToTexture = fixture.uploadToTexture;
  }
  if (typeof fixture.uploadVideoToWebGPUTexture === 'function') {
    Module.tgfx.uploadVideoToWebGPUTexture = fixture.uploadVideoToWebGPUTexture;
  }
  delete Module.videoElementReadFixture;
});

EM_JS(int, CanvasUploadCount, (), { return Module.videoElementReadFixture.uploads; });

EM_JS(void, PaintCanvasSource, (bool blue), {
  const context = Module.videoElementReadFixture.source.getContext('2d');
  context.fillStyle = blue ? '#0000ff' : '#ff0000';
  context.fillRect(0, 0, 2, 2);
});
// clang-format on

class CanvasUploadFixture {
 public:
  CanvasUploadFixture() : valid(InstallCanvasUploadFixture()) {
  }

  ~CanvasUploadFixture() {
    RemoveCanvasUploadFixture();
  }

  bool isValid() const {
    return valid;
  }

  emscripten::val source() const {
    return emscripten::val::module_property("videoElementReadFixture")["source"];
  }

 private:
  bool valid = false;
};

static void ExpectCanvasPixels(Context* context, const std::shared_ptr<TextureView>& texture,
                               bool blue) {
  ASSERT_NE(texture, nullptr);
  auto surface = Surface::MakeFrom(context, texture->getBackendTexture(), texture->origin());
  ASSERT_NE(surface, nullptr);
  uint8_t pixels[16] = {};
  auto info = ImageInfo::Make(2, 2, ColorType::RGBA_8888, AlphaType::Premultiplied);
  ASSERT_TRUE(surface->readPixels(info, pixels));
  for (int index = 0; index < 4; index++) {
    EXPECT_EQ(pixels[index * 4], blue ? 0 : 255);
    EXPECT_EQ(pixels[index * 4 + 1], 0);
    EXPECT_EQ(pixels[index * 4 + 2], blue ? 255 : 0);
    EXPECT_EQ(pixels[index * 4 + 3], 255);
  }
}

}  // namespace

TGFX_TEST(VideoElementReadTest, FirstAcquisitionUploadsOnce) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  CanvasUploadFixture fixture;
  ASSERT_TRUE(fixture.isValid());
  auto reader = VideoElementReader::MakeFrom(fixture.source(), 2, 2);
  ASSERT_NE(reader, nullptr);
  auto buffer = reader->acquireBuffer();
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(buffer->state(), ImageBufferState::Pending);
  EXPECT_EQ(CanvasUploadCount(), 0);
  auto texture = TextureView::MakeFrom(context, buffer);
  ExpectCanvasPixels(context, texture, false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  EXPECT_EQ(buffer->state(), ImageBufferState::Ready);
  EXPECT_EQ(buffer->timestamp(), -1);
}

TGFX_TEST(VideoElementReadTest, ReadyRedrawDoesNotUpload) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  CanvasUploadFixture fixture;
  ASSERT_TRUE(fixture.isValid());
  auto reader = VideoElementReader::MakeFrom(fixture.source(), 2, 2);
  ASSERT_NE(reader, nullptr);
  auto buffer = reader->acquireBuffer();
  ASSERT_NE(buffer, nullptr);
  auto texture = TextureView::MakeFrom(context, buffer);
  ExpectCanvasPixels(context, texture, false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  PaintCanvasSource(true);
  auto redrawTexture = TextureView::MakeFrom(context, buffer);
  EXPECT_EQ(redrawTexture, texture);
  ExpectCanvasPixels(context, redrawTexture, false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  EXPECT_EQ(buffer->state(), ImageBufferState::Ready);
}

TGFX_TEST(VideoElementReadTest, NewAcquisitionUploadsOnce) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  CanvasUploadFixture fixture;
  ASSERT_TRUE(fixture.isValid());
  auto reader = VideoElementReader::MakeFrom(fixture.source(), 2, 2);
  ASSERT_NE(reader, nullptr);
  auto first = reader->acquireBuffer();
  ASSERT_NE(first, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, first), false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  auto second = reader->acquireBuffer();
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(CanvasUploadCount(), 1);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, second), false);
  EXPECT_EQ(CanvasUploadCount(), 2);
  EXPECT_EQ(second->state(), ImageBufferState::Ready);
  EXPECT_TRUE(first->expired());
}

TGFX_TEST(VideoElementReadTest, ChangedPixelsWithoutCurrentTimeAreUploaded) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  CanvasUploadFixture fixture;
  ASSERT_TRUE(fixture.isValid());
  auto source = fixture.source();
  EXPECT_TRUE(source["currentTime"].isUndefined());
  auto reader = VideoElementReader::MakeFrom(source, 2, 2);
  ASSERT_NE(reader, nullptr);
  auto first = reader->acquireBuffer();
  ASSERT_NE(first, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, first), false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  PaintCanvasSource(true);
  EXPECT_TRUE(source == fixture.source());
  EXPECT_TRUE(source["currentTime"].isUndefined());
  auto second = reader->acquireBuffer();
  ASSERT_NE(second, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, second), true);
  EXPECT_EQ(CanvasUploadCount(), 2);
  EXPECT_EQ(second->state(), ImageBufferState::Ready);
  EXPECT_TRUE(first->expired());
}

TGFX_TEST(VideoElementReadTest, EqualAndDecreasingCurrentTimeDoNotSuppressUpload) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  CanvasUploadFixture fixture;
  ASSERT_TRUE(fixture.isValid());
  auto source = fixture.source();
  source.set("currentTime", 4);
  auto reader = VideoElementReader::MakeFrom(source, 2, 2);
  ASSERT_NE(reader, nullptr);
  auto first = reader->acquireBuffer();
  ASSERT_NE(first, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, first), false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  PaintCanvasSource(true);
  EXPECT_EQ(source["currentTime"].as<int>(), 4);
  auto equalTime = reader->acquireBuffer();
  ASSERT_NE(equalTime, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, equalTime), true);
  EXPECT_EQ(CanvasUploadCount(), 2);
  EXPECT_EQ(equalTime->state(), ImageBufferState::Ready);
  source.set("currentTime", 1);
  PaintCanvasSource(false);
  auto decreasingTime = reader->acquireBuffer();
  ASSERT_NE(decreasingTime, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, decreasingTime), false);
  EXPECT_EQ(CanvasUploadCount(), 3);
  EXPECT_EQ(decreasingTime->state(), ImageBufferState::Ready);
  EXPECT_EQ(decreasingTime->timestamp(), -1);
}

TGFX_TEST(VideoElementReadTest, NewReaderUploadsUnchangedSource) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  CanvasUploadFixture fixture;
  ASSERT_TRUE(fixture.isValid());
  auto source = fixture.source();
  auto reader = VideoElementReader::MakeFrom(source, 2, 2);
  ASSERT_NE(reader, nullptr);
  auto first = reader->acquireBuffer();
  ASSERT_NE(first, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, first), false);
  EXPECT_EQ(CanvasUploadCount(), 1);
  auto replacementReader = VideoElementReader::MakeFrom(source, 2, 2);
  ASSERT_NE(replacementReader, nullptr);
  auto replacementBuffer = replacementReader->acquireBuffer();
  ASSERT_NE(replacementBuffer, nullptr);
  ExpectCanvasPixels(context, TextureView::MakeFrom(context, replacementBuffer), false);
  EXPECT_EQ(CanvasUploadCount(), 2);
  EXPECT_EQ(replacementBuffer->state(), ImageBufferState::Ready);
  EXPECT_FALSE(first->expired());
}

}  // namespace tgfx
