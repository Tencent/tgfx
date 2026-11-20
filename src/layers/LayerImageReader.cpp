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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/layers/LayerImageReader.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
#include "core/utils/CopyPixels.h"
#include "core/utils/Log.h"
#include "gpu/AlignTo.h"
#include "layers/LayerDrawSession.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/GPUBuffer.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

namespace {

// Bound temporary planning storage while still sharing captures across neighboring regions.
static constexpr size_t MaxRegionsPerBatch = 64;

// Bound the staging memory independently of the shared background capture. One large tile can
// still be read on its own when the caller requests tiles that exceed this budget.
static constexpr uint64_t MaxPendingReadbackBytes = 32 * 1024 * 1024;

struct PendingReadback {
  std::shared_ptr<SurfaceReadback> readback;
  ImageInfo destinationInfo;
  void* destinationPixels = nullptr;
  bool flipY = false;
};

static DrawOutcome ReadPendingPixels(Context* context, const std::vector<PendingReadback>& pending,
                                     DrawControl& control) {
  if (control.cancelled()) {
    return DrawOutcome::Cancelled;
  }
  // Wait once for all the transfers, then map the buffers directly. Calling lockPixels() for
  // each readback would wait on the whole queue again.
  context->flushAndSubmit(true);
  for (const auto& entry : pending) {
    if (control.cancelled()) {
      return DrawOutcome::Cancelled;
    }
    auto buffer = entry.readback->getGPUBuffer(context);
    if (buffer == nullptr) {
      control.fail(LayerImageErrorCode::ReadbackFailed);
      return DrawOutcome::Failed;
    }
    // WebGPU also needs a map request after the transfers have been submitted.
    buffer->requestMapAsync();
    auto* pixels = buffer->isReady() ? buffer->map() : nullptr;
    if (pixels == nullptr) {
      buffer->unmap();
      control.fail(LayerImageErrorCode::ReadbackFailed);
      return DrawOutcome::Failed;
    }
    CopyPixels(entry.readback->info(), pixels, entry.destinationInfo, entry.destinationPixels,
               entry.flipY);
    buffer->unmap();
  }
  return DrawOutcome::Success;
}

static bool IsFiniteColor(const Color& color) {
  return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue) &&
         std::isfinite(color.alpha);
}

static bool IsSupportedColorType(ColorType colorType) {
  return colorType == ColorType::RGBA_8888 || colorType == ColorType::BGRA_8888;
}

static bool IsSupportedAlphaType(AlphaType alphaType) {
  return alphaType == AlphaType::Premultiplied || alphaType == AlphaType::Unpremultiplied;
}

static bool IsSupportedBlendMode(BlendMode blendMode) {
  switch (blendMode) {
    case BlendMode::Clear:
    case BlendMode::Src:
    case BlendMode::Dst:
    case BlendMode::SrcOver:
    case BlendMode::DstOver:
    case BlendMode::SrcIn:
    case BlendMode::DstIn:
    case BlendMode::SrcOut:
    case BlendMode::DstOut:
    case BlendMode::SrcATop:
    case BlendMode::DstATop:
    case BlendMode::Xor:
    case BlendMode::PlusLighter:
    case BlendMode::Modulate:
    case BlendMode::Screen:
    case BlendMode::Overlay:
    case BlendMode::Darken:
    case BlendMode::Lighten:
    case BlendMode::ColorDodge:
    case BlendMode::ColorBurn:
    case BlendMode::HardLight:
    case BlendMode::SoftLight:
    case BlendMode::Difference:
    case BlendMode::Exclusion:
    case BlendMode::Multiply:
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
    case BlendMode::PlusDarker:
      return true;
  }
  return false;
}

static std::unique_ptr<LayerImageReader> FailMake(LayerImageErrorCode* error,
                                                  LayerImageErrorCode code) {
  if (error != nullptr) {
    *error = code;
  }
  return nullptr;
}

}  // namespace

class LayerImageReader::Impl {
 public:
  std::unique_ptr<LayerDrawSession> session = nullptr;
  LayerImageRequest request = {};
  LayerImageReadOptions options = {};
  ImageInfo bandInfo = {};
  uint32_t deviceID = 0;
  std::atomic<bool> cancelRequested = {false};
  int rowsRead = 0;
  bool done = false;
  bool cancelled = false;
  LayerImageErrorCode error = LayerImageErrorCode::None;

  LayerImageReadResult fail(LayerImageErrorCode code) {
    error = code;
    LayerImageReadResult result = {};
    result.code = LayerImageReadCode::Failed;
    result.firstRow = rowsRead;
    result.error = error;
    return result;
  }

