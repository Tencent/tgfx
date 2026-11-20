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

#include <cstdint>
#include <utility>
#include "gpu/resources/TextureView.h"
#include "platform/ImageStream.h"
#include "platform/android/SurfaceTextureFrameState.h"
#include "tgfx/core/Surface.h"
#include "tgfx/platform/ImageReader.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

/** A controllable stream whose opaque red channel encodes the frame ID independently of its PTS. */
class FakeImageStream : public ImageStream {
 public:
  FakeImageStream() : ImageStream(1, 1) {
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

  /** Selects the producer frame that the next texture read will upload. */
  void setFrame(uint8_t frameID, int64_t timestamp) {
    _frameID = frameID;
    _timestamp = timestamp;
  }

  bool providesFrameTimestamp = true;
  bool fails = false;
  int readCount = 0;
  int seekCount = 0;
  int64_t lastTarget = -1;
  int64_t lastDeadline = 0;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override {
    return onReadTexture(context, mipmapped, nullptr, -1, 0).texture;
  }

  bool onUpdateTexture(std::shared_ptr<TextureView> texture) override {
    return !fails && texture != nullptr;
  }

  TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                  std::shared_ptr<TextureView> texture, int64_t target,
                                  int64_t deadline) override {
    readCount++;
    lastTarget = target;
    lastDeadline = deadline;
    if (fails) {
      return {std::move(texture), ImageReaderStatus::Failed, _boundTimestamp};
    }
    uint8_t pixels[] = {_frameID, 0, 0, 255};
    texture = TextureView::MakeFormat(context, width(), height(), pixels, sizeof(pixels),
                                      PixelFormat::RGBA_8888, mipmapped);
    if (texture == nullptr) {
      return {};
    }
    _boundTimestamp = providesFrameTimestamp ? _timestamp : -1;
    auto status = ImageReaderStatus::Ready;
    if (target >= 0) {
      if (!providesFrameTimestamp) {
        status = ImageReaderStatus::Unsupported;
      } else if (_boundTimestamp < target) {
        status = ImageReaderStatus::NotReady;
      } else if (_boundTimestamp > target) {
        status = ImageReaderStatus::Mismatched;
      }
    }
    return {std::move(texture), status, _boundTimestamp};
  }

  void onSeeked() override {
    seekCount++;
    _timestamp = -1;
    _boundTimestamp = -1;
  }

 private:
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  uint8_t _frameID = 0;
  int64_t _timestamp = -1;
  int64_t _boundTimestamp = -1;
};

