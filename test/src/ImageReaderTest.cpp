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

#include <atomic>
#include <deque>
#include <thread>
#include "core/images/RasterizedImage.h"
#include "gpu/resources/TextureView.h"
#include "platform/ImageStream.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/RenderFlags.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {
class ThreeStateStream : public ImageStream {
 public:
  ThreeStateStream() : ImageStream(1, 1) {
  }
  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return space;
  }
  void push(uint8_t identity, int64_t timestamp) {
    frames.push_back({identity, timestamp});
  }
  int reads = 0;
  bool invalidateWhenEmpty = false;
  bool replaceWhenEmpty = false;
  bool replaceTexture = true;

 protected:
  TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                  std::shared_ptr<TextureView> texture) override {
    reads++;
    if (frames.empty()) {
      if (replaceWhenEmpty) {
        uint8_t pixels[] = {99, 0, 0, 255};
        texture = TextureView::MakeFormat(context, 1, 1, pixels, sizeof(pixels),
                                          PixelFormat::RGBA_8888, mipmapped);
      }
      auto outcome = invalidateWhenEmpty ? ReadOutcome::Invalidated : ReadOutcome::Unchanged;
      return {std::move(texture), outcome, boundTimestamp};
    }
    auto frame = frames.front();
    frames.pop_front();
    uint8_t pixels[] = {frame.identity, 0, 0, 255};
    if (texture == nullptr || replaceTexture) {
      texture = TextureView::MakeFormat(context, 1, 1, pixels, sizeof(pixels),
                                        PixelFormat::RGBA_8888, mipmapped);
    } else {
      context->gpu()->queue()->writeTexture(texture->getTexture(), Rect::MakeWH(1, 1), pixels,
                                            sizeof(pixels));
    }
    boundTimestamp = frame.timestamp;
    return {std::move(texture), ReadOutcome::Delivered, boundTimestamp};
  }

 private:
  struct Frame {
    uint8_t identity;
    int64_t timestamp;
  };
  std::deque<Frame> frames;
  std::shared_ptr<ColorSpace> space = nullptr;
  int64_t boundTimestamp = -1;
};

static void ExpectIdentity(Context* context, const std::shared_ptr<TextureView>& texture,
                           uint8_t identity) {
  ASSERT_NE(texture, nullptr);
  auto surface = Surface::MakeFrom(context, texture->getBackendTexture(), texture->origin());
  ASSERT_NE(surface, nullptr);
  uint8_t pixels[4] = {};
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  ASSERT_TRUE(surface->readPixels(info, pixels));
  EXPECT_EQ(pixels[0], identity);
  EXPECT_EQ(pixels[3], 255);
}
static void ObserveDelivery(const std::shared_ptr<ImageBuffer>& buffer, std::atomic<bool>* started,
                            std::atomic<bool>* stop, std::atomic<bool>* invalid,
                            std::atomic<int>* readyObservations) {
  EXPECT_EQ(buffer->state(), ImageBufferState::Pending);
  EXPECT_EQ(buffer->timestamp(), -1);
  started->store(true);
  while (!stop->load()) {
    if (buffer->state() == ImageBufferState::Ready) {
      if (buffer->timestamp() != 3000 || buffer->expired()) {
        invalid->store(true);
      }
      readyObservations->fetch_add(1);
    }
    std::this_thread::yield();
  }
}

static void ObserveInvalidation(const std::shared_ptr<ImageBuffer>& buffer,
                                std::atomic<bool>* started, std::atomic<bool>* stop,
                                std::atomic<bool>* invalid, std::atomic<bool>* observedExpired) {
  EXPECT_EQ(buffer->state(), ImageBufferState::Ready);
  EXPECT_EQ(buffer->timestamp(), 3000);
  started->store(true);
  while (!stop->load()) {
    auto state = buffer->state();
    if (state != ImageBufferState::Ready && state != ImageBufferState::Expired) {
      invalid->store(true);
    }
    if (buffer->timestamp() != 3000) {
      invalid->store(true);
    }
    if (state == ImageBufferState::Expired) {
      if (!buffer->expired()) {
        invalid->store(true);
      }
      observedExpired->store(true);
    }
    std::this_thread::yield();
  }
  if (buffer->state() == ImageBufferState::Expired && buffer->timestamp() == 3000 &&
      buffer->expired()) {
    observedExpired->store(true);
  }
}

