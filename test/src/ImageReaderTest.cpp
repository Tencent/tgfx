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
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {
class UnimplementedImageStream : public ImageStream {
 public:
  UnimplementedImageStream() : ImageStream(1, 1) {
  }
  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return space;
  }

 private:
  std::shared_ptr<ColorSpace> space = nullptr;
};

class CreationOnlyImageStream : public ImageStream {
 public:
  CreationOnlyImageStream() : ImageStream(1, 1) {
  }
  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return space;
  }
  int creates = 0;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override {
    creates++;
    uint8_t pixels[] = {9, 0, 0, 255};
    return TextureView::MakeFormat(context, 1, 1, pixels, sizeof(pixels), PixelFormat::RGBA_8888,
                                   mipmapped);
  }

 private:
  std::shared_ptr<ColorSpace> space = nullptr;
};

class LegacyImageStream : public ImageStream {
 public:
  LegacyImageStream() : ImageStream(1, 1) {
  }
  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return space;
  }
  int creates = 0;
  int updates = 0;
  bool fails = false;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override {
    creates++;
    if (fails) {
      return nullptr;
    }
    uint8_t pixels[] = {7, 0, 0, 255};
    return TextureView::MakeFormat(context, 1, 1, pixels, sizeof(pixels), PixelFormat::RGBA_8888,
                                   mipmapped);
  }
  bool onUpdateTexture(std::shared_ptr<TextureView>) override {
    updates++;
    return !fails;
  }

 private:
  std::shared_ptr<ColorSpace> space = nullptr;
};

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
  bool failWhenEmpty = false;
  bool changeWhenEmpty = false;
  bool replaceTexture = true;

 protected:
  TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                  std::shared_ptr<TextureView> texture) override {
    reads++;
    if (frames.empty()) {
      return {std::move(texture), false, boundTimestamp, changeWhenEmpty, failWhenEmpty};
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
    return {std::move(texture), true, boundTimestamp, true, false};
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

static void ExpectImagePixels(Context* context, const std::shared_ptr<Image>& image,
                              uint8_t identity, uint8_t alpha) {
  ASSERT_NE(image, nullptr);
  auto surface = Surface::Make(context, 1, 1);
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

TGFX_TEST_PRIVATE(ImageReaderTest, MissingLegacyCreationFailsWithoutRecursiveDispatch) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<UnimplementedImageStream>();
      auto result = stream->onReadTexture(context, false, nullptr);
      EXPECT_EQ(result.texture, nullptr); EXPECT_FALSE(result.advanced); EXPECT_TRUE(result.failed);
      EXPECT_EQ(result.timestamp, -1); auto reader = ImageReader::MakeFrom(stream);
      auto first = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
      EXPECT_EQ(first->state(), ImageBufferState::Expired); EXPECT_EQ(first->timestamp(), -1);
      auto retry = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, retry), nullptr);
      EXPECT_EQ(retry->state(), ImageBufferState::Expired);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, MissingLegacyUpdateFailsWithoutRecursiveDispatch) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<CreationOnlyImageStream>();
      auto reader = ImageReader::MakeFrom(stream); auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 9);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(stream->creates, 1);
      auto next = reader->acquireBuffer(); EXPECT_EQ(TextureView::MakeFrom(context, next), nullptr);
      EXPECT_EQ(next->state(), ImageBufferState::Expired); EXPECT_EQ(next->timestamp(), -1);
      EXPECT_EQ(stream->creates, 1); EXPECT_TRUE(first->expired());)
}

TGFX_TEST_PRIVATE(ImageReaderTest, MetadataReadClassifiesEmptyQueueWithoutLegacyHooks) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto empty = stream->onReadTexture(context, false, nullptr);
      EXPECT_EQ(empty.texture, nullptr); EXPECT_FALSE(empty.advanced); EXPECT_FALSE(empty.failed);
      EXPECT_FALSE(empty.contentChanged); EXPECT_EQ(empty.timestamp, -1); stream->push(2, 2000);
      auto result = stream->onReadTexture(context, false, nullptr);
      ExpectIdentity(context, result.texture, 2); EXPECT_TRUE(result.advanced);
      EXPECT_FALSE(result.failed); EXPECT_EQ(result.timestamp, 2000);)
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

