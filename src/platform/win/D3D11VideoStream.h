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

#pragma once

#ifdef _WIN32

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mutex>
#include <unordered_map>
// Windows SDK defines `interface` as `struct`, which conflicts with tgfx code.
// This must be placed after all Windows SDK headers.
#ifdef interface
#undef interface
#endif
#include "platform/ImageStream.h"

namespace tgfx {

using Microsoft::WRL::ComPtr;

// Forward declaration for shared VideoProcessor
struct SharedVideoProcessor;

/**
 * D3D11VideoStream represents a video stream from D3D11 hardware video decoder.
 * It uses D3D11 Video Processor to convert NV12 (decoder output) to BGRA (OpenGL compatible),
 * and WGL_NV_DX_interop extension to share D3D11 textures with OpenGL.
 */
class D3D11VideoStream : public ImageStream {
 public:
  /**
   * Creates a new D3D11VideoStream with the specified dimensions and D3D11 device.
   * Returns nullptr if the parameters are invalid or WGL_NV_DX_interop is not available.
   */
  static std::shared_ptr<D3D11VideoStream> Make(int width, int height, ID3D11Device* device,
                                                 ID3D11DeviceContext* context);

  ~D3D11VideoStream() override;

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

  /**
   * Returns the D3D11 device.
   */
  ID3D11Device* getD3D11Device() const {
    return d3d11Device.Get();
  }

  /**
   * Returns the D3D11 device context.
   */
  ID3D11DeviceContext* getD3D11Context() const {
    return d3d11Context.Get();
  }

  /**
   * Updates the stream with a decoded NV12 texture from hardware video decoder.
   * This converts NV12 to BGRA using video processor.
   * @param decodedTexture The NV12 texture from hardware decoder.
   * @param subresourceIndex The array slice index for texture arrays.
   * @param srcWidth Actual video width (may be smaller than texture width due to alignment).
   * @param srcHeight Actual video height (may be smaller than texture height due to alignment).
   * @return true if update succeeded.
   */
  bool updateTexture(ID3D11Texture2D* decodedTexture, int subresourceIndex = 0,
                     int srcWidth = 0, int srcHeight = 0);

  /**
   * Returns the shared BGRA texture (for debugging purposes).
   */
  ID3D11Texture2D* getSharedTexture() const {
    return sharedTexture.Get();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override;
  bool onUpdateTexture(std::shared_ptr<TextureView> textureView) override;

 private:
  D3D11VideoStream(int width, int height, ID3D11Device* device, ID3D11DeviceContext* context);

  bool ensureInitialized();  // Lazy initialization - called on first use
  bool initSharedTexture();
  bool initVideoProcessor();

  std::mutex locker;
  std::shared_ptr<ColorSpace> _colorSpace;

  // D3D11 resources
  ComPtr<ID3D11Device> d3d11Device;
  ComPtr<ID3D11DeviceContext> d3d11Context;

  // Single BGRA output texture for D3D11-OpenGL interop.
  ComPtr<ID3D11Texture2D> sharedTexture;

  // Shared video processor (reduces memory and initialization overhead)
  SharedVideoProcessor* sharedVideoProcessor = nullptr;
  
  // Output view for sharedTexture
  ComPtr<ID3D11VideoProcessorOutputView> outputView;

  // Cached input views for each subresource index (MFT uses texture array)
  // MFT typically uses 4-16 surfaces in rotation, so we cache by subresource index
  std::unordered_map<int, ComPtr<ID3D11VideoProcessorInputView>> inputViewCache;
  ID3D11Texture2D* cachedInputTexture = nullptr;  // Raw pointer for comparison only

  // GL-D3D11 interop handles (WGL_NV_DX_interop)
  void* glInteropDevice = nullptr;
  void* glInteropTexture = nullptr;
  unsigned int glTextureId = 0;
  bool interopInitialized = false;
  bool textureLocked = false;
  bool hasPendingUpdate = false;  // True if updateTexture() was called and needs sync
  
  // GL_EXT_memory_object interop (alternative to WGL_NV_DX_interop)
  bool useMemoryObject = false;       // True if using memory_object path
  bool memObjDetectionDone = false;   // True after first detection attempt (prevents retries)
  unsigned int glMemoryObject = 0;
  
  // Lazy initialization state
  bool initialized = false;
  bool initializationFailed = false;

  // Cached Rect values to avoid redundant D3D11 SetRect calls (which trigger implicit GPU flush)
  RECT cachedSrcRect = {-1, -1, -1, -1};
  RECT cachedDstRect = {-1, -1, -1, -1};

  bool initGLInterop();
  bool initGLInteropMemoryObject();
  void releaseGLInterop();
  void releaseGLInteropMemoryObject();
  
  // Get or create input view for the given texture
  ID3D11VideoProcessorInputView* getOrCreateInputView(ID3D11Texture2D* texture, int subresourceIndex);
};

}  // namespace tgfx

#endif  // _WIN32
