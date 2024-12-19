/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <_types/_uint32_t.h>
#include <unistd.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "tgfx/core/Data.h"

namespace tgfx {

bool WriteStream::writeChar(char c) {
  return this->write(&c, 1);
}

bool WriteStream::writeText(const std::string& text) {
  return this->write(text.data(), static_cast<uint32_t>(text.size()));
}

/////////////////////////////////////////////////////////////////////////////////////////////////

FileWriteStream::FileWriteStream(const std::string& path) {
  file = fopen(path.c_str(), "wb");
}

FileWriteStream::~FileWriteStream() {
  if (file) {
    fclose(file);
  }
}

size_t FileWriteStream::bytesWritten() const {
  return static_cast<size_t>(ftell(file));
}

bool FileWriteStream::write(const char* buffer, size_t size) {
  if (file == nullptr) {
    return false;
  }

  if (fwrite(buffer, 1, size, file) != size) {
    file = nullptr;
    return false;
  }
  return true;
}

void FileWriteStream::flush() {
  if (file) {
    fflush(file);
  }
}

void FileWriteStream::sync() {
  flush();
  if (file) {
    auto fd = fileno(file);
    fsync(fd);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////

bool DynamicMemoryWriteStream::write(const char* data, size_t size) {
  buffer.insert(buffer.end(), data, data + size);
  return true;
}

size_t DynamicMemoryWriteStream::bytesWritten() const {
  return buffer.size();
}

bool DynamicMemoryWriteStream::read(char* data, size_t size, size_t offset) {
  if (offset + size > buffer.size()) {
    return false;
  }
  std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
            buffer.begin() + static_cast<std::ptrdiff_t>(offset + size), data);
  return true;
}

std::shared_ptr<Data> DynamicMemoryWriteStream::dumpAsData() {
  return Data::MakeWithCopy(buffer.data(), buffer.size());
}

}  // namespace tgfx