TGFX_TEST_PRIVATE(ImageReaderTest, LegacyAdapterPerformsOneOperationPerAcquisition) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<LegacyImageStream>();
      auto reader = ImageReader::MakeFrom(stream); auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 7);
      EXPECT_EQ(stream->creates, 1); EXPECT_EQ(stream->updates, 0);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(first->timestamp(), -1);
      auto second = reader->acquireBuffer(); auto texture = TextureView::MakeFrom(context, second);
      ExpectIdentity(context, texture, 7); EXPECT_EQ(stream->creates, 1);
      EXPECT_EQ(stream->updates, 1); EXPECT_EQ(TextureView::MakeFrom(context, second), texture);
      EXPECT_EQ(stream->updates, 1);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, LegacyAdapterFailureCanRecoverWithNewCredential) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<LegacyImageStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->fails = true;
      auto first = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
      EXPECT_EQ(first->state(), ImageBufferState::Expired); EXPECT_EQ(stream->creates, 1);
      stream->fails = false; auto next = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, next), 7);
      EXPECT_EQ(stream->creates, 2); stream->fails = true; auto failed = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr); EXPECT_EQ(stream->updates, 1);
      EXPECT_EQ(failed->state(), ImageBufferState::Expired); stream->fails = false;
      auto recovered = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, recovered), 7);
      EXPECT_EQ(stream->updates, 2); EXPECT_EQ(recovered->state(), ImageBufferState::Ready);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, LegacyAdapterInitializesNewReaderWithUnchangedPixels) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<LegacyImageStream>();
      auto reader = ImageReader::MakeFrom(stream); auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 7);
      auto replacementStream = std::make_shared<LegacyImageStream>();
      auto replacementReader = ImageReader::MakeFrom(replacementStream);
      auto next = replacementReader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, next), 7);
      EXPECT_EQ(replacementStream->creates, 1); EXPECT_EQ(replacementStream->updates, 0);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, EmptyReadPreservesPreviouslyDeliveredContent) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
      auto first = reader->acquireBuffer(); auto texture = TextureView::MakeFrom(context, first);
      ExpectIdentity(context, texture, 1); auto second = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, second), nullptr);
      EXPECT_EQ(second->state(), ImageBufferState::Expired); EXPECT_FALSE(first->expired());
      EXPECT_EQ(TextureView::MakeFrom(context, first), texture);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, SuccessfulReadDoesNotProbeFutureFailure) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0); stream->failWhenEmpty = true;
      stream->changeWhenEmpty = true; auto first = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, first), 1);
      EXPECT_EQ(first->state(), ImageBufferState::Ready); EXPECT_EQ(first->timestamp(), 0);
      EXPECT_EQ(stream->reads, 1); auto failed = reader->acquireBuffer();
      EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
      EXPECT_EQ(failed->state(), ImageBufferState::Expired); EXPECT_EQ(failed->timestamp(), -1);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(first->timestamp(), 0); EXPECT_EQ(stream->reads, 2);
      stream->failWhenEmpty = false; stream->changeWhenEmpty = false; stream->push(2, 1000);
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
      stream->failWhenEmpty = true; stream->changeWhenEmpty = true;
      auto next = reader->acquireBuffer(); EXPECT_EQ(TextureView::MakeFrom(context, next), nullptr);
      EXPECT_TRUE(first->expired()); EXPECT_EQ(first->timestamp(), 0);
      EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, EmptyReadIsTerminalButNextAcquisitionCanSucceed) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); auto first = reader->acquireBuffer();
      EXPECT_EQ(first->state(), ImageBufferState::Pending);
      EXPECT_EQ(TextureView::MakeFrom(context, first), nullptr);
      EXPECT_EQ(first->state(), ImageBufferState::Expired); stream->push(7, 0);
      auto second = reader->acquireBuffer();
      ExpectIdentity(context, TextureView::MakeFrom(context, second), 7);
      EXPECT_EQ(second->state(), ImageBufferState::Ready); EXPECT_EQ(stream->reads, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ExpiredAcquisitionsDoNotRequireUnavailableFrames) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_NE(context, nullptr);
      auto stream = std::make_shared<ThreeStateStream>();
      auto reader = ImageReader::MakeFrom(stream); auto skipped = reader->acquireBuffer();
      reader->acquireBuffer(); auto latest = reader->acquireBuffer();
      EXPECT_TRUE(skipped->expired()); stream->push(3, 2000);
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
      EXPECT_EQ(TextureView::MakeFrom(context, empty), nullptr);
      EXPECT_EQ(empty->state(), ImageBufferState::Expired); EXPECT_EQ(stream->reads, 9);)
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
                      ASSERT_NE(context, nullptr);
                      auto stream = std::make_shared<ThreeStateStream>();
                      auto reader = ImageReader::MakeFrom(stream); stream->push(1, 0);
                      auto first = reader->acquireBuffer();
                      ExpectIdentity(context, TextureView::MakeFrom(context, first), 1);
                      stream->push(2, 0); auto second = reader->acquireBuffer();
                      ExpectIdentity(context, TextureView::MakeFrom(context, second), 2);
                      EXPECT_EQ(second->state(), ImageBufferState::Ready);)
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

