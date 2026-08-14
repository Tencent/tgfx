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

// Number of backbuffers in the swap chain. Two matches Vulkan's MAX_FRAMES_IN_FLIGHT and keeps
// peak VRAM low on typical 4K windows.
static constexpr UINT BACKBUFFER_COUNT = 2;

// Private RenderTargetProxy that exposes the swap chain's current backbuffer as an external
// D3D12 render target. The proxy is created once when the application calls Surface::MakeFrom()
// and is then reused for every subsequent frame: Surface caches it for its entire lifetime
// rather than re-acquiring on each render. To keep that pattern working with FLIP_DISCARD —
// which rotates between BACKBUFFER_COUNT distinct ID3D12Resources — getRenderTarget() must
// re-query GetCurrentBackBufferIndex every call and invalidate the cached RenderTarget when
// the index changes. Otherwise every frame would be drawn into the same backbuffer slot and
// the other slot would never get updated, manifesting as "no visible change" on user input.
//
// Defined at file scope (not in an anonymous namespace) so D3D12Window::PlatformState can store
// a typed raw pointer to it; the .h does not expose this class, so it remains private to this
// translation unit even without anonymous-namespace internal linkage.
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

// Hidden state shared between D3D12Window and its private RenderTargetProxy. Stored as PImpl so
// the public header doesn't need <dxgi.h> / <d3d12.h>. The DXGI format is kept as `unsigned` to
// match the rest of the D3D12 backend (D3D12Defines.h shadows the SDK enum with constexpr
// integers so an unqualified DXGI_FORMAT_R8G8B8A8_UNORM here is `unsigned`, not the enum type).
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

  // Cached proxy for the currently-acquired backbuffer. Reset by onPresent() so the next
  // onCreateRenderTarget() picks up the new frame's index. Held as a shared_ptr because tgfx's
  // surface code may keep a strong reference for a single frame; currentProxyRaw mirrors the
  // underlying D3D12SwapchainProxy* so onPresent() can call releaseFrame() without a static_cast
  // from the base RenderTargetProxy*. The two pointers are written and cleared together so the
  // raw view never outlives the shared owner.
  std::shared_ptr<RenderTargetProxy> currentProxy;
  D3D12SwapchainProxy* currentProxyRaw = nullptr;

  bool buildBackBuffers();
  // Releases every tgfx-side owner of the backbuffers (proxy, ExternalRenderTargets in the
  // ResourceCache, resources retained by recycled command lists). Must run before ResizeBuffers
  // or swapchain release to avoid DXGI_ERROR_INVALID_CALL / OBJECT_DELETED_WHILE_STILL_IN_USE.
  void drainBackBufferOwners(Context* context, D3D12GPU* gpu);
  // Fully drains the command queue: first waits on tgfx-managed submissions, then signals a
  // fresh fence and waits on it to cover any Present-driven GPU work that was enqueued after
  // the last tgfx frame fence (the frame fence is signaled at submit() time, before Present()
  // hands the GPU-side flip to the queue). Callers that release backbuffers or resize the swap
  // chain must invoke this instead of a bare waitUntilCompleted().
  void drainQueue(D3D12GPU* gpu);
  // Executes the DirectComposition tear-down protocol and releases the DComp COM handles:
  // SetContent(nullptr) → SetRoot(nullptr) → Commit → drop refs. All three protocol steps are
  // required — skipping Commit leaves DWM holding a stale reference to the swap chain and
  // reproduces OBJECT_DELETED_WHILE_STILL_IN_USE once the swap chain is released.
  //
  // Callers must ensure the enclosing GPU queue has already been drained and must still hold
  // the device lock so this tear-down is serialized with any other GPU work. This is a no-op
  // for opaque swap chains (all three DComp fields are null). The failure fallback in
  // ~D3D12Window that runs without a device lock intentionally does NOT call this and skips
  // Commit; see the comment there for the rationale.
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
  // Just clearing backBuffers is not enough: ExternalRenderTargets in the ResourceCache and
  // recycled command lists still hold refs on the old ID3D12Resources, so DXGI rejects
  // ResizeBuffers. Route through drainBackBufferOwners which flushes both. drainQueue is
  // required before that: waitUntilCompleted alone leaves any pending Present-driven flip
  // work on the queue, which can race ResizeBuffers on rapid present-then-resize sequences
  // (see the analogous handling in ~D3D12Window).
  auto* gpu = static_cast<D3D12GPU*>(context->gpu());
  drainQueue(gpu);
  drainBackBufferOwners(context, gpu);
  auto hr =
      swapChain->ResizeBuffers(BACKBUFFER_COUNT, static_cast<UINT>(newWidth),
                               static_cast<UINT>(newHeight), static_cast<DXGI_FORMAT>(format), 0);
  if (FAILED(hr)) {
    LOGE("D3D12Window: ResizeBuffers failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    // Force the next onCreateRenderTarget to re-enter this path; otherwise a same-size WM_SIZE
    // would skip rebuild forever and the window would stay black.
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
  // Step 1: wait for tgfx-managed submissions. Their frame fence is signaled at submit() time,
  // before Present() is invoked, so this alone does not cover the GPU-side flip that DXGI
  // schedules on our command queue when Present() runs.
  gpu->queue()->waitUntilCompleted();

  // Step 2: signal a fresh fence *after* Present's flip work has been enqueued and wait on it,
  // ensuring the queue is truly idle before we release / resize the swapchain buffers.
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
  // DXGI_FORMAT_R8G8B8A8_UNORM in this TU resolves to the D3D12Defines.h `unsigned` constant
  // (= 28) rather than the SDK enum, so cast back here for DXGI_SWAP_CHAIN_DESC1::Format which
  // does want the real enum.
  desc.Format = static_cast<DXGI_FORMAT>(DXGI_FORMAT_R8G8B8A8_UNORM);
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  // Opaque path: FLIP_DISCARD gives DXGI freedom to drop stale backbuffers and is the standard
  // choice for windowed presentation. Transparent path: DirectComposition attaches to the swap
  // chain by resource identity, so we need FLIP_SEQUENTIAL to keep buffer identity stable
  // across frames; ALPHA_PREMULTIPLIED tells DWM how to blend the per-pixel alpha we render.
  desc.SwapEffect = transparent ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.AlphaMode = transparent ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.Flags = 0;

  ComPtr<IDXGISwapChain1> swapChain1;
  if (transparent) {
    // Composition swap chains have no direct HWND binding; they must be attached to a
    // DirectComposition visual tree below (SetContent + SetRoot + Commit) to reach the screen.
    hr = factory->CreateSwapChainForComposition(d3d12CommandQueue, &desc, nullptr, &swapChain1);
  } else {
    // Opaque swap chains render straight to the HWND through the DWM redirection bitmap; no
    // DirectComposition tree is required.
    hr = factory->CreateSwapChainForHwnd(d3d12CommandQueue, hwnd, &desc, nullptr, nullptr,
                                         &swapChain1);
  }
  if (FAILED(hr)) {
    LOGE("D3D12Window: swap chain creation failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    device->unlock();
    return nullptr;
  }
  // Flip-model swap chains require IDXGISwapChain3 for GetCurrentBackBufferIndex.
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
  if (transparent) {
    hr = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(state->compositionDevice.GetAddressOf()));
    if (SUCCEEDED(hr)) {
      // topmost=TRUE places the composition tree above any existing HWND drawing (GDI or
      // other DX content). tgfx-owned windows are expected to be the sole renderer of the
      // target hwnd, so this is the correct default. Callers that need to composite tgfx
      // underneath other per-window drawing must fork this path.
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
      // Unwind the half-initialized DComp tree so the swap chain is no longer referenced by
      // any visual before its COM Release runs at state.reset(). No GPU work has been enqueued
      // through this swap chain yet, so no queue drain is needed here.
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
  // Tear-down ordering is delicate. After the last frame, swap-chain Present() schedules its
  // own GPU work on our command queue (the GPU-side flip), but that work is *not* tracked by
  // any tgfx fence — D3D12CommandQueue::waitUntilCompleted() only waits on submissions we
  // submitted via executeSubmission. If we release the swap chain (or its backbuffers) while
  // that Present work is still pending, the runtime fires
  // OBJECT_DELETED_WHILE_STILL_IN_USE (#921) and the debug layer asserts.
  //
  // drainQueue performs both waits: the tgfx frame fence, followed by a fresh drain fence
  // signaled *after* Present's flip is enqueued.
  //
  // We still have to release the in-tgfx owners of each backbuffer before destroying the swap
  // chain itself:
  //   - the cached ExternalRenderTarget / ExternalTexture pair (drained via ResourceCache and
  //     D3D12GPU return queues)
  //   - recycled command lists in D3D12CommandListPool (each list still pins the resources it
  //     was last recorded against until its next Reset())
  auto context = device->lockContext();
  if (context != nullptr) {
    auto* d3d12GPU = static_cast<D3D12GPU*>(context->gpu());

    // 1. Drain both tgfx submissions and any Present-driven flip work still on the queue.
    _platformState->drainQueue(d3d12GPU);

    // 2. Drop tgfx-side owners of the backbuffers.
    _platformState->drainBackBufferOwners(context, d3d12GPU);

    // 3. Detach the swap chain from DirectComposition before releasing it, so DWM no longer
    //    references it.
    _platformState->detachCompositionTree();

    // 4. Release the swap chain while still holding the device lock so its final COM
    //    Release does not race concurrent D3D12 work on another thread.
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
    // rebuild() drains the queue (both tgfx and Present-driven work) before ResizeBuffers,
    // so callers do not need a separate waitUntilCompleted() here.
    if (!_platformState->rebuild(context, width, height)) {
      return nullptr;
    }
  }

  // Build one proxy per Surface and let it pull the current backbuffer index out of the swap
  // chain on every getRenderTarget() call. Surface caches the proxy for its whole lifetime, so
  // a per-frame allocation here would leak the freshly-created proxy and never reach the
  // backbuffer-rotation code path.
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
  // SyncInterval=1 mirrors VK_PRESENT_MODE_FIFO_KHR: wait for the next vertical blank. Apps that
  // need uncapped framerate can replace this with a FRAME_LATENCY_WAITABLE_OBJECT path later.
  auto hr = _platformState->swapChain->Present(1, 0);
  if (FAILED(hr)) {
    LOGE("D3D12Window: Present failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
  }
  // Tell the proxy to drop its cached RenderTarget so the next getRenderTarget() picks up the
  // backbuffer the swap chain just rotated in. Without this Surface keeps drawing into the
  // same slot forever and the user sees a frozen frame regardless of input.
  if (_platformState->currentProxyRaw != nullptr) {
    _platformState->currentProxyRaw->releaseFrame();
  }
}

}  // namespace tgfx
