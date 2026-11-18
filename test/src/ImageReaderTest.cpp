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
// real video source. It counts onDrainPendingFrames() calls. The stream itself needs no private
// access; the tests do, because ImageReader::MakeFrom() is private.
class FakeImageStream : public ImageStream {
 public:
  FakeImageStream(int width, int height) : ImageStream(width, height) {
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

  int drainCount = 0;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override {
    return TextureView::MakeFormat(context, width(), height(), PixelFormat::RGBA_8888, mipmapped);
  }

  bool onUpdateTexture(std::shared_ptr<TextureView>) override {
    return true;
  }

  void onDrainPendingFrames() override {
    drainCount++;
  }

 private:
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

/**
 * onDrainPendingFrames() runs only after requestSyncToLatestFrame() and only once per request, so
 * the texture is caught up to the latest frame exactly when the caller asked for it.
 */
TGFX_TEST_PRIVATE(ImageReaderTest, DrainsPendingFramesOnlyAfterSyncRequest) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<FakeImageStream>(16, 16);
                      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);

                      // A normal draw without a sync request does not drain.
                      auto first = reader->acquireNextBuffer();
                      ASSERT_TRUE(first != nullptr);
                      EXPECT_TRUE(TextureView::MakeFrom(context, first, false) != nullptr);
                      EXPECT_EQ(stream->drainCount, 0);

                      // After requesting a sync, the next texture generation drains exactly once.
                      reader->requestSyncToLatestFrame(); auto second = reader->acquireNextBuffer();
                      ASSERT_TRUE(second != nullptr);
                      EXPECT_TRUE(TextureView::MakeFrom(context, second, false) != nullptr);
                      EXPECT_EQ(stream->drainCount, 1);

                      // The request is one-shot: a subsequent draw does not drain again.
                      auto third = reader->acquireNextBuffer();
                      ASSERT_TRUE(third != nullptr);
                      EXPECT_TRUE(TextureView::MakeFrom(context, third, false) != nullptr);
                      EXPECT_EQ(stream->drainCount, 1);)
}
}  // namespace tgfx