TGFX_TEST_PRIVATE(ImageReaderTest, UnchangedReadPreservesCachedImage) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); for (bool failed
                                                        : {false, true}) {
                        auto stream = std::make_shared<ThreeStateStream>();
                        auto reader = ImageReader::MakeFrom(stream);
                        stream->push(17, 1000);
                        auto first = reader->acquireBuffer();
                        auto image = Image::MakeFrom(first);
                        ExpectImagePixels(context, image, 17, 255);
                        ASSERT_FALSE(HasFatalFailure());
                        auto key =
                            std::static_pointer_cast<RasterizedImage>(image)->getTextureKey();
                        auto texture = Resource::Find<TextureView>(context, key);
                        ASSERT_NE(texture, nullptr);
                        stream->failWhenEmpty = failed;
                        auto next = reader->acquireBuffer();
                        EXPECT_EQ(TextureView::MakeFrom(context, next), nullptr);
                        EXPECT_EQ(next->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->state(), ImageBufferState::Ready);
                        EXPECT_EQ(first->timestamp(), 1000);
                        EXPECT_EQ(Resource::Find<TextureView>(context, key), texture);
                        ExpectImagePixels(context, image, 17, 255);
                        ASSERT_FALSE(HasFatalFailure());
                        EXPECT_EQ(stream->reads, 2);
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
                        ExpectImagePixels(context, firstImage, 17, 255);
                        ASSERT_FALSE(HasFatalFailure());
                        auto firstKey =
                            std::static_pointer_cast<RasterizedImage>(firstImage)->getTextureKey();
                        auto firstTexture = Resource::Find<TextureView>(context, firstKey);
                        ASSERT_NE(firstTexture, nullptr);
                        stream->push(29, 2000);
                        auto next = reader->acquireBuffer();
                        auto nextImage = Image::MakeFrom(next);
                        ExpectImagePixels(context, nextImage, 29, 255);
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
                        ExpectImagePixels(context, firstImage, 0, 0);
                        ASSERT_FALSE(HasFatalFailure());
                        ExpectImagePixels(context, nextImage, 29, 255);
                        ASSERT_FALSE(HasFatalFailure());
                        EXPECT_EQ(stream->reads, 2);
                      })
}

TGFX_TEST_PRIVATE(ImageReaderTest, UnknownMutationInvalidatesCachedImage) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_NE(context, nullptr); for (bool failed
                                                        : {false, true}) {
                        auto stream = std::make_shared<ThreeStateStream>();
                        auto reader = ImageReader::MakeFrom(stream);
                        stream->push(17, 1000);
                        auto first = reader->acquireBuffer();
                        auto image = Image::MakeFrom(first);
                        ExpectImagePixels(context, image, 17, 255);
                        ASSERT_FALSE(HasFatalFailure());
                        auto key =
                            std::static_pointer_cast<RasterizedImage>(image)->getTextureKey();
                        auto texture = Resource::Find<TextureView>(context, key);
                        ASSERT_NE(texture, nullptr);
                        stream->failWhenEmpty = failed;
                        stream->changeWhenEmpty = true;
                        auto next = reader->acquireBuffer();
                        EXPECT_EQ(TextureView::MakeFrom(context, next), nullptr);
                        EXPECT_EQ(next->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->state(), ImageBufferState::Expired);
                        EXPECT_EQ(first->timestamp(), 1000);
                        EXPECT_EQ(Resource::Find<TextureView>(context, key), nullptr);
                        ExpectImagePixels(context, image, 0, 0);
                        ASSERT_FALSE(HasFatalFailure());
                        EXPECT_EQ(stream->reads, 2);
                      })
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
