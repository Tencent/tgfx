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
#ifdef TGFX_USE_THREADS
#include <chrono>
#include <condition_variable>
#include <thread>
#endif
#include "gpu/CommandQueue.h"
#include "gpu/GPU.h"
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

  void queueFrame(uint8_t red, int64_t timestamp) {
    frames.push_back({red, timestamp});
  }

  bool failNextRead = false;
  bool unknownMutationNextRead = false;
  bool readyWithoutTextureNextRead = false;
  bool reuseTexture = false;
  int readCount = 0;

 protected:
  ReadResult onReadTexture(Context* context, bool, std::shared_ptr<TextureView> textureView,
                           int64_t timestamp) override {
    readCount++;
    if (failNextRead) {
      failNextRead = false;
      return {std::move(textureView), ImageBufferStatus::Failed, false};
    }
    if (readyWithoutTextureNextRead) {
      readyWithoutTextureNextRead = false;
      return {nullptr, ImageBufferStatus::Ready, false};
    }
    if (unknownMutationNextRead) {
      unknownMutationNextRead = false;
      textureView = writeFrame(context, std::move(textureView), 99);
      return {std::move(textureView), ImageBufferStatus::Failed, true};
    }
    bool latched = false;
    while (!frames.empty()) {
      auto frame = frames.front();
      frames.pop_front();
      latched = true;
      textureView = writeFrame(context, std::move(textureView), frame.red);
      if (timestamp < 0 || frame.timestamp == timestamp) {
        return {std::move(textureView), ImageBufferStatus::Ready, latched};
      }
    }
    return {std::move(textureView), ImageBufferStatus::Timeout, latched};
  }

 private:
  std::shared_ptr<TextureView> writeFrame(Context* context,
                                          std::shared_ptr<TextureView> textureView, uint8_t red) {
    uint8_t pixels[] = {red, 0, 0, 255};
    if (reuseTexture && textureView != nullptr) {
      context->gpu()->queue()->writeTexture(textureView->getTexture(), Rect::MakeWH(1, 1), pixels,
                                            sizeof(pixels));
      return textureView;
    }
    return TextureView::MakeFormat(context, 1, 1, pixels, sizeof(pixels), PixelFormat::RGBA_8888);
  }

  struct Frame {
    uint8_t red = 0;
    int64_t timestamp = -1;
  };
  std::deque<Frame> frames = {};
};

static uint8_t DrawAndReadRed(Context* context, std::shared_ptr<Image> image) {
  auto surface = Surface::Make(context, 1, 1);
  if (surface == nullptr) {
    ADD_FAILURE() << "Failed to create the readback surface.";
    return 0;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawImage(std::move(image));
  uint8_t pixels[4] = {};
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, pixels)) {
    ADD_FAILURE() << "Failed to read the rendered pixels.";
    return 0;
  }
  return pixels[0];
}

#ifdef TGFX_TEST_ACCESS_PRIVATE
static void ExpectFramesForTimestamps(Context* context, const int64_t* timestamps, size_t count) {
  auto stream = std::make_shared<QueueImageStream>();
  auto reader = ImageReader::MakeFrom(stream);
  ASSERT_NE(reader, nullptr);
  std::shared_ptr<ImageBuffer> previous = nullptr;
  for (size_t index = 0; index < count; index++) {
    uint8_t red = static_cast<uint8_t>(20 + index * 20);
    stream->queueFrame(red, timestamps[index]);
    auto buffer = reader->acquireNextBuffer(timestamps[index]);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->status(), ImageBufferStatus::Pending);
    EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), red);
    EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);
    if (previous != nullptr) {
      EXPECT_EQ(previous->status(), ImageBufferStatus::Expired);
    }
    previous = std::move(buffer);
  }
  EXPECT_EQ(stream->readCount, static_cast<int>(count));
}
#endif

class ExpiringImageBuffer : public ImageBuffer {
 public:
  int width() const override {
    return 1;
  }

