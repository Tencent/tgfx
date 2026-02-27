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

#include "DeflateStream.h"
#include <cstddef>
#include <cstring>
#include "core/utils/Log.h"
#include "tgfx/core/WriteStream.h"
#include "zlib.h"

namespace tgfx {

#define DEFLATE_STREAM_INPUT_BUFFER_SIZE 4096
// 4096 + 128, usually big enough to always do a single loop.
#define DEFLATE_STREAM_OUTPUT_BUFFER_SIZE 4224

namespace {

template <typename T>
void* AllocFunction(void*, T items, T size) {
  const size_t maxItems = SIZE_MAX / size;
  if (maxItems < items) {
    return nullptr;
  }
  return malloc(static_cast<size_t>(items) * static_cast<size_t>(size));
}

void FreeFunction(void*, void* address) {
  free(address);
}

void DoDeflate(int flush, z_stream* zStream, WriteStream* out, unsigned char* inBuffer,
               size_t inBufferSize) {
  zStream->next_in = inBuffer;
  zStream->avail_in = static_cast<uInt>(inBufferSize);
  unsigned char outBuffer[DEFLATE_STREAM_OUTPUT_BUFFER_SIZE];
  do {
    zStream->next_out = outBuffer;
    zStream->avail_out = sizeof(outBuffer);
    deflate(zStream, flush);
    DEBUG_ASSERT(!zStream->msg);

    out->write(outBuffer, sizeof(outBuffer) - zStream->avail_out);
  } while (zStream->avail_in || !zStream->avail_out);
}

}  // namespace

struct DeflateWriteStream::Impl {
  WriteStream* outStream;
  unsigned char inBuffer[DEFLATE_STREAM_INPUT_BUFFER_SIZE];
  size_t inBufferIndex;
  z_stream ZStream;
};

DeflateWriteStream::DeflateWriteStream(WriteStream* outStream, int compressionLevel, bool gzip)
    : impl(std::make_unique<DeflateWriteStream::Impl>()) {

  // There has existed at some point at least one zlib implementation which thought it was being
  // clever by randomizing the compression level. This is actually not entirely incorrect, except
  // for the no-compression level which should always be deterministically pass-through.
  // Users should instead consider the zero compression level broken and handle it themselves.
  DEBUG_ASSERT(compressionLevel != 0);

  impl->outStream = outStream;
  impl->inBufferIndex = 0;
  if (!impl->outStream) {
    return;
  }
  impl->ZStream.next_in = nullptr;
  impl->ZStream.zalloc = &AllocFunction;
  impl->ZStream.zfree = &FreeFunction;
  impl->ZStream.opaque = nullptr;
  DEBUG_ASSERT((compressionLevel <= 9 && compressionLevel >= -1));
  deflateInit2(&impl->ZStream, compressionLevel, Z_DEFLATED, gzip ? 0x1F : 0x0F, 8,
               Z_DEFAULT_STRATEGY);
}

DeflateWriteStream::~DeflateWriteStream() {
  this->finalize();
}

void DeflateWriteStream::finalize() {
  if (!impl->outStream) {
    return;
  }
  DoDeflate(Z_FINISH, &impl->ZStream, impl->outStream, impl->inBuffer, impl->inBufferIndex);
  (void)deflateEnd(&impl->ZStream);
  impl->outStream = nullptr;
}

bool DeflateWriteStream::write(const void* data, size_t size) {
  if (!impl->outStream) {
    return false;
  }
  const char* buffer = static_cast<const char*>(data);
  while (size > 0) {
    size_t tocopy = std::min(size, sizeof(impl->inBuffer) - impl->inBufferIndex);
    memcpy(impl->inBuffer + impl->inBufferIndex, buffer, tocopy);
    size -= tocopy;
    buffer += tocopy;
    impl->inBufferIndex += tocopy;
    DEBUG_ASSERT(impl->inBufferIndex <= sizeof(impl->inBuffer));

    // if the buffer isn't filled, don't call into zlib yet.
    if (sizeof(impl->inBuffer) == impl->inBufferIndex) {
      DoDeflate(Z_NO_FLUSH, &impl->ZStream, impl->outStream, impl->inBuffer, impl->inBufferIndex);
      impl->inBufferIndex = 0;
    }
  }
  return true;
}

size_t DeflateWriteStream::bytesWritten() const {
  return impl->ZStream.total_in + impl->inBufferIndex;
}

}  // namespace tgfx
