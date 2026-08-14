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
#ifdef _WIN32
#include <dcomp.h>
#include <windows.h>
#endif
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

// Two matches Vulkan's MAX_FRAMES_IN_FLIGHT and keeps peak VRAM low on typical 4K windows.
static constexpr UINT BACKBUFFER_COUNT = 2;

// Exposes the swap chain's current backbuffer as a RenderTarget. Surface caches the proxy for
// its lifetime, so getRenderTarget() must re-query GetCurrentBackBufferIndex every call and
// invalidate the cached RenderTarget when the index changes; otherwise flip-model rotation
// would leave every other slot untouched.
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

  /// Drops the cached RenderTarget so the next getRenderTarget() wraps the next flipped
  /// backbuffer. Called by D3D12Window::onPresent.
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

// PImpl so the public header pulls in neither <dxgi.h> nor <d3d12.h>. `format` is kept as
// `unsigned` because D3D12Defines.h shadows the SDK enum with constexpr integers in this TU.
struct D3D12Window::PlatformState {
  ComPtr<IDXGISwapChain3> swapChain;
  ComPtr<IDCompositionDevice> compositionDevice;
  ComPtr<IDCompositionTarget> compositionTarget;
  ComPtr<IDCompositionVisual> compositionVisual;
  std::vector<ComPtr<ID3D12Resource>> backBuffers;
  unsigned format = DXGI_FORMAT_R8G8B8A8_UNORM;
  HWND hwnd = nullptr;
  int width = 0;
  int height = 0;

  // currentProxyRaw mirrors the currentProxy owner so onPresent can call releaseFrame without
  // a static_cast from the base RenderTargetProxy. The two are written and cleared together.
  std::shared_ptr<RenderTargetProxy> currentProxy;
  D3D12SwapchainProxy* currentProxyRaw = nullptr;

  bool buildBackBuffers();
  // Releases every tgfx-side owner of the backbuffers (proxy, ExternalRenderTargets in the
  // ResourceCache, recycled command lists). Required before ResizeBuffers or swapchain release
  // to avoid DXGI_ERROR_INVALID_CALL / OBJECT_DELETED_WHILE_STILL_IN_USE.
  void drainBackBufferOwners(Context* context, D3D12GPU* gpu);
  // Waits for tgfx-managed submissions, then signals a fresh fence to cover the GPU-side flip
  // that Present enqueues after the tgfx frame fence. Callers releasing or resizing the swap
  // chain must invoke this instead of a bare waitUntilCompleted.
  void drainQueue(D3D12GPU* gpu);
  // Runs the DirectComposition tear-down protocol (SetContent(nullptr) → SetRoot(nullptr) →
  // Commit) and drops the DComp COM refs. All three steps are required — skipping Commit
  // leaves DWM holding a stale reference and reproduces OBJECT_DELETED_WHILE_STILL_IN_USE.
  // Caller must have drained the GPU queue and still hold the device lock. No-op for opaque
  // swap chains. Not called from the unlocked fallback in ~D3D12Window, which cannot drain.
  void detachCompositionTree();
  bool rebuild(Context* context, int newWidth, int newHeight);
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

bool D3D12Window::PlatformState::rebuild(Context* context, int newWidth, int newHeight) {
  auto* gpu = static_cast<D3D12GPU*>(context->gpu());
  drainQueue(gpu);
  drainBackBufferOwners(context, gpu);
  auto hr =
      swapChain->ResizeBuffers(BACKBUFFER_COUNT, static_cast<UINT>(newWidth),
                               static_cast<UINT>(newHeight), static_cast<DXGI_FORMAT>(format), 0);
  if (FAILED(hr)) {
    LOGE("D3D12Window: ResizeBuffers failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    // Force re-entry on the next onCreateRenderTarget so a same-size WM_SIZE does not skip
    // rebuild forever and leave the window black.
    width = 0;
    height = 0;
    return false;
  }
  width = newWidth;
  height = newHeight;
  return buildBackBuffers();
}

void D3D12Window::PlatformState::drainBackBufferOwners(Context* context, D3D12GPU* gpu) {
  currentProxy = nullptr;
  currentProxyRaw = nullptr;
  backBuffers.clear();
  context->purgeResourcesNotUsedSince(std::chrono::steady_clock::now());
  gpu->processUnreferencedResources();
  gpu->commandListPool().clear();
}

void D3D12Window::PlatformState::drainQueue(D3D12GPU* gpu) {
  gpu->queue()->waitUntilCompleted();
  // The tgfx frame fence is signaled at submit(), before Present() enqueues its GPU-side flip
  // work. A second fence signaled after this point catches that work.
  auto* d3d12CmdQueue = static_cast<D3D12CommandQueue*>(gpu->queue())->d3d12CommandQueue();
  ComPtr<ID3D12Fence> drainFence;
  if (FAILED(gpu->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&drainFence)))) {
    return;
  }
  const UINT64 targetValue = 1;
  if (FAILED(d3d12CmdQueue->Signal(drainFence.Get(), targetValue))) {
    return;
  }
  if (drainFence->GetCompletedValue() >= targetValue) {
    return;
  }
  HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (evt == nullptr) {
    return;
  }
  if (SUCCEEDED(drainFence->SetEventOnCompletion(targetValue, evt))) {
    WaitForSingleObject(evt, 5000);
  }
  CloseHandle(evt);
}