  LayerImageReadResult cancel() {
    cancelled = true;
    LayerImageReadResult result = {};
    result.code = LayerImageReadCode::Cancelled;
    result.firstRow = rowsRead;
    return result;
  }

  bool validateDestination(const Pixmap& destination, size_t destinationByteSize,
                           int requiredRows) const;
  DrawOutcome drawRegionList(Context* context, const std::vector<LayerRegionPlan>& regions,
                             const LayerDrawLimits& limits, int bandFirstRow,
                             const Pixmap& destination, DrawControl& control);
  LayerImageReadResult readNextBand(Context* context, const Pixmap& destination,
                                    size_t destinationByteSize);
};

bool LayerImageReader::Impl::validateDestination(const Pixmap& destination,
                                                 size_t destinationByteSize,
                                                 int requiredRows) const {
  auto& info = destination.info();
  if (destination.writablePixels() == nullptr || destinationByteSize == 0) {
    return false;
  }
  auto colorSpace = info.colorSpace() != nullptr ? info.colorSpace() : ColorSpace::SRGB();
  if (info.width() != bandInfo.width() || info.colorType() != bandInfo.colorType() ||
      info.alphaType() != bandInfo.alphaType() ||
      !ColorSpace::Equals(colorSpace.get(), bandInfo.colorSpace().get())) {
    return false;
  }
  if (requiredRows <= 0 || info.height() < requiredRows) {
    return false;
  }
  auto bytesPerPixel = bandInfo.bytesPerPixel();
  auto minRowBytes = static_cast<size_t>(bandInfo.width()) * bytesPerPixel;
  if (info.rowBytes() < minRowBytes) {
    return false;
  }
  auto maxSize = std::numeric_limits<size_t>::max();
  auto rows = static_cast<size_t>(requiredRows - 1);
  if (rows > 0 && info.rowBytes() > (maxSize - minRowBytes) / rows) {
    return false;
  }
  auto bytes = rows * info.rowBytes() + minRowBytes;
  return bytes <= destinationByteSize;
}

DrawOutcome LayerImageReader::Impl::drawRegionList(Context* context,
                                                   const std::vector<LayerRegionPlan>& regions,
                                                   const LayerDrawLimits& limits, int bandFirstRow,
                                                   const Pixmap& destination,
                                                   DrawControl& control) {
  auto batchPlan = session->planBatch(regions, limits, control);
  if (batchPlan.code == BatchPlanCode::RequiresSplit) {
    using Difference = std::vector<LayerRegionPlan>::difference_type;
    auto mid = static_cast<Difference>(regions.size() / 2);
    std::vector<LayerRegionPlan> left(regions.begin(), regions.begin() + mid);
    std::vector<LayerRegionPlan> right(regions.begin() + mid, regions.end());
    auto first = drawRegionList(context, left, limits, bandFirstRow, destination, control);
    if (first != DrawOutcome::Success) {
      return first;
    }
    return drawRegionList(context, right, limits, bandFirstRow, destination, control);
  }
  if (batchPlan.code == BatchPlanCode::Cancelled) {
    return DrawOutcome::Cancelled;
  }
  if (batchPlan.code == BatchPlanCode::Failed) {
    return DrawOutcome::Failed;
  }
  auto batch = session->prepareBatch(context, batchPlan.captureRect, control);
  if (!batch) {
    return DrawOutcome::Failed;
  }
  auto bytesPerPixel = bandInfo.bytesPerPixel();
  const auto readbackBytesPerPixel = ImageInfo::GetBytesPerPixel(ColorType::RGBA_8888);
  auto* basePixels = static_cast<uint8_t*>(destination.writablePixels());
  const auto alignment = static_cast<size_t>(limits.gpuLimits.minBufferCopyRowAlignment);
  std::vector<PendingReadback> pending = {};
  pending.reserve(regions.size());
  uint64_t pendingBytes = 0;
  for (const auto& region : regions) {
    const int width = region.coreRect.width();
    const int height = region.coreRect.height();
    const auto readbackRowBytes =
        AlignTo(static_cast<size_t>(width) * readbackBytesPerPixel, alignment);
    const auto readbackBytes =
        static_cast<uint64_t>(readbackRowBytes) * static_cast<uint64_t>(height);
    if (!pending.empty() && pendingBytes + readbackBytes > MaxPendingReadbackBytes) {
      auto outcome = ReadPendingPixels(context, pending, control);
      if (outcome != DrawOutcome::Success) {
        return outcome;
      }
      pending.clear();
      pendingBytes = 0;
    }
    auto surface = Surface::Make(context, region.renderRect.width(), region.renderRect.height(),
                                 ColorType::RGBA_8888, 1, false, 0, request.renderColorSpace);
    if (surface == nullptr) {
      control.fail(LayerImageErrorCode::AllocationFailed);
      return DrawOutcome::Failed;
    }
    auto outcome = session->drawRegion(region, *batch, surface->getCanvas(), control);
    if (outcome != DrawOutcome::Success) {
      return outcome;
    }
    auto sourceX = region.coreRect.left - region.renderRect.left;
    auto sourceY = region.coreRect.top - region.renderRect.top;
    auto readback = surface->asyncReadPixels(Rect::MakeXYWH(sourceX, sourceY, width, height));
    if (readback == nullptr) {
      control.fail(LayerImageErrorCode::ReadbackFailed);
      return DrawOutcome::Failed;
    }
    auto readInfo = ImageInfo::Make(width, height, bandInfo.colorType(), bandInfo.alphaType(),
                                    destination.rowBytes(), bandInfo.colorSpace());
    DEBUG_ASSERT(!readInfo.isEmpty());
    auto rowOffset = static_cast<size_t>(region.coreRect.top - bandFirstRow);
    auto* dstPixels = basePixels + rowOffset * destination.rowBytes() +
                      static_cast<size_t>(region.coreRect.left) * bytesPerPixel;
    pending.push_back({std::move(readback), std::move(readInfo), dstPixels,
                       surface->origin() == ImageOrigin::BottomLeft});
    pendingBytes += readbackBytes;
  }
  return ReadPendingPixels(context, pending, control);
}