class BasicImageStream : public ImageStream {
 public:
  BasicImageStream() : ImageStream(1, 1) {
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

  uint8_t frameID = 1;
  bool fails = false;
  int updateCount = 0;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override {
    uint8_t pixels[] = {frameID, 0, 0, 255};
    return TextureView::MakeFormat(context, width(), height(), pixels, sizeof(pixels),
                                   PixelFormat::RGBA_8888, mipmapped);
  }

  bool onUpdateTexture(std::shared_ptr<TextureView>) override {
    updateCount++;
    return !fails;
  }

 private:
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

static void ExpectFrameID(Context* context, const std::shared_ptr<TextureView>& texture,
                          uint8_t frameID) {
  ASSERT_TRUE(texture != nullptr);
  auto surface = Surface::MakeFrom(context, texture->getBackendTexture(), texture->origin());
  ASSERT_TRUE(surface != nullptr);
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint8_t pixels[] = {0, 0, 0, 0};
  ASSERT_TRUE(surface->readPixels(info, pixels));
  EXPECT_EQ(pixels[0], frameID);
  EXPECT_EQ(pixels[1], 0);
  EXPECT_EQ(pixels[2], 0);
  EXPECT_EQ(pixels[3], 255);
}

}  // namespace

TGFX_TEST(SurfaceTextureFrameStateTest, SeekKeepsRecoveryWhileNotReady) {
  SurfaceTextureFrameState state;
  state.onLatched(5000);
  state.onSeeked();
  EXPECT_FALSE(state.hasLatchedFrame());
  EXPECT_EQ(state.readStatus(0, true), ImageReaderStatus::NotReady);
  state.onLatched(5000);
  EXPECT_EQ(state.readStatus(0), ImageReaderStatus::Pending);
  state.onLatched(0);
  EXPECT_EQ(state.readStatus(0), ImageReaderStatus::Ready);
  EXPECT_EQ(state.lastLatchedTimestamp(), 0);
  EXPECT_TRUE(state.lastLatchAdvanced());
}

TGFX_TEST(SurfaceTextureFrameStateTest, MismatchedSeekRetryDoesNotConsumeFrames) {
  SurfaceTextureFrameState state;
  state.onSeeked();
  int latchCount = 0;
  while (state.readStatus(10) == ImageReaderStatus::Pending && latchCount < 8) {
    state.onLatched(20 + latchCount);
    latchCount++;
  }
  EXPECT_EQ(latchCount, 8);
  EXPECT_EQ(state.readStatus(10, true), ImageReaderStatus::Mismatched);
  for (int retry = 0; retry < 3; retry++) {
    EXPECT_EQ(state.readStatus(10), ImageReaderStatus::Mismatched);
  }
  state.onSeeked();
  state.onLatched(10);
  EXPECT_EQ(state.readStatus(10), ImageReaderStatus::Ready);
}

TGFX_TEST(SurfaceTextureFrameStateTest, RestartedTimelineStopsAtOvershotFrame) {
  SurfaceTextureFrameState state;
  state.onLatched(5000);
  state.onSeeked();
  state.onLatched(5000);
  EXPECT_EQ(state.readStatus(10), ImageReaderStatus::Pending);
  state.onLatched(20);
  EXPECT_EQ(state.readStatus(10), ImageReaderStatus::Mismatched);
  EXPECT_EQ(state.lastLatchedTimestamp(), 20);
}

TGFX_TEST(SurfaceTextureFrameStateTest, ForwardSeekAndZeroTimestampRemainValid) {
  SurfaceTextureFrameState state;
  state.onSeeked();
  EXPECT_EQ(state.readStatus(2000, true), ImageReaderStatus::NotReady);
  state.onLatched(1000);
  EXPECT_EQ(state.readStatus(2000, true), ImageReaderStatus::NotReady);
  state.onLatched(2000);
  EXPECT_EQ(state.readStatus(2000), ImageReaderStatus::Ready);
  state.onSeeked();
  state.onLatched(0);
  EXPECT_EQ(state.readStatus(0), ImageReaderStatus::Ready);
  state.onLatched(0);
  EXPECT_FALSE(state.lastLatchAdvanced());
  EXPECT_EQ(state.readStatus(-1, true), ImageReaderStatus::Ready);
}

TGFX_TEST_PRIVATE(ImageReaderTest, BasicStreamDoesNotNeedTargetFrameImplementation) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<BasicImageStream>();
                      auto reader = ImageReader::MakeFrom(stream);
                      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 0;
                      auto first = reader->acquireNextBuffer(frame);
                      ExpectFrameID(context, TextureView::MakeFrom(context, first), 1);
                      EXPECT_EQ(frame->status.load(), ImageReaderStatus::Unsupported);
                      auto second = reader->acquireNextBuffer();
                      EXPECT_TRUE(TextureView::MakeFrom(context, second) != nullptr);
                      EXPECT_EQ(stream->updateCount, 1); stream->fails = true;
                      auto retryFrame = std::make_shared<ImageReaderFrame>();
                      auto third = reader->acquireNextBuffer(retryFrame);
                      EXPECT_TRUE(TextureView::MakeFrom(context, third) == nullptr);
                      EXPECT_EQ(retryFrame->status.load(), ImageReaderStatus::Failed);
                      EXPECT_EQ(reader->textureVersion, 0u); stream->fails = false;
                      EXPECT_TRUE(TextureView::MakeFrom(context, third) != nullptr);
                      EXPECT_EQ(retryFrame->status.load(), ImageReaderStatus::Ready);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ReadsLatestFrameWithoutTarget) {
  TGFX_PRIVATE_ACCESS(ContextScope scope; auto context = scope.getContext();
                      ASSERT_TRUE(context != nullptr);
                      auto stream = std::make_shared<FakeImageStream>();
                      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);

                      stream->setFrame(1, 1000); auto first = reader->acquireNextBuffer();
                      ASSERT_TRUE(first != nullptr);
                      ExpectFrameID(context, TextureView::MakeFrom(context, first), 1);
                      EXPECT_EQ(stream->lastTarget, -1); EXPECT_EQ(stream->lastDeadline, 0);
                      EXPECT_EQ(stream->readCount, 1);

                      stream->setFrame(2, 2000); auto second = reader->acquireNextBuffer();
                      ASSERT_TRUE(second != nullptr);
                      ExpectFrameID(context, TextureView::MakeFrom(context, second), 2);
                      EXPECT_EQ(stream->lastTarget, -1); EXPECT_EQ(stream->lastDeadline, 0);
                      EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, RetriesOlderFrameWithoutCommittingVersion) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 2000;
      auto buffer = reader->acquireNextBuffer(frame); ASSERT_TRUE(buffer != nullptr);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Pending);

      stream->setFrame(1, 1000); EXPECT_TRUE(TextureView::MakeFrom(context, buffer) == nullptr);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::NotReady);
      EXPECT_EQ(frame->timestamp.load(), 1000); ExpectFrameID(context, reader->textureView, 1);
      EXPECT_EQ(reader->textureVersion, 0u); EXPECT_FALSE(buffer->expired());
      EXPECT_EQ(stream->lastTarget, 2000); EXPECT_EQ(stream->readCount, 1);

      stream->setFrame(2, 2000); auto texture = TextureView::MakeFrom(context, buffer);
      ExpectFrameID(context, texture, 2);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(frame->timestamp.load(), 2000);
      EXPECT_EQ(reader->textureVersion, reader->bufferVersion); EXPECT_EQ(stream->readCount, 2);
      EXPECT_EQ(TextureView::MakeFrom(context, buffer), texture); EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, KeepsTargetBoundToBuffer) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      auto firstFrame = std::make_shared<ImageReaderFrame>(); firstFrame->targetTimestamp = 1000;
      auto first = reader->acquireNextBuffer(firstFrame); ASSERT_TRUE(first != nullptr);
      stream->setFrame(1, 1000); auto firstTexture = TextureView::MakeFrom(context, first);
      ExpectFrameID(context, firstTexture, 1);
      EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      auto firstVersion = reader->textureVersion;

      auto secondFrame = std::make_shared<ImageReaderFrame>(); secondFrame->targetTimestamp = 2000;
      auto second = reader->acquireNextBuffer(secondFrame); ASSERT_TRUE(second != nullptr);
      EXPECT_FALSE(first->expired());
      EXPECT_EQ(TextureView::MakeFrom(context, first), firstTexture);
      ExpectFrameID(context, firstTexture, 1); EXPECT_EQ(reader->textureVersion, firstVersion);
      EXPECT_EQ(firstFrame->targetTimestamp, 1000); EXPECT_EQ(firstFrame->timestamp.load(), 1000);
      EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(secondFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Pending);
      EXPECT_EQ(stream->lastTarget, 1000); EXPECT_EQ(stream->readCount, 1);

      stream->setFrame(2, 2000); ExpectFrameID(context, TextureView::MakeFrom(context, second), 2);
      EXPECT_EQ(stream->lastTarget, 2000); EXPECT_EQ(stream->readCount, 2);
      EXPECT_EQ(secondFrame->timestamp.load(), 2000);
      EXPECT_EQ(secondFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(firstFrame->timestamp.load(), 1000);
      EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_TRUE(first->expired());)
}

