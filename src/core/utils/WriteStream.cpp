/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/WriteStream.h"
#include <cstring>
#include "tgfx/core/Data.h"

namespace tgfx {

/**
 * FileWriteStream allows writing data to a disk file. The data written does not need to remain in
 * memory and can be flushed to disk using the flush method.
 */
class FileWriteStream : public WriteStream {
 public:
  explicit FileWriteStream(FILE* file) : file(file) {};

  ~FileWriteStream() override {
    if (file) {
      fclose(file);
    }
  };

  bool write(const void* data, size_t size) override {
    if (file == nullptr) {
      return false;
    }

    if (fwrite(data, 1, size, file) != size) {
      file = nullptr;
      return false;
    }
    return true;
  }

  size_t bytesWritten() const override {
    return static_cast<size_t>(ftell(file));
  }

  /**
   * Flushes the data in the buffer to the target storage. Note that the data is not guaranteed to
   * be immediately written to the storage device; it is only flushed to the system buffer.
   */
  void flush() override {
    if (file) {
      fflush(file);
    }
  }

 private:
  FILE* file = nullptr;
};

/////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<WriteStream> WriteStream::MakeFromFile(const std::string& filePath) {
  if (filePath.empty()) {
    return nullptr;
  }
  auto file = fopen(filePath.c_str(), "wb");
  if (file == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<WriteStream>(new FileWriteStream(file));
}

/////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<MemoryWriteStream> MemoryWriteStream::Make() {
  return std::shared_ptr<MemoryWriteStream>(new MemoryWriteStream());
}

bool MemoryWriteStream::write(const void* data, size_t size) {
  const auto bytes = static_cast<const uint8_t*>(data);
  buffer.insert(buffer.end(), bytes, bytes + size);
  return true;
}

bool MemoryWriteStream::writeToAndReset(const std::shared_ptr<MemoryWriteStream>& destStream) {
  if (!destStream) {
    return false;
  }
  if (bytesWritten() == 0) {
    return true;
  }
  if (destStream->bytesWritten() == 0) {
    destStream->buffer.swap(buffer);
    return true;
  }
  destStream->buffer.insert(destStream->buffer.end(), buffer.begin(), buffer.end());
  reset();
  return true;
}

void MemoryWriteStream::writeToStream(const std::shared_ptr<MemoryWriteStream>& destStream) {
  if (!destStream) {
    return;
  }
  destStream->buffer.insert(destStream->buffer.end(), buffer.begin(), buffer.end());
}

bool MemoryWriteStream::prependToAndReset(const std::shared_ptr<MemoryWriteStream>& destStream) {
  if (!destStream) {
    return false;
  }
  if (bytesWritten() == 0) {
    return true;
  }
  if (destStream->bytesWritten() == 0) {
    destStream->buffer.swap(buffer);
    return true;
  }
  buffer.insert(buffer.end(), destStream->buffer.begin(), destStream->buffer.end());
  destStream->buffer.swap(buffer);
  reset();
  return true;
}

size_t MemoryWriteStream::bytesWritten() const {
  return buffer.size();
}

bool MemoryWriteStream::read(void* data, size_t offset, size_t size) {
  if (offset + size > buffer.size()) {
    return false;
  }
  const auto bytes = buffer.data() + offset;
  memcpy(data, bytes, size);
  return true;
}

std::shared_ptr<Data> MemoryWriteStream::readData() {
  return Data::MakeWithCopy(buffer.data(), buffer.size());
}

std::string MemoryWriteStream::readString() {
  return std::string(buffer.begin(), buffer.end());
}

void MemoryWriteStream::reset() {
  buffer.clear();
  buffer.shrink_to_fit();
}

}  // namespace tgfx
