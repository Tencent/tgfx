/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "LZ4CompressionHandler.h"
#include "Protocol.h"

namespace tgfx::inspect {
#ifdef INSPECTOR_USE_SYSTEM_LZ4
#include <compression.h>

class AppleLZ4CompressionHandler : public LZ4CompressionHandler {
 public:
  AppleLZ4CompressionHandler() {
    auto scratchSize = compression_encode_scratch_buffer_size(COMPRESSION_LZ4);
    if (scratchSize > 0) {
      scratchBuffer = new (std::nothrow) uint8_t[scratchSize];
    }
  }

  ~AppleLZ4CompressionHandler() override {
    delete[] scratchBuffer;
  }

  size_t encode(uint8_t* dstBuffer, size_t dstSize, const uint8_t* srcBuffer,
                size_t srcSize) const override {
    return compression_encode_buffer(dstBuffer, dstSize, srcBuffer, srcSize, scratchBuffer,
                                     COMPRESSION_LZ4);
  }

  size_t decode(uint8_t* dstBuffer, size_t dstSize, const uint8_t* srcBuffer,
                size_t srcSize) const override {
    return compression_decode_buffer(dstBuffer, dstSize, srcBuffer, srcSize, scratchBuffer,
                                     COMPRESSION_LZ4);
  }

  void reset() override {
  }

 private:
  uint8_t* scratchBuffer = nullptr;
};

std::unique_ptr<LZ4CompressionHandler> LZ4CompressionHandler::Make() {
  return std::make_unique<AppleLZ4CompressionHandler>();
}

size_t LZ4CompressionHandler::GetMaxOutputSize(size_t inputSize) {
  return inputSize + LZ4HeaderSize;
}
#else
#include "lz4.h"

class DefaultLZ4CompressionHandler : public LZ4CompressionHandler {
 public:
  DefaultLZ4CompressionHandler()
      : lz4EncodeStream(LZ4_createStream()), lz4DecodeStream(LZ4_createStreamDecode()) {
    LZ4_setStreamDecode(lz4DecodeStream, nullptr, 0);
  }

  ~DefaultLZ4CompressionHandler() override {
    if (lz4DecodeStream) {
      LZ4_freeStreamDecode(lz4DecodeStream);
      lz4DecodeStream = nullptr;
    }
    if (lz4EncodeStream) {
      LZ4_freeStream(lz4EncodeStream);
      lz4EncodeStream = nullptr;
    }
  }

  size_t encode(uint8_t* dstBuffer, size_t dstSize, const uint8_t* srcBuffer,
                size_t srcSize) const override {
    return static_cast<size_t>(
        LZ4_compress_fast_continue(lz4EncodeStream, reinterpret_cast<const char*>(srcBuffer),
                                   reinterpret_cast<char*>(dstBuffer), static_cast<int>(srcSize),
                                   static_cast<int>(dstSize), 1));
  }

  size_t decode(uint8_t* dstBuffer, size_t dstSize, const uint8_t* srcBuffer,
                size_t srcSize) const override {
    return static_cast<size_t>(LZ4_decompress_safe_continue(
        lz4DecodeStream, reinterpret_cast<const char*>(srcBuffer),
        reinterpret_cast<char*>(dstBuffer), static_cast<int>(srcSize), static_cast<int>(dstSize)));
  }

  void reset() override {
    LZ4_resetStream(lz4EncodeStream);
  }

 private:
  LZ4_stream_t* lz4EncodeStream = nullptr;
  LZ4_streamDecode_t* lz4DecodeStream = nullptr;
};

std::unique_ptr<LZ4CompressionHandler> LZ4CompressionHandler::Make() {
  return std::make_unique<DefaultLZ4CompressionHandler>();
}

size_t LZ4CompressionHandler::GetMaxOutputSize(size_t inputSize) {
  return static_cast<size_t>(LZ4_compressBound(static_cast<int>(inputSize)));
}
#endif
}  // namespace tgfx::inspect