TGFX_TEST_PRIVATE(ImageReaderTest, AcceptsZeroTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 0;
      auto buffer = reader->acquireNextBuffer(frame); ASSERT_TRUE(buffer != nullptr);
      stream->setFrame(1, 0);

      ExpectFrameID(context, TextureView::MakeFrom(context, buffer), 1);
      EXPECT_EQ(stream->lastTarget, 0);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(frame->timestamp.load(), 0);
      EXPECT_EQ(reader->textureVersion, reader->bufferVersion);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, RejectsOvershotTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 1000;
      auto buffer = reader->acquireNextBuffer(frame); ASSERT_TRUE(buffer != nullptr);
      stream->setFrame(2, 2000);

      EXPECT_TRUE(TextureView::MakeFrom(context, buffer) == nullptr);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Mismatched);
      EXPECT_EQ(frame->timestamp.load(), 2000); ExpectFrameID(context, reader->textureView, 2);
      EXPECT_EQ(reader->textureVersion, 0u); EXPECT_FALSE(buffer->expired());

      stream->setFrame(1, 1000); ExpectFrameID(context, TextureView::MakeFrom(context, buffer), 1);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(frame->timestamp.load(), 1000);
      EXPECT_EQ(reader->textureVersion, reader->bufferVersion); EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, RetriesFailureWithExistingTexture) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      stream->setFrame(1, 1000); auto first = reader->acquireNextBuffer();
      ASSERT_TRUE(first != nullptr); auto firstTexture = TextureView::MakeFrom(context, first);
      ExpectFrameID(context, firstTexture, 1);

      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 2000;
      auto buffer = reader->acquireNextBuffer(frame); ASSERT_TRUE(buffer != nullptr);
      stream->fails = true; EXPECT_TRUE(TextureView::MakeFrom(context, buffer) == nullptr);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Failed);
      EXPECT_EQ(frame->timestamp.load(), 1000); EXPECT_EQ(reader->textureView, firstTexture);
      ExpectFrameID(context, reader->textureView, 1); EXPECT_EQ(reader->textureVersion, 0u);
      EXPECT_FALSE(buffer->expired()); EXPECT_EQ(stream->readCount, 2);

      stream->fails = false; stream->setFrame(2, 2000);
      ExpectFrameID(context, TextureView::MakeFrom(context, buffer), 2);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(frame->timestamp.load(), 2000);
      EXPECT_EQ(reader->textureVersion, reader->bufferVersion); EXPECT_EQ(stream->readCount, 3);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, SeekExpiresOldBufferAndAcceptsZeroTimestamp) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>();
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      auto firstFrame = std::make_shared<ImageReaderFrame>(); firstFrame->targetTimestamp = 1000;
      auto first = reader->acquireNextBuffer(firstFrame); ASSERT_TRUE(first != nullptr);
      stream->setFrame(1, 1000); ExpectFrameID(context, TextureView::MakeFrom(context, first), 1);
      EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);

      reader->notifySeeked(); EXPECT_EQ(stream->seekCount, 1);
      EXPECT_EQ(reader->textureVersion, 0u); EXPECT_TRUE(first->expired());
      EXPECT_TRUE(TextureView::MakeFrom(context, first) == nullptr);
      EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Expired);
      EXPECT_EQ(stream->readCount, 1);

      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 0;
      auto buffer = reader->acquireNextBuffer(frame); ASSERT_TRUE(buffer != nullptr);
      stream->setFrame(2, 0); ExpectFrameID(context, TextureView::MakeFrom(context, buffer), 2);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(frame->timestamp.load(), 0);
      EXPECT_EQ(reader->textureVersion, reader->bufferVersion); EXPECT_EQ(stream->lastTarget, 0);
      EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, ReportsUnsupportedTargetWithoutTimestamps) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto stream = std::make_shared<FakeImageStream>(); stream->providesFrameTimestamp = false;
      auto reader = ImageReader::MakeFrom(stream); ASSERT_TRUE(reader != nullptr);
      auto frame = std::make_shared<ImageReaderFrame>(); frame->targetTimestamp = 1000;
      auto buffer = reader->acquireNextBuffer(frame); ASSERT_TRUE(buffer != nullptr);
      stream->setFrame(1, 1000);

      ExpectFrameID(context, TextureView::MakeFrom(context, buffer), 1);
      EXPECT_EQ(frame->status.load(std::memory_order_acquire), ImageReaderStatus::Unsupported);
      EXPECT_EQ(frame->timestamp.load(), -1); EXPECT_EQ(stream->lastTarget, 1000);
      EXPECT_EQ(stream->readCount, 1);

      auto latestFrame = std::make_shared<ImageReaderFrame>();
      auto latest = reader->acquireNextBuffer(latestFrame); ASSERT_TRUE(latest != nullptr);
      stream->setFrame(2, 2000); ExpectFrameID(context, TextureView::MakeFrom(context, latest), 2);
      EXPECT_EQ(latestFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(latestFrame->timestamp.load(), -1); EXPECT_EQ(stream->lastTarget, -1);
      EXPECT_EQ(stream->readCount, 2);)
}

