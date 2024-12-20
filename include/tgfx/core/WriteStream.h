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

#include <memory>
#include <string>
#include <vector>
#include "tgfx/core/Data.h"

namespace tgfx {

/**
 * WriteStream represents a destination for bytes. The size of the stream is dynamic and does not
 * need to be initialized to a fixed size. Subclasses can be backed by memory, a file, or other
 * storage. Note that WriteStream is not thread-safe.
 */
class WriteStream {
 public:
  /**
   * Creates a new WriteStream object to write data to the specified file path. Returns nullptr if
   * the file path is invalid or if there are insufficient permissions to write to the file.
   */
  static std::shared_ptr<WriteStream> MakeFromFile(const std::string& filePath);

  /**
   * Destroy the Write Stream object
   */
  virtual ~WriteStream() = default;

  /**
   * Writes bytes to the stream. Returns true if the write was successful. The actual write
   * operation is implemented in the subclasses.
   */
  virtual bool write(const void* data, size_t size) = 0;

  /**
   * Writes text to the stream. Returns true if the write was successful.
   */
  bool writeText(const std::string& text) {
    return this->write(text.data(), text.size());
  };

  /**
   * Returns the current size of the written bytes.
   */
  virtual size_t bytesWritten() const = 0;

  /**
   * Flushes the data in the buffer to the target storage.
   */
  virtual void flush() = 0;
};

/**
 * MemoryWriteStream allows writing data to memory. The data written is stored in a buffer and can
 * be read back using the read method. The buffer can be dumped as a Data object.
 */
class MemoryWriteStream : public WriteStream {
 public:
  /**
   * Creates a new MemoryWriteStream object.
   */
  static std::shared_ptr<MemoryWriteStream> Make();

  /**
   * Destroy the MemoryWriteStream object
   */
  ~MemoryWriteStream() override = default;

  /**
   * Writes bytes to the memory stream. Returns true if the write was successful.
   */
  bool write(const void* data, size_t size) override;

  /**
   * Returns the current size of the written bytes.
   */
  size_t bytesWritten() const override;

  /**
   * The flush operation is a no-op for memory writes, so this is an empty implementation.
   */
  void flush() override{};

  /**
   * Reads a segment of the buffer into the provided data. Returns false if the offset and size 
   * exceed the buffer's range.
   */
  bool read(void* data, size_t offset, size_t size);

  /**
   * Copies all the buffer bytes into a Data object.
   */
  std::shared_ptr<Data> readData();

  /** 
   * Return the contents as string.
   */
  std::string readString();

 private:
  /**
   * Creates a new MemoryWriteStream object.
   */
  MemoryWriteStream() = default;

  std::vector<uint8_t> buffer{};
};

}  // namespace tgfx