LayerImageReadResult LayerImageReader::Impl::readNextBand(Context* context,
                                                          const Pixmap& destination,
                                                          size_t destinationByteSize) {
  LayerImageReadResult result = {};
  if (error != LayerImageErrorCode::None) {
    return fail(error);
  }
  if (cancelled) {
    return cancel();
  }
  if (done) {
    result.code = LayerImageReadCode::Done;
    result.firstRow = rowsRead;
    return result;
  }
  if (context == nullptr || context->gpu() == nullptr) {
    return fail(LayerImageErrorCode::ContextUnavailable);
  }
  if (context->uniqueID() != deviceID) {
    return fail(LayerImageErrorCode::ContextMismatch);
  }
  if (cancelRequested.load()) {
    return cancel();
  }
  const int outputHeight = request.outputSize.height;
  const int bandFirstRow = rowsRead;
  const int bandRows = std::min(bandInfo.height(), outputHeight - bandFirstRow);
  if (!validateDestination(destination, destinationByteSize, bandRows)) {
    return fail(LayerImageErrorCode::InvalidInput);
  }
  LayerDrawLimits limits = {};
  limits.gpuLimits = *context->gpu()->limits();
  limits.outputSurfaceLimit = options.tileSizeLimit;
  auto surfaceLimit = LayerDrawSession::ResolveSurfaceLimit(limits);
  if (surfaceLimit.width <= 2 || surfaceLimit.height <= 2) {
    return fail(LayerImageErrorCode::DeviceLimitExceeded);
  }
  const int coreLimitWidth = surfaceLimit.width - 2;
  const int coreLimitHeight = surfaceLimit.height - 2;
  DrawControl control = {};
  control.cancelRequested = &cancelRequested;
  const int outputWidth = request.outputSize.width;
  const auto estimatedRegions = static_cast<size_t>((outputWidth - 1) / coreLimitWidth + 1);
  std::vector<LayerRegionPlan> regions = {};
  regions.reserve(std::min(estimatedRegions, MaxRegionsPerBatch));
  std::vector<PixelRect> pending = {};
  const int bandEndRow = bandFirstRow + bandRows;
  for (int top = bandFirstRow; top < bandEndRow;) {
    const int bottom = top + std::min(coreLimitHeight, bandEndRow - top);
    for (int left = 0; left < outputWidth;) {
      const int right = left + std::min(coreLimitWidth, outputWidth - left);
      PixelRect core = {left, top, right, bottom};
      for (;;) {
        auto plan = session->planRegion(core, limits, control);
        if (plan.code == RegionPlanCode::Cancelled) {
          return cancel();
        }
        if (plan.code == RegionPlanCode::Failed) {
          return fail(control.error);
        }
        if (plan.code == RegionPlanCode::RequiresSubdivision) {
          DEBUG_ASSERT(core.width() > 1 || core.height() > 1);
          // Split only the unplanned tile. Its disjoint halves can be read back in any order.
          auto remainder = core;
          if (core.width() >= core.height()) {
            core.right = core.left + core.width() / 2;
            remainder.left = core.right;
          } else {
            core.bottom = core.top + core.height() / 2;
            remainder.top = core.bottom;
          }
          pending.push_back(remainder);
          continue;
        }
        regions.push_back(plan.region);
        if (regions.size() == MaxRegionsPerBatch || (pending.empty() && right == outputWidth)) {
          auto outcome =
              drawRegionList(context, regions, limits, bandFirstRow, destination, control);
          if (outcome == DrawOutcome::Failed) {
            return fail(control.error);
          }
          if (outcome == DrawOutcome::Cancelled || cancelRequested.load()) {
            return cancel();
          }
          regions.clear();
        }
        if (pending.empty()) {
          break;
        }
        core = pending.back();
        pending.pop_back();
      }
      left = right;
    }
    top = bottom;
  }
  if (cancelRequested.load()) {
    return cancel();
  }
  rowsRead += bandRows;
  done = rowsRead >= outputHeight;
  result.code = LayerImageReadCode::BandReady;
  result.firstRow = bandFirstRow;
  result.rowCount = bandRows;
  result.lastBand = done;
  return result;
}