  int height() const override {
    return 1;
  }

  bool isAlphaOnly() const override {
    return false;
  }

  bool expired() const override {
    return isExpired;
  }

  void expire() {
    isExpired = true;
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context*, bool) const override {
    return nullptr;
  }

 private:
  bool isExpired = false;
};

#ifdef TGFX_USE_THREADS
class SynchronizedReadStream : public QueueImageStream {
 public:
  void queryDuringRead(std::shared_ptr<ImageBuffer> buffer) {
    std::unique_lock<std::mutex> lock(control);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!reading) {
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout && !reading) {
        ADD_FAILURE() << "The rendering thread never entered the source read.";
        return;
      }
    }
    queryStarted = true;
    condition.notify_all();
    lock.unlock();
    EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);
    lock.lock();
    queryFinished = true;
    condition.notify_all();
  }

 protected:
  ReadResult onReadTexture(Context* context, bool mipmapped, std::shared_ptr<TextureView> texture,
                           int64_t timestamp) override {
    std::unique_lock<std::mutex> lock(control);
    reading = true;
    condition.notify_all();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!queryStarted) {
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout && !queryStarted) {
        ADD_FAILURE() << "The query thread did not start its concurrent status query.";
        return {};
      }
    }
    // status() must wait for this read to finish; without the reader lock it would return Pending.
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (!queryFinished) {
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout) {
        break;
      }
    }
    EXPECT_FALSE(queryFinished);
    lock.unlock();
    return QueueImageStream::onReadTexture(context, mipmapped, std::move(texture), timestamp);
  }

 private:
  std::mutex control = {};
  std::condition_variable condition = {};
  bool reading = false;
  bool queryStarted = false;
  bool queryFinished = false;
};
#endif

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

TGFX_TEST_PRIVATE(ImageReaderTest, UnknownFailedMutationDropsPreviousPixelsAndCache) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>(); stream->reuseTexture = true;
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      stream->queueFrame(10, 1000); auto first = reader->acquireNextBuffer(1000);
      auto firstImage = Image::MakeFrom(first); EXPECT_EQ(DrawAndReadRed(context, firstImage), 10);
      auto oldTexture = reader->textureView; ASSERT_NE(oldTexture, nullptr);
      auto oldKey = oldTexture->uniqueKey; ASSERT_FALSE(oldKey.empty());
      stream->unknownMutationNextRead = true; auto second = reader->acquireNextBuffer(2000);
      EXPECT_EQ(TextureView::MakeFrom(context, second), nullptr);
      EXPECT_EQ(second->status(), ImageBufferStatus::Failed);
      EXPECT_EQ(first->status(), ImageBufferStatus::Expired); EXPECT_TRUE(first->expired());
      EXPECT_EQ(Resource::Find<TextureView>(context, oldKey), nullptr);
      EXPECT_EQ(DrawAndReadRed(context, firstImage), 0); EXPECT_EQ(stream->readCount, 2);
      EXPECT_EQ(TextureView::MakeFrom(context, second), nullptr);
      EXPECT_EQ(second->status(), ImageBufferStatus::Timeout); stream->queueFrame(20, 2000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 20);
      EXPECT_EQ(second->status(), ImageBufferStatus::Ready);)
}

