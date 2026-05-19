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

#include "tgfx/gpu/d3d12/D3D12Window.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <vector>
#include "D3D12CommandQueue.h"
#include "D3D12Defines.h"
#include "D3D12GPU.h"
#include "core/utils/Log.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/d3d12/D3D12Types.h"

namespace tgfx {

// Number of backbuffers in the swap chain. Two is the minimum allowed by FLIP_DISCARD; using three
// gives the OS one extra frame to compose, smoothing latency spikes under heavy GPU load. We pick
// two for parity with Vulkan's MAX_FRAMES_IN_FLIGHT and to keep peak VRAM low for typical 4K
// windows. The presentation engine still queues a small number of frames internally.
static constexpr UINT BACKBUFFER_COUNT = 2;

// Hidden state shared between D3D12Window and its private RenderTargetProxy. Stored as PImpl so
// the public header doesn't need <dxgi.h> / <d3d12.h>. The DXGI format is kept as `unsigned` to
// match the rest of the D3D12 backend (D3D12Defines.h shadows the SDK enum with constexpr
// integers so an unqualified DXGI_FORMAT_R8G8B8A8_UNORM here is `unsigned`, not the enum type).
struct D3D12Window::PlatformState {
  ComPtr<IDXGISwapChain3> swapChain;
  std::vector<ComPtr<ID3D12Resource>> backBuffers;
  unsigned format = DXGI_FORMAT_R8G8B8A8_UNORM;
  HWND hwnd = nullptr;
  int width = 0;
  int height = 0;

  // Cached proxy for the currently-acquired backbuffer. Reset by onPresent() so the next
  // onCreateRenderTarget() picks up the new frame's index. Held as a shared_ptr because tgfx's
  // surface code may keep a strong reference for a single frame.
  std::shared_ptr<RenderTargetProxy> currentProxy;

