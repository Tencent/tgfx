/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "StencilCoverPathBuffer.h"

namespace tgfx {
std::shared_ptr<StencilCoverPathBuffer> StencilCoverPathBuffer::FindOrCreate(
    Context* context, size_t size, PathFillType fillType) {
  static const uint32_t STENCIL_COVER_PATH_BUFFER_TYPE = UniqueID::Next();
  BytesKey bytesKey(3);
  bytesKey.write(STENCIL_COVER_PATH_BUFFER_TYPE);
  bytesKey.write(static_cast<uint32_t>(size & 0xFFFFFFFF));
  bytesKey.write(static_cast<uint32_t>(static_cast<uint64_t>(size) >> 32));
  ScratchKey scratchKey = bytesKey;
  auto resource = Resource::Find<StencilCoverPathBuffer>(context, scratchKey);
  if (resource != nullptr) {
    resource->pathFillType = fillType;
    return resource;
  }
  auto buffer = context->gpu()->createBuffer(size, GPUBufferUsage::VERTEX);
  if (buffer == nullptr) {
    return nullptr;
  }
  return Resource::AddToCache(context, new StencilCoverPathBuffer(std::move(buffer), fillType),
                              scratchKey);
}
}  // namespace tgfx