// A new Image takes ownership of the texture cache key; earlier Images can subsequently miss it.
TGFX_TEST_PRIVATE(ImageReaderTest, TimeoutCachedImageDoesNotRetryButNewImageDoes) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      stream->queueFrame(10, 1000); auto first = reader->acquireNextBuffer(1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(first)), 10);
      auto second = reader->acquireNextBuffer(2000); auto cachedImage = Image::MakeFrom(second);
      EXPECT_EQ(DrawAndReadRed(context, cachedImage), 10);
      EXPECT_EQ(second->status(), ImageBufferStatus::Timeout); EXPECT_EQ(stream->readCount, 2);
      EXPECT_EQ(DrawAndReadRed(context, cachedImage), 10); EXPECT_EQ(stream->readCount, 2);
      auto retryImage = Image::MakeFrom(second); EXPECT_EQ(DrawAndReadRed(context, retryImage), 10);
      EXPECT_EQ(stream->readCount, 3); EXPECT_EQ(second->status(), ImageBufferStatus::Timeout);
      EXPECT_EQ(first->status(), ImageBufferStatus::Ready); stream->queueFrame(20, 2000);
      EXPECT_EQ(DrawAndReadRed(context, retryImage), 10); EXPECT_EQ(stream->readCount, 3);
      EXPECT_EQ(second->status(), ImageBufferStatus::Timeout);
      EXPECT_EQ(DrawAndReadRed(context, cachedImage), 20); EXPECT_EQ(stream->readCount, 4);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 20);
      EXPECT_EQ(stream->readCount, 4); EXPECT_EQ(second->status(), ImageBufferStatus::Ready);
      EXPECT_EQ(first->status(), ImageBufferStatus::Expired);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, FailedCachedImageDoesNotRetryButNewImageDoes) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      stream->queueFrame(10, 1000); auto first = reader->acquireNextBuffer(1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(first)), 10); stream->queueFrame(20, 2000);
      stream->failNextRead = true; auto second = reader->acquireNextBuffer(2000);
      auto cachedImage = Image::MakeFrom(second);
      EXPECT_EQ(DrawAndReadRed(context, cachedImage), 10);
      EXPECT_EQ(second->status(), ImageBufferStatus::Failed);
      EXPECT_EQ(first->status(), ImageBufferStatus::Ready); EXPECT_EQ(stream->readCount, 2);
      EXPECT_EQ(DrawAndReadRed(context, cachedImage), 10); EXPECT_EQ(stream->readCount, 2);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 20);
      EXPECT_EQ(stream->readCount, 3); EXPECT_EQ(second->status(), ImageBufferStatus::Ready);
      EXPECT_EQ(first->status(), ImageBufferStatus::Expired);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, InPlaceMutationRemovesOldImageCacheKey) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>(); stream->reuseTexture = true;
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      stream->queueFrame(10, 1000); auto first = reader->acquireNextBuffer(1000);
      auto firstImage = Image::MakeFrom(first); EXPECT_EQ(DrawAndReadRed(context, firstImage), 10);
      auto oldTexture = reader->textureView; ASSERT_NE(oldTexture, nullptr);
      auto oldKey = oldTexture->uniqueKey; ASSERT_FALSE(oldKey.empty());
      EXPECT_EQ(Resource::Find<TextureView>(context, oldKey), oldTexture);
      stream->queueFrame(20, 2000); auto second = reader->acquireNextBuffer(2000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(second)), 20);
      EXPECT_EQ(reader->textureView, oldTexture);
      EXPECT_EQ(second->status(), ImageBufferStatus::Ready);
      EXPECT_EQ(first->status(), ImageBufferStatus::Expired);
      EXPECT_EQ(Resource::Find<TextureView>(context, oldKey), nullptr);
      EXPECT_EQ(DrawAndReadRed(context, firstImage), 0); EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, EqualTimestampNewBufferDeliversChangedPixels) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); const int64_t timestamps[] = {1000, 1000};
                      ExpectFramesForTimestamps(context, timestamps, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ZeroTimestampIsAValidTarget) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); const int64_t timestamps[] = {0, 1000};
                      ExpectFramesForTimestamps(context, timestamps, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, DecreasingTimestampTargetsAreDelivered) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); const int64_t timestamps[] = {2000, 1000, 0};
                      ExpectFramesForTimestamps(context, timestamps, 3);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, UnknownSourceTimestampDeliversDefaultTarget) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr);
                      auto stream = std::make_shared<QueueImageStream>();
                      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
                      stream->queueFrame(10, -1); auto buffer = reader->acquireNextBuffer();
                      EXPECT_EQ(buffer->status(), ImageBufferStatus::Pending);
                      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 10);
                      EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, InitialTimeoutRetriesTheSameBufferAndImage) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      auto buffer = reader->acquireNextBuffer(1000); auto image = Image::MakeFrom(buffer);
      EXPECT_EQ(DrawAndReadRed(context, image), 0);
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Timeout); EXPECT_EQ(stream->readCount, 1);
      stream->queueFrame(10, 1000); EXPECT_EQ(DrawAndReadRed(context, image), 10);
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready); EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, InitialFailedReadRetriesTheSameBuffer) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      auto buffer = reader->acquireNextBuffer(1000); stream->failNextRead = true;
      EXPECT_EQ(TextureView::MakeFrom(context, buffer), nullptr);
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Failed); stream->queueFrame(10, 1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 10);
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready); EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, AcquiringLatestExpiresOlderFailedBuffer) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      auto failed = reader->acquireNextBuffer(1000); stream->failNextRead = true;
      EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
      EXPECT_EQ(failed->status(), ImageBufferStatus::Failed);
      auto latest = reader->acquireNextBuffer(2000);
      EXPECT_EQ(latest->status(), ImageBufferStatus::Pending);
      EXPECT_EQ(failed->status(), ImageBufferStatus::Expired); EXPECT_TRUE(failed->expired());
      EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr); EXPECT_EQ(stream->readCount, 1);
      stream->queueFrame(20, 2000); EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(latest)), 20);
      EXPECT_EQ(latest->status(), ImageBufferStatus::Ready);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ReadyWithoutTextureDoesNotClaimDelivery) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<QueueImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      stream->readyWithoutTextureNextRead = true; auto first = reader->acquireNextBuffer(1000);
      EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
      EXPECT_EQ(first->status(), ImageBufferStatus::Failed); stream->queueFrame(10, 1000);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(first)), 10);
      EXPECT_EQ(first->status(), ImageBufferStatus::Ready);
      stream->readyWithoutTextureNextRead = true; auto second = reader->acquireNextBuffer(2000);
      EXPECT_EQ(TextureView::MakeFrom(context, second), nullptr);
      EXPECT_EQ(second->status(), ImageBufferStatus::Failed);
      EXPECT_EQ(first->status(), ImageBufferStatus::Expired);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, NullStreamCannotCreateReader) {
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(ImageReader::MakeFrom(nullptr), nullptr);)
}