  bool buildBackBuffers();
  bool rebuild(int newWidth, int newHeight);
};

bool D3D12Window::PlatformState::buildBackBuffers() {
  backBuffers.clear();
  backBuffers.resize(BACKBUFFER_COUNT);
  for (UINT i = 0; i < BACKBUFFER_COUNT; i++) {
    auto hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
    if (FAILED(hr)) {
      LOGE("D3D12Window: GetBuffer(%u) failed, HRESULT=0x%08X", i, static_cast<unsigned>(hr));
      backBuffers.clear();
      return false;
    }
  }
  return true;
}

bool D3D12Window::PlatformState::rebuild(int newWidth, int newHeight) {
  // Releasing every backbuffer reference is mandatory before ResizeBuffers; otherwise the call
  // returns DXGI_ERROR_INVALID_CALL because the swapchain still owns outstanding references.
  backBuffers.clear();
  currentProxy = nullptr;
  auto hr =
      swapChain->ResizeBuffers(BACKBUFFER_COUNT, static_cast<UINT>(newWidth),
                               static_cast<UINT>(newHeight), static_cast<DXGI_FORMAT>(format), 0);
  if (FAILED(hr)) {
    LOGE("D3D12Window: ResizeBuffers failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    return false;
  }
  width = newWidth;
  height = newHeight;
  return buildBackBuffers();
}

namespace {

// Private RenderTargetProxy that exposes the swap chain's current backbuffer as an external
// D3D12 render target. The proxy is created once when the application calls Surface::MakeFrom()
// and is then reused for every subsequent frame: Surface caches it for its entire lifetime
// rather than re-acquiring on each render. To keep that pattern working with FLIP_DISCARD —
// which rotates between BACKBUFFER_COUNT distinct ID3D12Resources — getRenderTarget() must
// re-query GetCurrentBackBufferIndex every call and invalidate the cached RenderTarget when
// the index changes. Otherwise every frame would be drawn into the same backbuffer slot and
// the other slot would never get updated, manifesting as "no visible change" on user input.
class D3D12SwapchainProxy : public RenderTargetProxy {
 public:
  D3D12SwapchainProxy(Context* context, IDXGISwapChain3* swapChain,
                      const std::vector<ComPtr<ID3D12Resource>>* backBuffers, unsigned format,
                      int width, int height)
      : _context(context), _swapChain(swapChain), _backBuffers(backBuffers), _format(format),
        _width(width), _height(height) {
  }

  Context* getContext() const override {
    return _context;
  }
  int width() const override {
    return _width;
  }
  int height() const override {
    return _height;
  }
  PixelFormat format() const override {
    return DXGIFormatToPixelFormat(_format);
  }
  int sampleCount() const override {
    return 1;
  }
  ImageOrigin origin() const override {
    return ImageOrigin::TopLeft;
  }
  bool externallyOwned() const override {
    return true;
  }
  std::shared_ptr<TextureView> getTextureView() const override {
    return nullptr;
  }

  std::shared_ptr<RenderTarget> getRenderTarget() const override {
    if (_swapChain == nullptr || _backBuffers == nullptr || _backBuffers->empty()) {
      return nullptr;
    }
    UINT index = _swapChain->GetCurrentBackBufferIndex();
    if (index >= _backBuffers->size()) {
      return nullptr;
    }
    auto* currentBuffer = (*_backBuffers)[index].Get();
    if (_renderTarget != nullptr && currentBuffer == _cachedBackBuffer) {
      return _renderTarget;
    }
    D3D12TextureInfo info = {};
    info.resource = currentBuffer;
    info.format = _format;
    BackendRenderTarget backendRT(info, _width, _height);
    _renderTarget = RenderTarget::MakeFrom(_context, backendRT, ImageOrigin::TopLeft);
    _cachedBackBuffer = currentBuffer;
    return _renderTarget;
  }

  /// Drops the cached RenderTarget so the next getRenderTarget() call goes through MakeFrom
  /// again. Invoked by D3D12Window::onPresent — after Present() the swap chain promotes a new
  /// backbuffer to "current", so the next acquisition must wrap that buffer instead of the one
  /// the GPU just submitted to.
  void releaseFrame() {
    _renderTarget = nullptr;
    _cachedBackBuffer = nullptr;
  }

 private:
  Context* _context = nullptr;
  IDXGISwapChain3* _swapChain = nullptr;
  const std::vector<ComPtr<ID3D12Resource>>* _backBuffers = nullptr;
  unsigned _format = DXGI_FORMAT_R8G8B8A8_UNORM;
  int _width = 0;
  int _height = 0;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
  mutable ID3D12Resource* _cachedBackBuffer = nullptr;
};

}  // namespace

#ifdef _WIN32

std::shared_ptr<D3D12Window> D3D12Window::MakeFrom(HWND hwnd, std::shared_ptr<D3D12Device> device,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (hwnd == nullptr || device == nullptr) {
    return nullptr;
  }
  if (colorSpace && !colorSpace->isSRGB()) {
    LOGI(
        "D3D12Window::MakeFrom(): non-sRGB colorSpace is not yet supported and will be ignored. "
        "Only sRGB output is currently available.");
  }

  auto context = device->lockContext();
  if (context == nullptr) {
    return nullptr;
  }
  auto* gpu = static_cast<D3D12GPU*>(context->gpu());
  auto* d3d12CommandQueue = static_cast<D3D12CommandQueue*>(gpu->queue())->d3d12CommandQueue();

  RECT clientRect = {};
  GetClientRect(hwnd, &clientRect);
  int width = static_cast<int>(clientRect.right - clientRect.left);
  int height = static_cast<int>(clientRect.bottom - clientRect.top);
  if (width <= 0 || height <= 0) {
    width = std::max(width, 1);
    height = std::max(height, 1);
  }

  ComPtr<IDXGIFactory4> factory;
  auto hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    LOGE("D3D12Window: CreateDXGIFactory1 failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    device->unlock();
    return nullptr;
  }

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.BufferCount = BACKBUFFER_COUNT;
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  // DXGI_FORMAT_R8G8B8A8_UNORM in this TU resolves to the D3D12Defines.h `unsigned` constant
  // (= 28) rather than the SDK enum, so cast back here for DXGI_SWAP_CHAIN_DESC1::Format which
  // does want the real enum.
  desc.Format = static_cast<DXGI_FORMAT>(DXGI_FORMAT_R8G8B8A8_UNORM);
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.Flags = 0;

  ComPtr<IDXGISwapChain1> swapChain1;
  hr = factory->CreateSwapChainForHwnd(d3d12CommandQueue, hwnd, &desc, nullptr, nullptr,
                                       &swapChain1);
  if (FAILED(hr)) {
    LOGE("D3D12Window: CreateSwapChainForHwnd failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    device->unlock();
    return nullptr;
  }
  // FLIP_DISCARD requires IDXGISwapChain3 for GetCurrentBackBufferIndex; QI is mandatory here.
  ComPtr<IDXGISwapChain3> swapChain3;
  hr = swapChain1.As(&swapChain3);
  if (FAILED(hr) || swapChain3 == nullptr) {
    LOGE("D3D12Window: failed to QI IDXGISwapChain3, HRESULT=0x%08X", static_cast<unsigned>(hr));
    device->unlock();
    return nullptr;
  }
  // Disable DXGI's default Alt+Enter fullscreen handling. tgfx callers manage that themselves.
  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  auto state = std::make_unique<PlatformState>();
  state->swapChain = std::move(swapChain3);
  state->format = desc.Format;
  state->hwnd = hwnd;
  state->width = width;
  state->height = height;
  if (!state->buildBackBuffers()) {
    device->unlock();
    return nullptr;
  }

  device->unlock();
  return std::shared_ptr<D3D12Window>(new D3D12Window(device, std::move(state), colorSpace));
}

#endif

D3D12Window::D3D12Window(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)), _platformState(std::move(state)) {
}

D3D12Window::~D3D12Window() {
  // Tear-down ordering is delicate. After the last frame, swap-chain Present() schedules its
  // own GPU work on our command queue (the GPU-side flip), but that work is *not* tracked by
  // any tgfx fence — D3D12CommandQueue::waitUntilCompleted() only waits on submissions we
  // submitted via executeSubmission. If we release the swap chain (or its backbuffers) while
  // that Present work is still pending, the runtime fires
  // OBJECT_DELETED_WHILE_STILL_IN_USE (#921) and the debug layer asserts.
  //
  // To make sure the queue really is idle, we Signal a fresh fence on the queue and wait for
  // it: that flushes everything previously enqueued, Present included.
  //
  // Then we still have to release the in-tgfx owners of each backbuffer before destroying
  // the swap chain itself:
  //   - the cached ExternalRenderTarget / ExternalTexture pair (drained via ResourceCache and
  //     D3D12GPU return queues)
  //   - recycled command lists in D3D12CommandListPool (each list still pins the resources it
  //     was last recorded against until its next Reset())
  auto context = device->lockContext();
  if (context != nullptr) {
    auto* d3d12GPU = static_cast<D3D12GPU*>(context->gpu());
    auto* d3d12CmdQueue = static_cast<D3D12CommandQueue*>(d3d12GPU->queue())->d3d12CommandQueue();

    // 1. Wait for all tgfx-managed submissions to complete.
    d3d12GPU->queue()->waitUntilCompleted();

    // 2. Wait for any Present-driven work the queue still has queued up. Without this the
    //    swap-chain release path below trips OBJECT_DELETED_WHILE_STILL_IN_USE because DXGI's
    //    internal flip operation is still in flight on the queue.
    ComPtr<ID3D12Fence> drainFence;
    if (SUCCEEDED(
            d3d12GPU->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&drainFence)))) {
      const UINT64 targetValue = 1;
      if (SUCCEEDED(d3d12CmdQueue->Signal(drainFence.Get(), targetValue))) {
        if (drainFence->GetCompletedValue() < targetValue) {
          HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
          if (evt != nullptr) {
            if (SUCCEEDED(drainFence->SetEventOnCompletion(targetValue, evt))) {
              WaitForSingleObject(evt, 5000);
            }
            CloseHandle(evt);
          }
        }
      }
    }

    // 3. Drop tgfx-side owners of the backbuffers.
    _platformState->currentProxy = nullptr;
    context->purgeResourcesNotUsedSince(std::chrono::steady_clock::now());
    d3d12GPU->processUnreferencedResources();
    d3d12GPU->commandListPool().clear();

    // 4. Release the swap chain and our own backbuffer ComPtrs. The order between these two
    //    is not important once the queue is idle and no tgfx object pins the backbuffers.
    _platformState->backBuffers.clear();
    _platformState->swapChain = nullptr;
    device->unlock();
  } else {
    _platformState->currentProxy = nullptr;
    _platformState->backBuffers.clear();
    _platformState->swapChain = nullptr;
  }
}

