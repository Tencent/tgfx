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

#include "gpu/resources/BufferResource.h"

namespace tgfx {
ScratchKey BufferResource::ComputeScratchKey(size_t size, uint32_t usage) {
  static const uint32_t BufferResourceType = UniqueID::Next();
  BytesKey bytesKey(4);
  bytesKey.write(BufferResourceType);
  bytesKey.write(static_cast<uint32_t>(size & 0xFFFFFFFF));
  bytesKey.write(static_cast<uint32_t>(static_cast<uint64_t>(size) >> 32));
  bytesKey.write(usage);
  return bytesKey;
}

std::shared_ptr<BufferResource> BufferResource::FindOrCreate(Context* context, size_t size,
                                                             uint32_t usage) {
  auto scratchKey = ComputeScratchKey(size, usage);
  auto resource = Resource::Find<BufferResource>(context, scratchKey);
  if (resource != nullptr) {
    return resource;
  }
  auto gpuBuffer = context->gpu()->createBuffer(size, usage);
  if (!gpuBuffer) {
    return nullptr;
  }
  return Wrap(context, std::move(gpuBuffer), scratchKey);
}
}  // namespace tgfx
