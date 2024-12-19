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

#pragma once

#include <_stdio.h>
#include <_types/_uint8_t.h>
#include <cstddef>
#include <cstring>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "tgfx/core/Data.h"

namespace tgfx {

class WriteStream {
 public:
  WriteStream() = default;

  virtual ~WriteStream() = default;

  virtual bool write(const char* buffer, size_t size) = 0;

  virtual void flush(){};

  virtual size_t bytesWritten() const = 0;

  bool writeChar(char c);

  bool writeText(const std::string& text);
};

class FileWriteStream : public WriteStream {
 public:
  explicit FileWriteStream(const std::string& path);
  ~FileWriteStream() override;

  bool isValid() const {
    return file != nullptr;
  }

  bool write(const char* buffer, size_t size) override;

  void flush() override;

  void sync();

  size_t bytesWritten() const override;

 private:
  FILE* file;
};

class DynamicMemoryWriteStream : public WriteStream {
 public:
  DynamicMemoryWriteStream() = default;

  ~DynamicMemoryWriteStream() override = default;

  bool write(const char* data, size_t size) override;

  size_t bytesWritten() const override;

  bool read(char* data, size_t size, size_t offset);

  std::shared_ptr<Data> dumpAsData();

 private:
  std::vector<char> buffer;
};

}  // namespace tgfx