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
  bool writeText(const std::string& text);

  /**
   * Returns the current size of the written bytes.
   */
  virtual size_t size() const = 0;

  /**
   * Flushes the data in the buffer to the target storage.
   */
  virtual void flush() = 0;
};

/**
 * FileWriteStream allows writing data to a disk file. The data written does not need to remain in
 * memory and can be flushed to disk using the flush method.
 */
class FileWriteStream : public WriteStream {
 public:
  /**
   * Creates a new FileWriteStream object with the specified file path. If the return value is
   * nullptr, it may indicate that the directory path is incorrect or that there are insufficient
   * permissions for the directory path.
   */
  static std::unique_ptr<FileWriteStream> MakeFromPath(const std::string& outputPath);

  /**
   * Destroy the FileWriteStream object
   */
  ~FileWriteStream() override;

  /**
   * Returns true if the file is valid and open.
   */
  bool isValid() const {
    return file != nullptr;
  }

  /**
   * Writes bytes to the file stream. Returns true if the write was successful.
   */
  bool write(const void* data, size_t size) override;

  /**
   * Returns the current size of the written bytes.
   */
  size_t size() const override;

  /**
   * Flushes the data in the buffer to the target storage. Note that the data is not guaranteed to
   * be immediately written to the storage device; it is only flushed to the system buffer.
   */
  void flush() override;

  /**
   * Synchronizes the file's in-memory state with the underlying storage device. This method is
   * useful when the file needs to be read by another process or when the file is critical and
   * cannot be lost.
   */
  void sync();

 private:
  /**
   * Creates a new FileWriteStream object with the specified file path.
   */
  explicit FileWriteStream(const std::string& outputPath);

  FILE* file;
};

/**
 * MemoryWriteStream allows writing data to memory. The data written is stored in a buffer and can be
 * read back using the read method. The buffer can be dumped as a Data object.
 */
class MemoryWriteStream : public WriteStream {
 public:
  /**
   * Creates a new MemoryWriteStream object.
   */
  static std::unique_ptr<MemoryWriteStream> Make();

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
  size_t size() const override;

  /**
   * The flush operation is a no-op for memory writes, so this is an empty implementation.
   */
  void flush() override{};

  /**
   * Copies a range of the buffer bytes into a Data object. If offset or length is out of range,
   * they are clamped to the beginning and end of the buffer. Returns nullptr if the clamped length
   * is 0.
   */
  std::shared_ptr<Data> copyRange(size_t offset, size_t length);

  /**
   * Copies all the buffer bytes into a Data object.
   */
  std::shared_ptr<Data> copy();

 private:
  /**
   * Creates a new MemoryWriteStream object.
   */
  MemoryWriteStream() = default;

  std::vector<uint8_t> buffer;
};

}  // namespace tgfx