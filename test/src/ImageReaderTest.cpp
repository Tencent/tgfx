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

#include "gpu/resources/TextureView.h"
#include "platform/ImageStream.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"

namespace tgfx {
// A controllable ImageStream used to drive the platform-independent ImageReader logic without a
// real video source. hasNewFrame() and the texture-update result can both be forced to exercise
// the frame-availability gate and the update-failure path. The stream itself needs no private
// access; the tests do, because ImageReader::MakeFrom() is private.
class FakeImageStream : public ImageStream {
 public:
  FakeImageStream(int width, int height) : ImageStream(width, height) {
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

  bool newFrame = true;
  bool updateShouldFail = false;
  int makeTextureCount = 0;
  int updateTextureCount = 0;

 protected:
  bool hasNewFrame() const override {
    return newFrame;
  }

  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override {
    makeTextureCount++;
    return TextureView::MakeFormat(context, width(), height(), PixelFormat::RGBA_8888, mipmapped);
  }

  bool onUpdateTexture(std::shared_ptr<TextureView>) override {
    updateTextureCount++;
    return !updateShouldFail;
  }

 private:
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

/**
 * acquireNextBuffer() must return nullptr while the stream reports no new frame, and the previously
 * acquired buffer must stay valid (not expire) so the caller keeps rendering the current frame
 * instead of a stale one.
 */
TGFX_TEST_PRIVATE(ImageReaderTest, AcquireReturnsNullWithoutNewFrame) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>(16, 16);
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);

      stream->newFrame = true; auto firstBuffer = reader->acquireNextBuffer();
      ASSERT_TRUE(firstBuffer != nullptr);
      auto firstTexture = TextureView::MakeFrom(context, firstBuffer, false);
      EXPECT_TRUE(firstTexture != nullptr);

      // No new frame has arrived: the gate returns nullptr and does not advance the buffer
      // version, so the previously acquired buffer stays the current content.
      stream->newFrame = false;
      EXPECT_TRUE(reader->acquireNextBuffer() == nullptr); EXPECT_EQ(reader->bufferVersion, 1u);)
}

/**
 * When onUpdateTexture() fails, readTexture() must return nullptr instead of the previously created
 * texture view, so a stale frame is never reused as new content.
 */
TGFX_TEST_PRIVATE(ImageReaderTest, ReadTextureFailsWithoutReusingStaleTexture) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>(16, 16);
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);

      // First frame: onMakeTexture() creates the shared texture view.
      auto firstBuffer = reader->acquireNextBuffer();
      ASSERT_TRUE(firstBuffer != nullptr);
      auto firstTexture = TextureView::MakeFrom(context, firstBuffer, false);
      EXPECT_TRUE(firstTexture != nullptr); EXPECT_EQ(stream->makeTextureCount, 1);
      EXPECT_EQ(stream->updateTextureCount, 0);

      // Second frame: onUpdateTexture() fails, so readTexture() returns nullptr rather than the
      // previously created texture view.
      stream->updateShouldFail = true;
      auto secondBuffer = reader->acquireNextBuffer(); ASSERT_TRUE(secondBuffer != nullptr);
      auto secondTexture = TextureView::MakeFrom(context, secondBuffer, false);
      EXPECT_TRUE(secondTexture == nullptr); EXPECT_EQ(stream->updateTextureCount, 1);

      // Third frame: onUpdateTexture() succeeds again, so a valid texture view is produced.
      stream->updateShouldFail = false;
      auto thirdBuffer = reader->acquireNextBuffer(); ASSERT_TRUE(thirdBuffer != nullptr);
      auto thirdTexture = TextureView::MakeFrom(context, thirdBuffer, false);
      EXPECT_TRUE(thirdTexture != nullptr); EXPECT_EQ(stream->updateTextureCount, 2);)
}
}  // namespace tgfx
