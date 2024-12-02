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

#pragma once

#include <memory>
#include <string>
#include <unordered_set>

namespace tgfx {
/**
 * Stream represents a source of bytes. Subclasses can be backed by memory, or a file, or something
 * else. Stream is not thread safe.
 */
class Stream {
 public:
  virtual ~Stream() = default;

  /**
   * Attempts to open the specified file as a stream, returns nullptr on failure.
   */
  static std::unique_ptr<Stream> MakeFromFile(const std::string& filePath);

  /**
   * Returns the total length of the stream. If this cannot be done, returns 0.
   */
  virtual size_t size() const = 0;

  /**
   * Seeks to an absolute position in the stream. If this cannot be done, returns false. If an
   * attempt is made to seek past the end of the stream, the position will be set to the end of the
   * stream.
   */
  virtual bool seek(size_t position) = 0;

  /**
   * Seeks to an relative offset in the stream. If this cannot be done, returns false. If an attempt
   * is made to move to a position outside the stream, the position will be set to the closest point
   * within the stream (beginning or end).
   */
  virtual bool move(int offset) = 0;

  /**
   * Reads size number of bytes. Copy size bytes into buffer, return how many were copied.
   */
  virtual size_t read(void* buffer, size_t size) = 0;

  /**
   * Rewinds to the beginning of the stream. Returns true if the stream is known to be at the
   * beginning after this call returns.
   */
  virtual bool rewind() = 0;
};

/**
 * StreamFactory creates instances of Stream and can be inherited externally to implement the
 * loading of cache files without local path, such as caching data into a database, or assets
 * resource files on the Android platform.
 */
class StreamFactory {
 public:
  /**
  * Registers a custom protocol with the specified factory. The factory will create streams for
  * paths that start with the specified protocol. customProtocol can use protocol headers like
  * "assets://".
  */
  static void RegisterCustomProtocol(const std::unordered_set<std::string>& customProtocols,
                                     std::unique_ptr<StreamFactory> factory);

  virtual ~StreamFactory() = default;
  /**
   * Creates a Stream instance for the specified file path. The path needs to start with
   * customProtocol.
   */
  virtual std::unique_ptr<Stream> createStream(const std::string& filePath) = 0;
};

}  // namespace tgfx
