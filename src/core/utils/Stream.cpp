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

#include "tgfx/core/Stream.h"
#include <mutex>
#include "core/utils/Log.h"

#ifdef __EMSCRIPTEN__
#include "platform/web/WebStream.h"
#endif

namespace tgfx {
static std::mutex locker = {};
static std::unique_ptr<StreamFactory> streamFactory = nullptr;

class FileStream : public Stream {
 public:
  FileStream(FILE* file, size_t length) : file(file), length(length) {
  }

  ~FileStream() override {
    fclose(file);
  }

  size_t size() const override {
    return length;
  }

  bool seek(size_t position) override {
    return fseek(file, static_cast<long>(position), SEEK_SET) == 0;
  }

  bool move(int offset) override {
    return fseek(file, offset, SEEK_CUR) == 0;
  }

  size_t read(void* buffer, size_t size) override {
    return fread(buffer, 1, size, file);
  }

  bool rewind() override {
    return fseek(file, 0, SEEK_SET) == 0;
  }

 private:
  FILE* file = nullptr;
  size_t length = 0;
};

void Stream::RegisterStreamFactory(std::unique_ptr<StreamFactory> factory) {
  std::lock_guard<std::mutex> autoLock(locker);
  streamFactory = std::move(factory);
}

std::unique_ptr<Stream> Stream::MakeFromFile(const std::string& filePath) {
  if (filePath.find("http://") == 0 || filePath.find("https://") == 0 ||
      filePath.find("assets://")) {
    std::unique_ptr<Stream> stream = nullptr;
    locker.lock();
#ifdef __EMSCRIPTEN__
    if (streamFactory == nullptr) {
      streamFactory = std::unique_ptr<StreamFactory>(new WebStreamFactory());
    }
#endif
    if (streamFactory) {
      stream = streamFactory->createStream(filePath);
    }
    locker.unlock();
    if (stream) {
      return stream;
    }
  }
    auto file = fopen(filePath.c_str(), "rb");
    if (file == nullptr) {
      LOGE("file open failed! filePath:%s \n", filePath.c_str());
      return nullptr;
    }
    fseek(file, 0, SEEK_END);
    auto length = ftell(file);
    if (length <= 0) {
      fclose(file);
      return nullptr;
    }
    fseek(file, 0, SEEK_SET);
    return std::make_unique<FileStream>(file, length);
  }

}  // namespace tgfx