std::shared_ptr<RenderTargetProxy> D3D12Window::onCreateRenderTarget(Context* context) {
  if (_platformState->swapChain == nullptr) {
    return nullptr;
  }
  // Detect resize. The application's WM_SIZE handler is expected to reset the cached Surface,
  // which in turn drops references to our previous proxy/backbuffer; only then is it safe to
  // call ResizeBuffers (which requires zero outstanding backbuffer references).
  RECT rect = {};
  GetClientRect(_platformState->hwnd, &rect);
  int width = static_cast<int>(rect.right - rect.left);
  int height = static_cast<int>(rect.bottom - rect.top);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (width != _platformState->width || height != _platformState->height) {
    // Wait for the GPU to finish reading old backbuffers; ResizeBuffers cannot proceed while
    // any reference is outstanding, including in-flight command lists.
    context->gpu()->queue()->waitUntilCompleted();
    if (!_platformState->rebuild(width, height)) {
      return nullptr;
    }
  }

  // Build one proxy per Surface and let it pull the current backbuffer index out of the swap
  // chain on every getRenderTarget() call. Surface caches the proxy for its whole lifetime, so
  // a per-frame allocation here would leak the freshly-created proxy and never reach the
  // backbuffer-rotation code path.
  _platformState->currentProxy = std::make_shared<D3D12SwapchainProxy>(
      context, _platformState->swapChain.Get(), &_platformState->backBuffers,
      _platformState->format, _platformState->width, _platformState->height);
  return _platformState->currentProxy;
}

void D3D12Window::onPresent(Context* /*context*/) {
  if (_platformState->swapChain == nullptr) {
    return;
  }
  // SyncInterval=1 mirrors VK_PRESENT_MODE_FIFO_KHR: wait for the next vertical blank. Apps that
  // need uncapped framerate can replace this with a FRAME_LATENCY_WAITABLE_OBJECT path later.
  auto hr = _platformState->swapChain->Present(1, 0);
  if (FAILED(hr)) {
    LOGE("D3D12Window: Present failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
  }
  // Tell the proxy to drop its cached RenderTarget so the next getRenderTarget() picks up the
  // backbuffer the swap chain just rotated in. Without this Surface keeps drawing into the
  // same slot forever and the user sees a frozen frame regardless of input.
  if (auto* proxy = static_cast<D3D12SwapchainProxy*>(_platformState->currentProxy.get())) {
    proxy->releaseFrame();
  }
}

}  // namespace tgfx
