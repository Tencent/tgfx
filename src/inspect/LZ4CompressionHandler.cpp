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
#include "lz4.h"

namespace tgfx::inspect {
class DefaultLZ4CompressionHandler : public LZ4CompressionHandler {
 public:
  size_t encode(uint8_t* dstBuffer, size_t dstSize, const uint8_t* srcBuffer,
                size_t srcSize) const override {
    return static_cast<size_t>(LZ4_compress_default(
        reinterpret_cast<const char*>(srcBuffer), reinterpret_cast<char*>(dstBuffer),
        static_cast<int>(srcSize), static_cast<int>(dstSize)));
  }
};

std::unique_ptr<LZ4CompressionHandler> LZ4CompressionHandler::Make() {
  return std::make_unique<DefaultLZ4CompressionHandler>();
}

size_t LZ4CompressionHandler::GetMaxOutputSize(size_t inputSize) {
  return static_cast<size_t>(LZ4_compressBound(static_cast<int>(inputSize)));
}
}  // namespace tgfx::inspect