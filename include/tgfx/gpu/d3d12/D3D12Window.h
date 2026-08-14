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
   * Creates an opaque D3D12Window bound directly to a Win32 window handle.
   *
   * Mirrors IDXGIFactory2::CreateSwapChainForHwnd: an opaque FLIP_DISCARD swap chain is
   * created for hwnd and DirectComposition is not involved.
   *
   * @param hwnd The target Win32 window. Must be non-null.
   * @param device The D3D12 device used to create the swap chain resources. Must be non-null.
   * @param colorSpace Optional color space for the output. Only sRGB is currently supported;
   *        any non-sRGB value is ignored with a logged warning.
   * @return A new D3D12Window, or nullptr if the swap chain cannot be created.
   */
  static std::shared_ptr<D3D12Window> MakeForHwnd(HWND hwnd, std::shared_ptr<D3D12Device> device,
                                                  std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a transparent D3D12Window that composes with the desktop via DirectComposition.
   *
   * Mirrors IDXGIFactory2::CreateSwapChainForComposition: a FLIP_SEQUENTIAL swap chain with
   * DXGI_ALPHA_MODE_PREMULTIPLIED is created and attached to a DirectComposition visual tree
   * rooted at hwnd, letting the window blend with the desktop and other UI beneath it.
   *
   * The backbuffer is declared as premultiplied alpha: transparent regions must be cleared or
   * drawn with alpha < 1 for the composited result to show through. The hwnd itself does NOT
   * need WS_EX_LAYERED; DirectComposition routes composition outside the redirection bitmap.
   *
   * @param hwnd The target Win32 window. Must be non-null.
   * @param device The D3D12 device used to create the swap chain resources. Must be non-null.
   * @param colorSpace Optional color space for the output. Only sRGB is currently supported;
   *        any non-sRGB value is ignored with a logged warning.
   * @return A new D3D12Window, or nullptr if the swap chain or DirectComposition tree
   *         cannot be created.
   */
  static std::shared_ptr<D3D12Window> MakeForComposition(
      HWND hwnd, std::shared_ptr<D3D12Device> device,
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

#ifdef _WIN32
  // Shared implementation for MakeForHwnd (transparent=false) and MakeForComposition
  // (transparent=true). Kept private so the public API surface exposes only the two
  // self-descriptive entry points that pair with CreateSwapChainForHwnd and
  // CreateSwapChainForComposition, matching the naming convention Windows uses itself.
  static std::shared_ptr<D3D12Window> MakeImpl(HWND hwnd, std::shared_ptr<D3D12Device> device,
                                               std::shared_ptr<ColorSpace> colorSpace,
                                               bool transparent);
#endif

  explicit D3D12Window(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state,
                       std::shared_ptr<ColorSpace> colorSpace);

  std::unique_ptr<PlatformState> _platformState;
};

}  // namespace tgfx
