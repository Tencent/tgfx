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

#include <memory>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Window.h"
#include "tgfx/gpu/d3d12/D3D12Device.h"

#ifdef _WIN32
struct HWND__;
typedef HWND__* HWND;
#endif

namespace tgfx {

/**
 * D3D12Window manages an IDXGISwapChain3 and its backbuffer textures for presenting rendered
 * content to a Win32 window. Each frame the current backbuffer is exposed as a RenderTarget
 * through the standard Window/Surface API; on present the swap chain flips to the next buffer.
 */
class D3D12Window : public Window {
 public:
#ifdef _WIN32
  /**
   * Creates a D3D12Window from a Win32 window handle. Returns nullptr if the swap chain cannot
   * be created. Note: only sRGB output is currently supported. The colorSpace parameter is
   * accepted for forward compatibility but non-sRGB values are ignored with a warning.
   */
  static std::shared_ptr<D3D12Window> MakeFrom(HWND hwnd, std::shared_ptr<D3D12Device> device,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);
#endif

  ~D3D12Window() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;
  void onPresent(Context* context) override;

 private:
  // PImpl: all DXGI / D3D12 handles and per-backbuffer state live in PlatformState (defined in
  // the .cpp) so this header pulls in neither dxgi.h nor d3d12.h.
  struct PlatformState;

  explicit D3D12Window(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state,
                       std::shared_ptr<ColorSpace> colorSpace);

  std::unique_ptr<PlatformState> _platformState;
};

}  // namespace tgfx