std::unique_ptr<LayerImageReader> LayerImageReader::Make(Context* context,
                                                         const LayerImageRequest& request,
                                                         const LayerImageReadOptions& options,
                                                         LayerImageErrorCode* error) {
  if (error != nullptr) {
    *error = LayerImageErrorCode::None;
  }
  if (context == nullptr || context->gpu() == nullptr) {
    return FailMake(error, LayerImageErrorCode::ContextUnavailable);
  }
  if (!IsFiniteColor(request.clearColor)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (request.renderColorSpace == nullptr) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (!IsSupportedColorType(request.colorType)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (!IsSupportedAlphaType(request.alphaType)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (!IsSupportedBlendMode(request.blendMode)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (options.bandRows <= 0) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (options.tileSizeLimit.width < 0 || options.tileSizeLimit.height < 0 ||
      (options.tileSizeLimit.width > 0) != (options.tileSizeLimit.height > 0)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (options.tileSizeLimit.width > 0 && options.tileSizeLimit.width <= 2) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (options.tileSizeLimit.height > 0 && options.tileSizeLimit.height <= 2) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  LayerDrawRequest drawRequest = {};
  drawRequest.target = request.target.get();
  drawRequest.targetLocalToOutput = request.targetLocalToOutput;
  drawRequest.outputSize = request.outputSize;
  drawRequest.renderColorSpace = request.renderColorSpace;
  drawRequest.clearColor = request.clearColor;
  drawRequest.alpha = request.alpha;
  drawRequest.blendMode = request.blendMode;
  DrawControl control = {};
  auto session = LayerDrawSession::Prepare(drawRequest, control);
  if (session == nullptr) {
    return FailMake(error, control.error);
  }

  auto outputColorSpace =
      request.outputColorSpace != nullptr ? request.outputColorSpace : request.renderColorSpace;
  auto bandHeight = std::min(options.bandRows, request.outputSize.height);
  auto bandInfo = ImageInfo::Make(request.outputSize.width, bandHeight, request.colorType,
                                  request.alphaType, 0, outputColorSpace);
  DEBUG_ASSERT(!bandInfo.isEmpty());
  auto maxSize = std::numeric_limits<size_t>::max();
  if (static_cast<uint64_t>(bandInfo.height()) >
      static_cast<uint64_t>(maxSize / bandInfo.rowBytes())) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }

  auto impl = std::make_unique<Impl>();
  impl->session = std::move(session);
  impl->request = request;
  impl->options = options;
  impl->bandInfo = bandInfo;
  impl->deviceID = context->uniqueID();
  return std::unique_ptr<LayerImageReader>(new LayerImageReader(std::move(impl)));
}

LayerImageReader::LayerImageReader(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {
}

LayerImageReader::~LayerImageReader() = default;

ISize LayerImageReader::outputSize() const {
  return impl->request.outputSize;
}

const ImageInfo& LayerImageReader::bandInfo() const {
  return impl->bandInfo;
}

int LayerImageReader::rowsRead() const {
  return impl->rowsRead;
}

bool LayerImageReader::isDone() const {
  return impl->done;
}

LayerImageReadResult LayerImageReader::readNextBand(Context* context, const Pixmap& destination,
                                                    size_t destinationByteSize) {
  return impl->readNextBand(context, destination, destinationByteSize);
}

void LayerImageReader::requestCancel() noexcept {
  impl->cancelRequested.store(true);
}

}  // namespace tgfx
