/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ImageDecoder.h"
#include "tgfx/utils/Task.h"

namespace tgfx {

class ImageBufferWrapper : public ImageDecoder {
 public:
  explicit ImageBufferWrapper(std::shared_ptr<ImageBuffer> imageBuffer)
      : imageBuffer(std::move(imageBuffer)) {
  }

  int width() const override {
    return imageBuffer->width();
  }

  int height() const override {
    return imageBuffer->height();
  }

  bool isAlphaOnly() const override {
    return imageBuffer->isAlphaOnly();
  }

  std::shared_ptr<ImageBuffer> decode() const override {
    return imageBuffer;
  }

 private:
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
};

class ImageGeneratorWrapper : public ImageDecoder {
 public:
  explicit ImageGeneratorWrapper(std::shared_ptr<ImageGenerator> generator, bool tryHardware)
      : imageGenerator(std::move(generator)), tryHardware(tryHardware) {
  }

  int width() const override {
    return imageGenerator->width();
  }

  int height() const override {
    return imageGenerator->height();
  }

  bool isAlphaOnly() const override {
    return imageGenerator->isAlphaOnly();
  }

  std::shared_ptr<ImageBuffer> decode() const override {
    return imageGenerator->makeBuffer(tryHardware);
  }

 private:
  std::shared_ptr<ImageGenerator> imageGenerator = nullptr;
  bool tryHardware = true;
};

struct ImageBufferHolder {
  std::shared_ptr<ImageBuffer> imageBuffer;
};

class AsyncImageDecoder : public ImageDecoder {
 public:
  AsyncImageDecoder(std::shared_ptr<ImageGenerator> generator, bool tryHardware)
      : imageGenerator(std::move(generator)) {
    holder = std::make_shared<ImageBufferHolder>();
    task = Task::Run([result = holder, generator = imageGenerator, tryHardware]() {
      result->imageBuffer = generator->makeBuffer(tryHardware);
    });
  }

  ~AsyncImageDecoder() override {
    task->cancel();
  }

  int width() const override {
    return imageGenerator->width();
  }

  int height() const override {
    return imageGenerator->height();
  }

  bool isAlphaOnly() const override {
    return imageGenerator->isAlphaOnly();
  }

  std::shared_ptr<ImageBuffer> decode() const override {
    task->wait();
    return holder->imageBuffer;
  }

 private:
  std::shared_ptr<ImageGenerator> imageGenerator = nullptr;
  std::shared_ptr<ImageBufferHolder> holder = nullptr;
  std::shared_ptr<Task> task = nullptr;
};

std::shared_ptr<ImageDecoder> ImageDecoder::Wrap(std::shared_ptr<ImageBuffer> imageBuffer) {
  if (imageBuffer == nullptr) {
    return nullptr;
  }
  return std::make_shared<ImageBufferWrapper>(std::move(imageBuffer));
}

std::shared_ptr<ImageDecoder> ImageDecoder::MakeFrom(std::shared_ptr<ImageGenerator> generator,
                                                     bool tryHardware, bool asyncDecoding) {
  if (generator == nullptr) {
    return nullptr;
  }
  if (asyncDecoding && generator->asyncSupport()) {
    auto imageBuffer = generator->makeBuffer(tryHardware);
    return Wrap(std::move(imageBuffer));
  }
  if (asyncDecoding) {
    return std::make_shared<AsyncImageDecoder>(std::move(generator), tryHardware);
  }
  return std::make_shared<ImageGeneratorWrapper>(std::move(generator), tryHardware);
}
}  // namespace tgfx