static void ExpectImagePixels(Context* context, const std::shared_ptr<Image>& image,
                              uint8_t identity, uint8_t alpha, uint32_t renderFlags) {
  ASSERT_NE(image, nullptr);
  auto surface = Surface::Make(context, 1, 1, false, 1, false, renderFlags);
  ASSERT_NE(surface, nullptr);
  surface->getCanvas()->drawImage(image);
  uint8_t pixels[4] = {};
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  ASSERT_TRUE(surface->readPixels(info, pixels));
  EXPECT_EQ(pixels[0], identity);
  EXPECT_EQ(pixels[1], 0);
  EXPECT_EQ(pixels[2], 0);
  EXPECT_EQ(pixels[3], alpha);
}
}  // namespace

TGFX_TEST_PRIVATE(ImageReaderTest, ReadOutcomeClassifiesEmptyAndDeliveredContent) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto empty = stream->onReadTexture(context, false, nullptr);
      EXPECT_EQ(empty.texture, nullptr);
      EXPECT_EQ(empty.outcome, ImageStream::ReadOutcome::Unchanged); EXPECT_EQ(empty.timestamp, -1);
      stream->push(2, 2000); auto result = stream->onReadTexture(context, false, nullptr);
      ExpectIdentity(context, result.texture, 2);
      EXPECT_EQ(result.outcome, ImageStream::ReadOutcome::Delivered);
      EXPECT_EQ(result.timestamp, 2000);
      auto unchanged = stream->onReadTexture(context, false, result.texture);
      EXPECT_EQ(unchanged.texture, result.texture);
      EXPECT_EQ(unchanged.outcome, ImageStream::ReadOutcome::Unchanged);
      stream->invalidateWhenEmpty = true;
      auto invalidated = stream->onReadTexture(context, false, result.texture);
      EXPECT_EQ(invalidated.texture, result.texture);
      EXPECT_EQ(invalidated.outcome, ImageStream::ReadOutcome::Invalidated);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ConcurrentQueriesObserveRenderingThreadDelivery) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(3, 3000);
      auto buffer = reader->acquireBuffer(); std::atomic<bool> started = false;
      std::atomic<bool> stop = false; std::atomic<bool> invalid = false;
      std::atomic<int> readyObservations = 0;
      std::thread observer(ObserveDelivery, buffer, &started, &stop, &invalid, &readyObservations);
      while (!started.load()) { std::this_thread::yield(); } auto texture =
          TextureView::MakeFrom(context, buffer);
      while (readyObservations.load() == 0 && texture != nullptr) {
        std::this_thread::yield();
      } stop.store(true);
      observer.join(); ExpectIdentity(context, texture, 3); EXPECT_FALSE(invalid.load());
      EXPECT_GT(readyObservations.load(), 0);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ConcurrentQueriesObserveInvalidationWithHistoricalTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(3, 3000);
      auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 3);
      ASSERT_FALSE(HasFatalFailure()); auto retained = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, retained), 3);
      ASSERT_FALSE(HasFatalFailure()); std::atomic<bool> started = false;
      std::atomic<bool> stop = false; std::atomic<bool> invalid = false;
      std::atomic<bool> observedExpired = false; std::thread observer(
          ObserveInvalidation, retained, &started, &stop, &invalid, &observedExpired);
      while (!started.load()) { std::this_thread::yield(); } stream->invalidateWhenEmpty = true;
      auto failed = reader->acquireBuffer(); auto texture = TextureView::MakeFrom(context, failed);
      stop.store(true); observer.join(); EXPECT_EQ(texture, nullptr); EXPECT_FALSE(invalid.load());
      EXPECT_TRUE(observedExpired.load()); EXPECT_EQ(first->state(), ImageBufferState::Expired);
      EXPECT_EQ(first->timestamp(), 3000); EXPECT_EQ(retained->state(), ImageBufferState::Expired);
      EXPECT_EQ(retained->timestamp(), 3000); EXPECT_EQ(failed->state(), ImageBufferState::Expired);
      EXPECT_EQ(failed->timestamp(), -1); EXPECT_EQ(stream->reads, 3);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, FailedCredentialCannotBeRevivedByReaderResult) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); auto failed = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
      EXPECT_EQ(failed->state(), ImageBufferState::Expired); auto attempts = stream->reads;
      stream->push(1, 1000); EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
      EXPECT_EQ(stream->reads, attempts); EXPECT_EQ(failed->timestamp(), -1);
      auto next = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, next), 1);
      EXPECT_EQ(next->timestamp(), 1000); EXPECT_EQ(failed->timestamp(), -1);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ConsecutiveEmptyReadsReusePreviouslyDeliveredContent) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 1000);
      auto first = reader->acquireBuffer(); auto texture = TextureView::MakeFrom(context, first);
      ExpectIdentity(context, texture, 1); for (int count = 0; count < 3; count++) {
        auto next = reader->acquireBuffer();
        EXPECT_EQ(next->state(), ImageBufferState::Pending);
        EXPECT_EQ(next->timestamp(), -1);
        EXPECT_EQ(TextureView::MakeFrom(context, next), texture);
        EXPECT_EQ(next->state(), ImageBufferState::Ready);
        EXPECT_EQ(next->timestamp(), 1000);
        EXPECT_EQ(first->state(), ImageBufferState::Ready);
        EXPECT_EQ(first->timestamp(), 1000);
        EXPECT_EQ(TextureView::MakeFrom(context, first), texture);
        EXPECT_EQ(TextureView::MakeFrom(context, next), texture);
        EXPECT_EQ(stream->reads, count + 2);
      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, SuccessfulReadDoesNotProbeFutureFailure) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
      stream->invalidateWhenEmpty = true; auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 1);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(first->timestamp(), 0);
      EXPECT_EQ(stream->reads, 1); auto failed = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
      EXPECT_EQ(failed->state(), ImageBufferState::Expired); EXPECT_EQ(failed->timestamp(), -1);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(first->timestamp(), 0); EXPECT_EQ(stream->reads, 2);
      stream->invalidateWhenEmpty = false; stream->push(2, 1000);
      auto recovered = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, recovered), 2);
      EXPECT_EQ(recovered->timestamp(), 1000); EXPECT_EQ(stream->reads, 3);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, UnknownMutationExpiresPreviouslyDeliveredBuffer) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
      auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 1);
      stream->invalidateWhenEmpty = true; auto next = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, next), nullptr); EXPECT_TRUE(first->expired());
      EXPECT_EQ(first->timestamp(), 0); EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, EmptyReadIsTerminalButNextAcquisitionCanSucceed) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); for (bool invalidateWhenEmpty
                                                        : {false, true}) {
                        auto stream = std::make_shared<ThreeStateStream>();
                        stream->invalidateWhenEmpty = invalidateWhenEmpty;
                        auto reader = ImageReader::MakeFrom(stream);
                        auto first = reader->acquireBuffer();
                        EXPECT_EQ(first->state(), ImageBufferState::Pending);
                        EXPECT_EQ(first->timestamp(), -1);
                        EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
                        EXPECT_EQ(first->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->timestamp(), -1);
                        stream->push(7, 0);
                        EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
                        EXPECT_EQ(stream->reads, 1);
                        auto second = reader->acquireBuffer();
                        ExpectIdentity(context, TextureView::MakeFrom(context, second), 7);
                        EXPECT_EQ(second->state(), ImageBufferState::Ready);
                        EXPECT_EQ(second->timestamp(), 0);
                        EXPECT_EQ(first->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->timestamp(), -1);
                        EXPECT_EQ(stream->reads, 2);
                      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, ExpiredAcquisitionsDoNotRequireUnavailableFrames) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); auto skipped = reader->acquireBuffer();
      reader->acquireBuffer(); auto latest = reader->acquireBuffer();
      EXPECT_TRUE(skipped->expired()); EXPECT_EQ(skipped->state(), ImageBufferState::Expired);
      EXPECT_EQ(skipped->timestamp(), -1); stream->push(3, 2000);
      EXPECT_EQ(TextureView::MakeFrom(context, skipped), nullptr); EXPECT_EQ(stream->reads, 0);
      ExpectIdentity(context, TextureView::MakeFrom(context, latest), 3);
      EXPECT_EQ(latest->state(), ImageBufferState::Ready); EXPECT_EQ(stream->reads, 1);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, SingleReadLeavesNextQueuedFrameForCaller) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); reader->acquireBuffer(); reader->acquireBuffer();
      auto first = reader->acquireBuffer(); stream->push(1, 1000); stream->push(2, 2000);
      auto texture = TextureView::MakeFrom(context, first); ExpectIdentity(context, texture, 1);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(first->timestamp(), 1000);
      EXPECT_EQ(stream->reads, 1); EXPECT_EQ(TextureView::MakeFrom(context, first), texture);
      EXPECT_EQ(stream->reads, 1); auto next = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, next), 2);
      EXPECT_EQ(next->state(), ImageBufferState::Ready); EXPECT_EQ(next->timestamp(), 2000);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(first->timestamp(), 1000);
      EXPECT_EQ(stream->reads, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, SkippedCredentialsDoNotDrainQueuedFrames) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream);
      for (int i = 0; i < 12; i++) { reader->acquireBuffer(); } for (int i = 0; i < 8; i++) {
        stream->push(static_cast<uint8_t>(i + 1), i * 1000);
      } for (int i = 0; i < 8; i++) {
        auto next = reader->acquireBuffer();
        ExpectIdentity(context, TextureView::MakeFrom(context, next), static_cast<uint8_t>(i + 1));
        EXPECT_EQ(next->state(), ImageBufferState::Ready);
        EXPECT_EQ(next->timestamp(), i * 1000);
        EXPECT_EQ(stream->reads, i + 1);
      } auto empty = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, empty), 8);
      EXPECT_EQ(empty->state(), ImageBufferState::Ready); EXPECT_EQ(empty->timestamp(), 7000);
      EXPECT_EQ(stream->reads, 9);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, RepositionedProducerCanDeliverZeroTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(5, 5000);
      auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 5); stream->push(1, 0);
      auto second = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, second), 1);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(second->state(), ImageBufferState::Ready);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, EqualTimestampNewContentIsAcceptedAsProgress) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); for (bool replaceTexture
                                                        : {false, true}) {
                        auto stream = std::make_shared<ThreeStateStream>();
                        stream->replaceTexture = replaceTexture;
                        auto reader = ImageReader::MakeFrom(stream);
                        stream->push(1, 0);
                        auto first = reader->acquireBuffer();
                        auto firstTexture = TextureView::MakeFrom(context, first);
                        ExpectIdentity(context, firstTexture, 1);
                        stream->push(2, 0);
                        auto second = reader->acquireBuffer();
                        auto secondTexture = TextureView::MakeFrom(context, second);
                        ExpectIdentity(context, secondTexture, 2);
                        EXPECT_EQ(firstTexture == secondTexture, !replaceTexture);
                        EXPECT_EQ(second->state(), ImageBufferState::Ready);
                        EXPECT_EQ(second->timestamp(), 0);
                        EXPECT_EQ(first->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->timestamp(), 0);
                        EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
                        EXPECT_EQ(stream->reads, 2);
                      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, DeliveredBufferRedrawDoesNotConsumeAnotherFrame) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
      auto first = reader->acquireBuffer(); auto texture = TextureView::MakeFrom(context, first);
      ExpectIdentity(context, texture, 1); stream->push(2, 1000);
      EXPECT_EQ(TextureView::MakeFrom(context, first), texture); EXPECT_EQ(stream->reads, 1);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ZeroArgumentAcquireNextBufferRemainsDeferred) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
      auto first = reader->acquireNextBuffer(); ASSERT_NE(first, nullptr);
      EXPECT_EQ(first->state(), ImageBufferState::Pending); EXPECT_EQ(first->timestamp(), -1);
      EXPECT_EQ(stream->reads, 0);
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 1);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(first->timestamp(), 0);
      stream->push(2, 1000); auto second = reader->acquireNextBuffer(); ASSERT_NE(second, nullptr);
      EXPECT_EQ(second->state(), ImageBufferState::Pending); EXPECT_EQ(second->timestamp(), -1);
      EXPECT_EQ(stream->reads, 1);
      ExpectIdentity(context, TextureView::MakeFrom(context, second), 2);
      EXPECT_EQ(second->state(), ImageBufferState::Ready); EXPECT_EQ(second->timestamp(), 1000);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(first->timestamp(), 0); EXPECT_EQ(stream->reads, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, LaterReadExpiresOldBufferWithoutChangingItsTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
      auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 1);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(first->timestamp(), 0);
      stream->push(2, 1000); auto second = reader->acquireBuffer();
      EXPECT_EQ(second->state(), ImageBufferState::Pending); EXPECT_EQ(second->timestamp(), -1);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_FALSE(first->expired());
      EXPECT_EQ(first->timestamp(), 0); auto texture = TextureView::MakeFrom(context, second);
      ExpectIdentity(context, texture, 2); EXPECT_EQ(second->state(), ImageBufferState::Ready);
      EXPECT_EQ(second->timestamp(), 1000); EXPECT_EQ(first->state(), ImageBufferState::Expired);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(first->timestamp(), 0);
      EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr); EXPECT_EQ(first->timestamp(), 0);
      EXPECT_EQ(TextureView::MakeFrom(context, second), texture); EXPECT_EQ(stream->reads, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, UnchangedReadPreservesOldAndNewImagesWithOrWithoutCache) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); for (bool disableCache
                                                        : {false, true}) {
                        auto renderFlags = disableCache ? RenderFlags::DisableCache : 0;
                        auto stream = std::make_shared<ThreeStateStream>();
                        auto reader = ImageReader::MakeFrom(stream);
                        stream->push(17, 1000);
                        auto first = reader->acquireBuffer();
                        auto firstImage = Image::MakeFrom(first);
                        ExpectImagePixels(context, firstImage, 17, 255, renderFlags);
                        ASSERT_FALSE(HasFatalFailure());
                        auto firstKey =
                            std::static_pointer_cast<RasterizedImage>(firstImage)->getTextureKey();
                        auto texture = TextureView::MakeFrom(context, first);
                        ASSERT_NE(texture, nullptr);
                        if (!disableCache) {
                          EXPECT_EQ(Resource::Find<TextureView>(context, firstKey), texture);
                        }
                        auto next = reader->acquireBuffer();
                        auto nextImage = Image::MakeFrom(next);
                        ExpectImagePixels(context, nextImage, 17, 255, renderFlags);
                        ASSERT_FALSE(HasFatalFailure());
                        auto nextKey =
                            std::static_pointer_cast<RasterizedImage>(nextImage)->getTextureKey();
                        EXPECT_EQ(next->state(), ImageBufferState::Ready);
                        EXPECT_EQ(next->timestamp(), 1000);
                        EXPECT_EQ(first->state(), ImageBufferState::Ready);
                        EXPECT_EQ(first->timestamp(), 1000);
                        EXPECT_EQ(TextureView::MakeFrom(context, next), texture);
                        EXPECT_EQ(stream->reads, 2);
                        if (!disableCache) {
                          EXPECT_EQ(Resource::Find<TextureView>(context, nextKey), texture);
                        }
                        stream->push(29, 2000);
                        for (int count = 0; count < 3; count++) {
                          ExpectImagePixels(context, firstImage, 17, 255, renderFlags);
                          ASSERT_FALSE(HasFatalFailure());
                          if (!disableCache) {
                            EXPECT_EQ(Resource::Find<TextureView>(context, firstKey), texture);
                          }
                          ExpectImagePixels(context, nextImage, 17, 255, renderFlags);
                          ASSERT_FALSE(HasFatalFailure());
                          if (!disableCache) {
                            EXPECT_EQ(Resource::Find<TextureView>(context, nextKey), texture);
                          }
                          EXPECT_EQ(stream->reads, 2);
                        }
                        auto delivered = reader->acquireBuffer();
                        ExpectIdentity(context, TextureView::MakeFrom(context, delivered), 29);
                        EXPECT_EQ(delivered->timestamp(), 2000);
                        EXPECT_EQ(stream->reads, 3);
                        EXPECT_EQ(first->state(), ImageBufferState::Expired);
                        EXPECT_EQ(next->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->timestamp(), 1000);
                        EXPECT_EQ(next->timestamp(), 1000);
                        ExpectImagePixels(context, firstImage, 0, 0, renderFlags);
                        ASSERT_FALSE(HasFatalFailure());
                        ExpectImagePixels(context, nextImage, 0, 0, renderFlags);
                        ASSERT_FALSE(HasFatalFailure());
                        EXPECT_EQ(stream->reads, 3);
                      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, ChangedReadInvalidatesCachedImage) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); for (bool replaceTexture
                                                        : {false, true}) {
                        auto stream = std::make_shared<ThreeStateStream>();
                        stream->replaceTexture = replaceTexture;
                        auto reader = ImageReader::MakeFrom(stream);
                        stream->push(17, 1000);
                        auto first = reader->acquireBuffer();
                        auto firstImage = Image::MakeFrom(first);
                        ExpectImagePixels(context, firstImage, 17, 255, 0);
                        ASSERT_FALSE(HasFatalFailure());
                        auto firstKey =
                            std::static_pointer_cast<RasterizedImage>(firstImage)->getTextureKey();
                        auto firstTexture = Resource::Find<TextureView>(context, firstKey);
                        ASSERT_NE(firstTexture, nullptr);
                        stream->push(29, 2000);
                        auto next = reader->acquireBuffer();
                        auto nextImage = Image::MakeFrom(next);
                        ExpectImagePixels(context, nextImage, 29, 255, 0);
                        ASSERT_FALSE(HasFatalFailure());
                        auto nextKey =
                            std::static_pointer_cast<RasterizedImage>(nextImage)->getTextureKey();
                        auto nextTexture = Resource::Find<TextureView>(context, nextKey);
                        ASSERT_NE(nextTexture, nullptr);
                        EXPECT_EQ(nextTexture == firstTexture, !replaceTexture);
                        EXPECT_EQ(Resource::Find<TextureView>(context, firstKey), nullptr);
                        EXPECT_EQ(first->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->timestamp(), 1000);
                        EXPECT_EQ(next->state(), ImageBufferState::Ready);
                        EXPECT_EQ(next->timestamp(), 2000);
                        ExpectImagePixels(context, firstImage, 0, 0, 0);
                        ASSERT_FALSE(HasFatalFailure());
                        ExpectImagePixels(context, nextImage, 29, 255, 0);
                        ASSERT_FALSE(HasFatalFailure());
                        EXPECT_EQ(stream->reads, 2);
                      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, InvalidatedReadCannotFallbackToRetainedImage) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      for (bool replaceWhenEmpty
           : {false, true}) {
        auto stream = std::make_shared<ThreeStateStream>();
        auto reader = ImageReader::MakeFrom(stream);
        stream->push(17, 1000);
        auto first = reader->acquireBuffer();
        auto firstImage = Image::MakeFrom(first);
        ExpectImagePixels(context, firstImage, 17, 255, 0);
        ASSERT_FALSE(HasFatalFailure());
        auto firstKey = std::static_pointer_cast<RasterizedImage>(firstImage)->getTextureKey();
        ASSERT_NE(Resource::Find<TextureView>(context, firstKey), nullptr);
        auto retained = reader->acquireBuffer();
        auto retainedImage = Image::MakeFrom(retained);
        ExpectImagePixels(context, retainedImage, 17, 255, 0);
        ASSERT_FALSE(HasFatalFailure());
        auto retainedKey =
            std::static_pointer_cast<RasterizedImage>(retainedImage)->getTextureKey();
        ASSERT_NE(Resource::Find<TextureView>(context, retainedKey), nullptr);
        EXPECT_EQ(retained->state(), ImageBufferState::Ready);
        EXPECT_EQ(retained->timestamp(), 1000);
        stream->invalidateWhenEmpty = true;
        stream->replaceWhenEmpty = replaceWhenEmpty;
        auto failed = reader->acquireBuffer();
        EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
        EXPECT_EQ(failed->state(), ImageBufferState::Expired);
        EXPECT_EQ(failed->timestamp(), -1);
        EXPECT_EQ(first->state(), ImageBufferState::Expired);
        EXPECT_EQ(first->timestamp(), 1000);
        EXPECT_EQ(retained->state(), ImageBufferState::Expired);
        EXPECT_EQ(retained->timestamp(), 1000);
        EXPECT_EQ(Resource::Find<TextureView>(context, firstKey), nullptr);
        EXPECT_EQ(Resource::Find<TextureView>(context, retainedKey), nullptr);
        ExpectImagePixels(context, firstImage, 0, 0, 0);
        ASSERT_FALSE(HasFatalFailure());
        ExpectImagePixels(context, retainedImage, 0, 0, 0);
        ASSERT_FALSE(HasFatalFailure());
        EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
        EXPECT_EQ(stream->reads, 3);
        stream->invalidateWhenEmpty = false;
        stream->replaceWhenEmpty = false;
        auto empty = reader->acquireBuffer();
        EXPECT_EQ(TextureView::MakeFrom(context, empty), nullptr);
        EXPECT_EQ(empty->state(), ImageBufferState::Expired);
        EXPECT_EQ(empty->timestamp(), -1);
        EXPECT_EQ(stream->reads, 4);
        stream->push(29, 2000);
        EXPECT_EQ(TextureView::MakeFrom(context, empty), nullptr);
        EXPECT_EQ(stream->reads, 4);
        auto recovered = reader->acquireBuffer();
        ExpectIdentity(context, TextureView::MakeFrom(context, recovered), 29);
        EXPECT_EQ(recovered->state(), ImageBufferState::Ready);
        EXPECT_EQ(recovered->timestamp(), 2000);
        EXPECT_EQ(stream->reads, 5);
      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, UnchangedReplacementDoesNotInheritPixelsOrTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(17, 1000);
      auto first = reader->acquireBuffer(); auto firstImage = Image::MakeFrom(first);
      ExpectImagePixels(context, firstImage, 17, 255, 0); ASSERT_FALSE(HasFatalFailure());
      auto firstKey = std::static_pointer_cast<RasterizedImage>(firstImage)->getTextureKey();
      ASSERT_NE(Resource::Find<TextureView>(context, firstKey), nullptr);
      stream->replaceWhenEmpty = true; auto replacement = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, replacement), nullptr);
      EXPECT_EQ(replacement->state(), ImageBufferState::Expired);
      EXPECT_EQ(replacement->timestamp(), -1); EXPECT_EQ(first->state(), ImageBufferState::Expired);
      EXPECT_EQ(first->timestamp(), 1000);
      EXPECT_EQ(Resource::Find<TextureView>(context, firstKey), nullptr);
      auto replacementImage = Image::MakeFrom(replacement);
      ExpectImagePixels(context, replacementImage, 0, 0, 0); ASSERT_FALSE(HasFatalFailure());
      ExpectImagePixels(context, firstImage, 0, 0, 0); ASSERT_FALSE(HasFatalFailure());
      EXPECT_EQ(stream->reads, 2); stream->replaceWhenEmpty = false;
      auto unchanged = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, unchanged), nullptr);
      EXPECT_EQ(unchanged->state(), ImageBufferState::Expired);
      EXPECT_EQ(unchanged->timestamp(), -1); EXPECT_EQ(stream->reads, 3); stream->push(29, 2000);
      auto delivered = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, delivered), 29);
      EXPECT_EQ(delivered->state(), ImageBufferState::Ready);
      EXPECT_EQ(delivered->timestamp(), 2000); EXPECT_EQ(stream->reads, 4);)
}

TGFX_TEST(ImageReaderTest, StablePixelBufferDefaultsToReadyWithoutTimestamp) {
  Bitmap bitmap;
  ASSERT_TRUE(bitmap.allocPixels(1, 1, false, false));
  uint8_t pixels[] = {9, 0, 0, 255};
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  ASSERT_TRUE(bitmap.writePixels(info, pixels));
  auto buffer = bitmap.makeBuffer();
  ASSERT_NE(buffer, nullptr);
  EXPECT_FALSE(buffer->isAlphaOnly());
  EXPECT_FALSE(buffer->expired());
  EXPECT_EQ(buffer->state(), ImageBufferState::Ready);
  EXPECT_EQ(buffer->timestamp(), -1);
  bitmap = Bitmap();
  EXPECT_FALSE(buffer->expired());
  EXPECT_EQ(buffer->state(), ImageBufferState::Ready);
  EXPECT_EQ(buffer->timestamp(), -1);
}
}  // namespace tgfx
