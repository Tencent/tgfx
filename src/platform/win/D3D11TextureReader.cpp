/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#ifdef _WIN32

// Include D3D11VideoStream.h first to ensure Windows headers are included before tgfx headers
#include "platform/win/D3D11VideoStream.h"
#include "tgfx/platform/win/D3D11TextureReader.h"

namespace tgfx {

std::shared_ptr<D3D11TextureReader> D3D11TextureReader::Make(int width, int height,
                                                              ID3D11Device* device,
                                                              ID3D11DeviceContext* context) {
  if (width < 1 || height < 1 || device == nullptr || context == nullptr) {
    return nullptr;
  }

  auto videoStream = D3D11VideoStream::Make(width, height, device, context);
  if (videoStream == nullptr) {
    return nullptr;
  }

  auto reader =
      std::shared_ptr<D3D11TextureReader>(new D3D11TextureReader(std::move(videoStream)));
  reader->weakThis = reader;
  return reader;
}

D3D11TextureReader::D3D11TextureReader(std::shared_ptr<D3D11VideoStream> videoStream)
    : ImageReader(std::move(videoStream)) {
}

std::shared_ptr<D3D11VideoStream> D3D11TextureReader::getStream() const {
  return std::static_pointer_cast<D3D11VideoStream>(stream);
}

ID3D11Device* D3D11TextureReader::getD3D11Device() const {
  return getStream()->getD3D11Device();
}

ID3D11DeviceContext* D3D11TextureReader::getD3D11Context() const {
  return getStream()->getD3D11Context();
}

bool D3D11TextureReader::updateTexture(ID3D11Texture2D* decodedTexture, int subresourceIndex) {
  return getStream()->updateTexture(decodedTexture, subresourceIndex);
}

}  // namespace tgfx

#endif  // _WIN32
