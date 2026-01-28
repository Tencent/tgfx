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

#pragma once

#ifdef _WIN32

#include <d3d11.h>
// Windows SDK defines `interface` as `struct`, which conflicts with tgfx code.
// This must be placed after all Windows SDK headers.
#ifdef interface
#undef interface
#endif
#include "tgfx/platform/ImageReader.h"

namespace tgfx {

class D3D11VideoStream;

/**
 * D3D11TextureReader allows direct access to image data from D3D11 hardware video decoder.
 * It uses D3D11 Video Processor to convert NV12 to BGRA and WGL_NV_DX_interop for
 * D3D11-OpenGL texture sharing. D3D11TextureReader is safe across threads.
 */
class D3D11TextureReader : public ImageReader {
 public:
  /**
   * Creates a new D3D11TextureReader with the specified image size and D3D11 device.
   * Returns nullptr if the parameters are invalid or WGL_NV_DX_interop is not available.
   * @param width The width of generated ImageBuffers.
   * @param height The height of generated ImageBuffers.
   * @param device The D3D11 device used by the hardware video decoder.
   * @param context The D3D11 device context.
   */
  static std::shared_ptr<D3D11TextureReader> Make(int width, int height, ID3D11Device* device,
                                                   ID3D11DeviceContext* context);

  /**
   * Returns the D3D11 device.
   */
  ID3D11Device* getD3D11Device() const;

  /**
   * Returns the D3D11 device context.
   */
  ID3D11DeviceContext* getD3D11Context() const;

  /**
   * Updates the reader with a decoded NV12 texture from hardware video decoder.
   * This converts NV12 to BGRA using video processor.
   * @param decodedTexture The NV12 texture from hardware decoder.
   * @param subresourceIndex The array slice index for texture arrays.
   * @return true if update succeeded.
   */
  bool updateTexture(ID3D11Texture2D* decodedTexture, int subresourceIndex = 0);

 private:
  explicit D3D11TextureReader(std::shared_ptr<D3D11VideoStream> stream);

  std::shared_ptr<D3D11VideoStream> getStream() const;
};

/**
 * Check if WGL_NV_DX_interop extension is available for D3D11-OpenGL texture sharing.
 * This function will create a temporary OpenGL context to detect extension support.
 */
bool IsNVDXInteropAvailable();

}  // namespace tgfx

#endif  // _WIN32
