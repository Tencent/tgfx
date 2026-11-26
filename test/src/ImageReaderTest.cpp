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

#include <deque>
#include "gpu/resources/TextureView.h"
#include "platform/ImageStream.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"

namespace tgfx {
class QueueImageStream : public ImageStream {
 public:
  QueueImageStream() : ImageStream(1, 1) {
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

  void queueFrame(uint8_t red, int64_t timestamp) {
    frames.push_back({red, timestamp});
  }

  bool failNextRead = false;
  int readCount = 0;

 protected:
  ReadResult onReadTexture(Context* context, bool, std::shared_ptr<TextureView> textureView,
                           int64_t timestamp) override {
    readCount++;
    if (failNextRead) {
      failNextRead = false;
      return {std::move(textureView), ImageBufferStatus::Failed, true};
    }
    bool latched = false;
    while (!frames.empty()) {
      auto frame = frames.front();
      frames.pop_front();
      latched = true;
      uint8_t pixels[] = {frame.red, 0, 0, 255};
      textureView =
          TextureView::MakeFormat(context, 1, 1, pixels, sizeof(pixels), PixelFormat::RGBA_8888);
      if (timestamp < 0 || frame.timestamp == timestamp) {
        return {std::move(textureView), ImageBufferStatus::Ready, latched};
      }
    }
    return {std::move(textureView), ImageBufferStatus::Timeout, latched};
  }

 private:
  struct Frame {
    uint8_t red = 0;
    int64_t timestamp = -1;
  };
  std::deque<Frame> frames = {};
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

static uint8_t DrawAndReadRed(Context* context, std::shared_ptr<Image> image) {
  auto surface = Surface::Make(context, 1, 1);
  if (surface == nullptr) {
    return 0;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawImage(std::move(image));
  uint8_t pixels[4] = {};
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, pixels)) {
    return 0;
  }
  return pixels[0];
}

TGFX_TEST_PRIVATE(ImageReaderTest, SkipsOlderFramesToTheRequestedTimestamp) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<QueueImageStream>();
                      auto reader = ImageReader::MakeFrom(stream); stream->queueFrame(10, 1000);
                      stream->queueFrame(20, 2000); stream->queueFrame(30, 3000);
                      auto buffer = reader->acquireNextBuffer(2000);
                      EXPECT_EQ(buffer->status(), ImageBufferStatus::Pending);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 20);
                      EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);
                      auto next = reader->acquireNextBuffer(3000);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(next)), 30);
                      EXPECT_EQ(next->status(), ImageBufferStatus::Ready);
                      EXPECT_EQ(buffer->status(), ImageBufferStatus::Expired);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, TimeoutKeepsPreviousFrameAndRetriesWithNewImage) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->queueFrame(10, 1000);
      auto first = reader->acquireNextBuffer(1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(first)), 10);
      auto second = reader->acquireNextBuffer(2000); auto secondImage = Image::MakeFrom(second);
      EXPECT_EQ(DrawAndReadRed(context, secondImage), 10);
      EXPECT_EQ(second->status(), ImageBufferStatus::Timeout);
      EXPECT_EQ(first->status(), ImageBufferStatus::Ready); stream->queueFrame(20, 2000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 20);
      EXPECT_EQ(second->status(), ImageBufferStatus::Ready);
      EXPECT_EQ(first->status(), ImageBufferStatus::Expired);
      EXPECT_EQ(DrawAndReadRed(context, secondImage), 20);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, TimeoutAfterLatchingOlderFramesExpiresPreviousBuffer) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<QueueImageStream>();
                      auto reader = ImageReader::MakeFrom(stream); stream->queueFrame(10, 1000);
                      auto first = reader->acquireNextBuffer(1000);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(first)), 10);
                      stream->queueFrame(15, 1500); auto second = reader->acquireNextBuffer(2000);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 15);
                      EXPECT_EQ(second->status(), ImageBufferStatus::Timeout);
                      EXPECT_EQ(first->status(), ImageBufferStatus::Expired);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, FailedReadKeepsPreviousFrame) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->queueFrame(10, 1000);
      auto first = reader->acquireNextBuffer(1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(first)), 10); stream->queueFrame(20, 2000);
      stream->failNextRead = true; auto second = reader->acquireNextBuffer(2000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 10);
      EXPECT_EQ(second->status(), ImageBufferStatus::Failed);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 20);
      EXPECT_EQ(second->status(), ImageBufferStatus::Ready);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, FirstReadWithoutFrameDrawsNothing) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); auto buffer = reader->acquireNextBuffer(1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 0);
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Timeout);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, UnknownTimestampReadsNextFrame) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<QueueImageStream>();
                      auto reader = ImageReader::MakeFrom(stream); stream->queueFrame(10, 1000);
                      stream->queueFrame(20, 2000); auto buffer = reader->acquireNextBuffer();
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 10);
                      EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 10);
                      EXPECT_EQ(stream->readCount, 1);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, SkippedBufferExpiresWithoutReading) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<QueueImageStream>();
                      auto reader = ImageReader::MakeFrom(stream); stream->queueFrame(10, 1000);
                      stream->queueFrame(20, 2000); auto skipped = reader->acquireNextBuffer(1000);
                      auto latest = reader->acquireNextBuffer(2000);
                      EXPECT_EQ(skipped->status(), ImageBufferStatus::Expired);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(skipped)), 0);
                      EXPECT_EQ(stream->readCount, 0);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(latest)), 20);
                      EXPECT_EQ(latest->status(), ImageBufferStatus::Ready);)
}

TGFX_TEST(ImageReaderTest, FixedBufferIsAlwaysReady) {
  Bitmap bitmap = {};
  ASSERT_TRUE(bitmap.allocPixels(1, 1));
  auto buffer = bitmap.makeBuffer();
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);
}
}  // namespace tgfx