void D3D12Window::PlatformState::detachCompositionTree() {
  if (compositionVisual != nullptr) {
    compositionVisual->SetContent(nullptr);
  }
  if (compositionTarget != nullptr) {
    compositionTarget->SetRoot(nullptr);
  }
  if (compositionDevice != nullptr) {
    compositionDevice->Commit();
  }
  compositionVisual = nullptr;
  compositionTarget = nullptr;
  compositionDevice = nullptr;
}

#ifdef _WIN32

std::shared_ptr<D3D12Window> D3D12Window::MakeForHwnd(HWND hwnd,
                                                      std::shared_ptr<D3D12Device> device,
                                                      std::shared_ptr<ColorSpace> colorSpace) {
  return MakeImpl(hwnd, std::move(device), std::move(colorSpace), false);
}

std::shared_ptr<D3D12Window> D3D12Window::MakeForComposition(
    HWND hwnd, std::shared_ptr<D3D12Device> device, std::shared_ptr<ColorSpace> colorSpace) {
  return MakeImpl(hwnd, std::move(device), std::move(colorSpace), true);
}

std::shared_ptr<D3D12Window> D3D12Window::MakeImpl(HWND hwnd, std::shared_ptr<D3D12Device> device,
                                                   std::shared_ptr<ColorSpace> colorSpace,
                                                   bool transparent) {
  if (hwnd == nullptr || device == nullptr) {
    return nullptr;
  }
  if (colorSpace && !colorSpace->isSRGB()) {
    LOGI("D3D12Window: non-sRGB colorSpace is not yet supported and will be ignored. Only sRGB "
         "output is currently available.");
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
  // D3D12Defines.h shadows DXGI_FORMAT_R8G8B8A8_UNORM with an `unsigned` constant; the DXGI
  // struct field needs the real enum.
  desc.Format = static_cast<DXGI_FORMAT>(DXGI_FORMAT_R8G8B8A8_UNORM);
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  // Transparent path needs FLIP_SEQUENTIAL because DirectComposition attaches to the swap
  // chain by resource identity and FLIP_DISCARD would let DXGI swap buffers under DComp.
  desc.SwapEffect = transparent ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.AlphaMode = transparent ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.Flags = 0;

  ComPtr<IDXGISwapChain1> swapChain1;
  if (transparent) {
    hr = factory->CreateSwapChainForComposition(d3d12CommandQueue, &desc, nullptr, &swapChain1);
  } else {
    hr = factory->CreateSwapChainForHwnd(d3d12CommandQueue, hwnd, &desc, nullptr, nullptr,
                                         &swapChain1);
  }
  if (FAILED(hr)) {
    LOGE("D3D12Window: swap chain creation failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    device->unlock();
    return nullptr;
  }
  ComPtr<IDXGISwapChain3> swapChain3;
  hr = swapChain1.As(&swapChain3);
  if (FAILED(hr) || swapChain3 == nullptr) {
    LOGE("D3D12Window: failed to QI IDXGISwapChain3, HRESULT=0x%08X", static_cast<unsigned>(hr));
    device->unlock();
    return nullptr;
  }
  // Disable DXGI's default Alt+Enter fullscreen handling; tgfx callers manage that themselves.
  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  auto state = std::make_unique<PlatformState>();
  state->swapChain = std::move(swapChain3);
  state->format = desc.Format;
  state->hwnd = hwnd;
  state->width = width;
  state->height = height;
  if (transparent) {
    hr = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(state->compositionDevice.GetAddressOf()));
    if (SUCCEEDED(hr)) {
      // topmost=TRUE places the tree above any existing HWND drawing. tgfx owns the hwnd, so
      // this is the correct default.
      hr = state->compositionDevice->CreateTargetForHwnd(hwnd, TRUE,
                                                         state->compositionTarget.GetAddressOf());
    }
    if (SUCCEEDED(hr)) {
      hr = state->compositionDevice->CreateVisual(state->compositionVisual.GetAddressOf());
    }
    if (SUCCEEDED(hr)) {
      hr = state->compositionVisual->SetContent(state->swapChain.Get());
    }
    if (SUCCEEDED(hr)) {
      hr = state->compositionTarget->SetRoot(state->compositionVisual.Get());
    }
    if (SUCCEEDED(hr)) {
      hr = state->compositionDevice->Commit();
    }
    if (FAILED(hr)) {
      LOGE("D3D12Window: DirectComposition setup failed, HRESULT=0x%08X",
           static_cast<unsigned>(hr));
      state->detachCompositionTree();
      state.reset();
      device->unlock();
      return nullptr;
    }
  }
  if (!state->buildBackBuffers()) {
    state.reset();
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
  // Present enqueues its own GPU-side flip work on our command queue after the tgfx frame
  // fence is signaled. Releasing the swap chain or its backbuffers while that work is still
  // pending trips OBJECT_DELETED_WHILE_STILL_IN_USE (#921). drainQueue covers both waits;
  // drainBackBufferOwners then drops the cached ExternalRenderTarget and recycled command
  // lists that still pin each backbuffer resource.
  auto context = device->lockContext();
  if (context != nullptr) {
    auto* d3d12GPU = static_cast<D3D12GPU*>(context->gpu());
    _platformState->drainQueue(d3d12GPU);
    _platformState->drainBackBufferOwners(context, d3d12GPU);
    _platformState->detachCompositionTree();
    // Release the swap chain while still holding the device lock so its final COM Release
    // does not race concurrent D3D12 work on another thread.
    _platformState->swapChain = nullptr;
    device->unlock();
  } else {
    // The device context is unavailable, so GPU work cannot be drained. Release the remaining
    // COM references without submitting another composition update.
    _platformState->currentProxy = nullptr;
    _platformState->currentProxyRaw = nullptr;
    _platformState->backBuffers.clear();
    _platformState->compositionVisual = nullptr;
    _platformState->compositionTarget = nullptr;
    _platformState->compositionDevice = nullptr;
    _platformState->swapChain = nullptr;
  }
}

std::shared_ptr<RenderTargetProxy> D3D12Window::onCreateRenderTarget(Context* context) {
  if (_platformState->swapChain == nullptr) {
    return nullptr;
  }
  RECT rect = {};
  GetClientRect(_platformState->hwnd, &rect);
  int width = static_cast<int>(rect.right - rect.left);
  int height = static_cast<int>(rect.bottom - rect.top);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (width != _platformState->width || height != _platformState->height) {
    if (!_platformState->rebuild(context, width, height)) {
      return nullptr;
    }
  }
  // One proxy per Surface; it re-queries the current backbuffer on every getRenderTarget()
  // call to follow flip-model rotation. Allocating per frame would leak and skip rotation.
  auto proxy = std::make_shared<D3D12SwapchainProxy>(
      context, _platformState->swapChain.Get(), &_platformState->backBuffers,
      _platformState->format, _platformState->width, _platformState->height);
  _platformState->currentProxyRaw = proxy.get();
  _platformState->currentProxy = std::move(proxy);
  return _platformState->currentProxy;
}

void D3D12Window::onPresent(Context* /*context*/) {
  if (_platformState->swapChain == nullptr) {
    return;
  }
  // SyncInterval=1 mirrors VK_PRESENT_MODE_FIFO_KHR (wait for vblank).
  auto hr = _platformState->swapChain->Present(1, 0);
  if (FAILED(hr)) {
    LOGE("D3D12Window: Present failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
  }
  if (_platformState->currentProxyRaw != nullptr) {
    _platformState->currentProxyRaw->releaseFrame();
  }
}

}  // namespace tgfx