TGFX_TEST_PRIVATE(ImageReaderTest, SharesAbsoluteDeadlineAcrossReadersAndRetries) {
  TGFX_PRIVATE_ACCESS(
      ContextScope scope; auto context = scope.getContext(); ASSERT_TRUE(context != nullptr);
      auto firstStream = std::make_shared<FakeImageStream>();
      auto secondStream = std::make_shared<FakeImageStream>();
      auto firstReader = ImageReader::MakeFrom(firstStream);
      auto secondReader = ImageReader::MakeFrom(secondStream); ASSERT_TRUE(firstReader != nullptr);
      ASSERT_TRUE(secondReader != nullptr); auto firstFrame = std::make_shared<ImageReaderFrame>();
      auto secondFrame = std::make_shared<ImageReaderFrame>(); auto deadline = Clock::Now() + 5000;
      firstFrame->targetTimestamp = 1000; secondFrame->targetTimestamp = 2000;
      firstFrame->deadline.store(deadline, std::memory_order_release);
      secondFrame->deadline.store(deadline, std::memory_order_release);
      auto first = firstReader->acquireNextBuffer(firstFrame);
      auto second = secondReader->acquireNextBuffer(secondFrame); ASSERT_TRUE(first != nullptr);
      ASSERT_TRUE(second != nullptr); firstStream->setFrame(1, 0); secondStream->setFrame(2, 0);

      for (int read = 0; read < 2; read++) {
        EXPECT_TRUE(TextureView::MakeFrom(context, first) == nullptr);
        EXPECT_TRUE(TextureView::MakeFrom(context, second) == nullptr);
        EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::NotReady);
        EXPECT_EQ(secondFrame->status.load(std::memory_order_acquire), ImageReaderStatus::NotReady);
        ExpectFrameID(context, firstReader->textureView, 1);
        ExpectFrameID(context, secondReader->textureView, 2);
        EXPECT_EQ(firstReader->textureVersion, 0u);
        EXPECT_EQ(secondReader->textureVersion, 0u);
        EXPECT_EQ(firstStream->lastDeadline, deadline);
        EXPECT_EQ(secondStream->lastDeadline, deadline);
        EXPECT_EQ(firstStream->lastTarget, 1000);
        EXPECT_EQ(secondStream->lastTarget, 2000);
      } EXPECT_EQ(firstStream->readCount, 2);
      EXPECT_EQ(secondStream->readCount, 2);

      firstFrame->deadline.store(0, std::memory_order_release);
      secondFrame->deadline.store(0, std::memory_order_release); firstStream->setFrame(3, 1000);
      secondStream->setFrame(4, 2000);
      ExpectFrameID(context, TextureView::MakeFrom(context, first), 3);
      ExpectFrameID(context, TextureView::MakeFrom(context, second), 4);
      EXPECT_EQ(firstStream->lastDeadline, 0); EXPECT_EQ(secondStream->lastDeadline, 0);
      EXPECT_EQ(firstFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(secondFrame->status.load(std::memory_order_acquire), ImageReaderStatus::Ready);
      EXPECT_EQ(firstFrame->timestamp.load(), 1000); EXPECT_EQ(secondFrame->timestamp.load(), 2000);
      EXPECT_EQ(firstStream->readCount, 3); EXPECT_EQ(secondStream->readCount, 3);)
}

}  // namespace tgfx