TGFX_TEST(ImageReaderTest, DefaultStatusHonorsExpiredOverride) {
  ExpiringImageBuffer buffer;
  EXPECT_EQ(buffer.status(), ImageBufferStatus::Ready);
  buffer.expire();
  EXPECT_TRUE(buffer.expired());
  EXPECT_EQ(buffer.status(), ImageBufferStatus::Expired);
}

#ifdef TGFX_USE_THREADS
TGFX_TEST_PRIVATE(ImageReaderTest, StatusQueryWaitsForInFlightSourceRead) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<SynchronizedReadStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_NE(reader, nullptr);
      stream->queueFrame(20, 2000); auto buffer = reader->acquireNextBuffer(2000);
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Pending);
      std::thread thread(&SynchronizedReadStream::queryDuringRead, stream.get(), buffer);
      EXPECT_EQ(DrawAndReadRed(context, Image::MakeFrom(buffer)), 20); thread.join();
      EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready); EXPECT_FALSE(buffer->expired());
      EXPECT_EQ(stream->readCount, 1);)
}
#endif

TGFX_TEST(ImageReaderTest, FixedBufferIsAlwaysReady) {
  Bitmap bitmap = {};
  ASSERT_TRUE(bitmap.allocPixels(1, 1));
  auto buffer = bitmap.makeBuffer();
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_EQ(buffer->status(), ImageBufferStatus::Ready);
}
}  // namespace tgfx
