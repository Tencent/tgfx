/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextureTag.h"
#include "DataContext.h"

namespace inspector {
void ReadTextureTag(DecodeStream* stream) {
  auto count = stream->readEncodedUint32();
  auto context = dynamic_cast<DataContext*>(stream->context);
  auto& textures = context->textures;
  for (uint32_t i = 0; i < count; ++i) {
    auto ptr = std::make_shared<TextureData>();
    auto childIndex = stream->readEncodedUint32();
    auto& inputTextures = ptr->inputTextures;
    auto inputTexturesCount = stream->readEncodedUint32();
    inputTextures.reserve(inputTexturesCount);
    for (uint32_t j = 0; j < inputTexturesCount; ++j) {
      inputTextures[j] = stream->readData();
    }
    ptr->outputTexture = stream->readData();

    textures[childIndex] = ptr;
  }
}

TagType WriteTextureTag(EncodeStream* stream,
                        std::unordered_map<uint32_t, std::shared_ptr<TextureData>>* textures) {
  stream->writeEncodedUint32(static_cast<uint32_t>(textures->size()));
  for (const auto& texture : *textures) {
    stream->writeEncodedUint32(texture.first);
    const auto& textureData = texture.second;
    stream->writeEncodedUint32(static_cast<uint32_t>(textureData->inputTextures.size()));
    for (const auto& inputTexture : textureData->inputTextures) {
      stream->writeData(inputTexture.get());
    }
    stream->writeData(textureData->outputTexture.get());
  }

  return TagType::Texture;
}

}  // namespace inspector
