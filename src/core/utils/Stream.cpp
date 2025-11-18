/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include <cstring>
#include <memory>
#include <mutex>
#include <regex>
#include <unordered_map>
#include <utility>
#include "../../../include/tgfx/core/Log.h"
#include "tgfx/core/Data.h"

namespace tgfx {
static std::mutex& locker = *new std::mutex;
static std::unordered_map<std::string, std::shared_ptr<StreamFactory>>& customProtocolsMap =
    *new std::unordered_map<std::string, std::shared_ptr<StreamFactory>>();

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

class MemoryStream : public Stream {
 public:
  explicit MemoryStream(std::shared_ptr<Data> data) : data(std::move(data)) {
  }

  ~MemoryStream() override = default;

  size_t size() const override {
    return data->size();
  }

  bool seek(size_t position) override {
    offset = position > data->size() ? data->size() : position;
    return true;
  }

  bool move(int moveOffset) override {
    return this->seek(offset + static_cast<size_t>(moveOffset));
  }

  size_t read(void* buffer, size_t size) override {
    size_t dataSize = data->size();

    if (size > dataSize - offset) {
      size = dataSize - offset;
    }
    if (buffer) {
      memcpy(buffer, data->bytes() + offset, size);
    }
    offset += size;
    return size;
  }

  bool rewind() override {
    offset = 0;
    return true;
  }

  const void* getMemoryBase() override {
    return data->data();
  }

 private:
  std::shared_ptr<Data> data = nullptr;
  size_t offset = 0;
};

std::string GetProtocolFromPath(const std::string& path) {
  size_t pos = path.find("://");
  if (pos != std::string::npos) {
    return path.substr(0, pos + 3);
  }
  return "";
}

std::unique_ptr<Stream> Stream::MakeFromFile(const std::string& filePath) {
  if (filePath.empty()) {
    return nullptr;
  }
  auto protocol = GetProtocolFromPath(filePath);
  if (!protocol.empty()) {
    locker.lock();
    auto streamFactory = customProtocolsMap[protocol];
    locker.unlock();
    if (streamFactory) {
      auto stream = streamFactory->createStream(filePath);
      if (stream) {
        return stream;
      }
    }
  }
  auto file = fopen(filePath.c_str(), "rb");
  if (file == nullptr) {
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

std::unique_ptr<Stream> Stream::MakeFromData(std::shared_ptr<Data> data) {
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_unique<MemoryStream>(data);
}

void StreamFactory::RegisterCustomProtocol(const std::string& customProtocol,
                                           std::shared_ptr<StreamFactory> factory) {
  if (customProtocol.empty() || factory == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(locker);
  customProtocolsMap[customProtocol] = factory;
}

void StreamFactory::UnRegisterCustomProtocol(const std::string& customProtocol) {
  if (customProtocol.empty()) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(locker);
  customProtocolsMap.erase(customProtocol);
}

}  // namespace tgfx
