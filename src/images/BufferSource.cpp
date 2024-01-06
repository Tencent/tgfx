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

#include "BufferSource.h"

namespace tgfx {
BufferSource::BufferSource(UniqueKey uniqueKey, std::shared_ptr<ImageBuffer> buffer, bool mipMapped)
    : ImageSource(std::move(uniqueKey)), imageBuffer(std::move(buffer)), mipMapped(mipMapped) {
}

std::shared_ptr<ImageSource> BufferSource::onMakeMipMapped() const {
  return std::shared_ptr<BufferSource>(new BufferSource(UniqueKey::MakeWeak(), imageBuffer, true));
}

std::shared_ptr<TextureProxy> BufferSource::onMakeTextureProxy(Context* context, uint32_t) const {
  return context->proxyProvider()->createTextureProxy(imageBuffer, mipMapped);
}
}  // namespace tgfx
