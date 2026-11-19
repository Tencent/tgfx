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
#include <limits>
#include <utility>
#include <vector>
#include "layers/LayerDrawSession.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

namespace {

// Bound temporary planning storage while still sharing captures across neighboring regions.
static constexpr size_t MaxRegionsPerBatch = 64;

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

static LayerImageErrorCode ToImageCode(DrawFailureCode code) {
  switch (code) {
    case DrawFailureCode::None:
      return LayerImageErrorCode::None;
    case DrawFailureCode::InvalidRequest:
      return LayerImageErrorCode::InvalidInput;
    case DrawFailureCode::UnsupportedContent:
      return LayerImageErrorCode::UnsupportedContent;
    case DrawFailureCode::SurfaceLimit:
      return LayerImageErrorCode::DeviceLimitExceeded;
    case DrawFailureCode::MissingDependency:
      return LayerImageErrorCode::DrawFailed;
    case DrawFailureCode::AllocationFailure:
      return LayerImageErrorCode::AllocationFailed;
    case DrawFailureCode::RenderingFailure:
      return LayerImageErrorCode::DrawFailed;
  }
  return LayerImageErrorCode::DrawFailed;
}

static std::unique_ptr<LayerImageReader> FailMake(LayerImageErrorCode* error,
                                                  LayerImageErrorCode code) {
  if (error != nullptr) {
    *error = code;
  }
  return nullptr;
}

struct BandSurface {
  ISize size = {};
  std::shared_ptr<Surface> surface = nullptr;
};

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

  LayerImageReadResult failFromControl(const DrawControl& control) {
    return fail(ToImageCode(control.firstFailure.code));
  }

  bool validateDestination(const Pixmap& destination, size_t destinationByteSize,
                           int requiredRows) const;
  DrawOutcome drawRegionList(Context* context, const std::vector<LayerRegionPlan>& regions,
                             int bandFirstRow, const Pixmap& destination, DrawControl* control,
                             BandSurface* bandSurface, LayerImageReadResult* failure);
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
  if (info.width() != bandInfo.width() || info.colorType() != bandInfo.colorType() ||
      info.alphaType() != bandInfo.alphaType() ||
      !ColorSpace::Equals(info.colorSpace().get(), bandInfo.colorSpace().get())) {
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
                                                   int bandFirstRow, const Pixmap& destination,
                                                   DrawControl* control, BandSurface* bandSurface,
                                                   LayerImageReadResult* failure) {
  auto batchResult = session->planBatch(
      regions, LayerDrawLimits{*context->gpu()->limits(), options.tileSizeLimit}, control);
  if (batchResult.code == BatchPlanCode::RequiresSplit) {
    using Difference = std::vector<LayerRegionPlan>::difference_type;
    auto mid = static_cast<Difference>(regions.size() / 2);
    std::vector<LayerRegionPlan> left(regions.begin(), regions.begin() + mid);
    std::vector<LayerRegionPlan> right(regions.begin() + mid, regions.end());
    auto first =
        drawRegionList(context, left, bandFirstRow, destination, control, bandSurface, failure);
    if (first != DrawOutcome::Success) {
      return first;
    }
    return drawRegionList(context, right, bandFirstRow, destination, control, bandSurface, failure);
  }
  if (batchResult.code == BatchPlanCode::Cancelled) {
    return DrawOutcome::Cancelled;
  }
  if (batchResult.code != BatchPlanCode::Ready || batchResult.plan == nullptr) {
    *failure = control != nullptr && control->failed() ? failFromControl(*control)
                                                       : fail(LayerImageErrorCode::DrawFailed);
    return DrawOutcome::Failed;
  }
  auto batch = session->prepareBatch(context, *batchResult.plan, control);
  if (batch == nullptr) {
    if (control != nullptr && control->failed()) {
      *failure = failFromControl(*control);
    } else {
      *failure = fail(LayerImageErrorCode::AllocationFailed);
    }
    return DrawOutcome::Failed;
  }
  auto& surface = bandSurface->surface;
  if (surface == nullptr) {
    surface = Surface::Make(context, bandSurface->size.width, bandSurface->size.height,
                            ColorType::RGBA_8888, 1, false, 0, request.renderColorSpace);
    if (surface == nullptr) {
      *failure = fail(LayerImageErrorCode::AllocationFailed);
      return DrawOutcome::Failed;
    }
  }
  LayerDrawTarget target = {};
  target.canvas = surface->getCanvas();
  target.clearBeforeDraw = true;
  auto bytesPerPixel = bandInfo.bytesPerPixel();
  auto* basePixels = static_cast<uint8_t*>(destination.writablePixels());
  for (size_t i = 0; i < batch->regionCount(); ++i) {
    auto& region = regions[i];
    auto outcome = session->drawRegion(context, *batch, i, target, control);
    if (outcome == DrawOutcome::Cancelled) {
      return DrawOutcome::Cancelled;
    }
    if (outcome != DrawOutcome::Success) {
      if (control != nullptr && control->failed()) {
        *failure = failFromControl(*control);
      } else {
        *failure = fail(LayerImageErrorCode::DrawFailed);
      }
      return DrawOutcome::Failed;
    }
    auto sourceX = region.coreRect.left - region.renderRect.left;
    auto sourceY = region.coreRect.top - region.renderRect.top;
    auto readInfo =
        ImageInfo::Make(region.coreRect.width(), region.coreRect.height(), bandInfo.colorType(),
                        bandInfo.alphaType(), destination.rowBytes(), bandInfo.colorSpace());
    if (readInfo.isEmpty()) {
      *failure = fail(LayerImageErrorCode::InvalidInput);
      return DrawOutcome::Failed;
    }
    auto rowOffset = static_cast<size_t>(region.coreRect.top - bandFirstRow);
    auto* dstPixels = basePixels + rowOffset * destination.rowBytes() +
                      static_cast<size_t>(region.coreRect.left) * bytesPerPixel;
    if (!surface->readPixels(readInfo, dstPixels, sourceX, sourceY)) {
      *failure = fail(LayerImageErrorCode::ReadbackFailed);
      return DrawOutcome::Failed;
    }
    if (cancelRequested.load()) {
      return DrawOutcome::Cancelled;
    }
  }
  return DrawOutcome::Success;
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
  BandSurface bandSurface = {};
  bandSurface.size = {std::min(outputWidth, coreLimitWidth) + 2,
                      std::min(bandRows, coreLimitHeight) + 2};
  const auto estimatedRegions = static_cast<size_t>((outputWidth - 1) / coreLimitWidth + 1);
  std::vector<LayerRegionPlan> regions = {};
  regions.reserve(std::min(estimatedRegions, MaxRegionsPerBatch));
  int bandOffsetY = 0;
  while (bandOffsetY < bandRows) {
    int chunkHeight = std::min(coreLimitHeight, bandRows - bandOffsetY);
    bool planned = false;
    while (!planned) {
      regions.clear();
      planned = true;
      int offsetX = 0;
      while (offsetX < outputWidth) {
        int width = std::min(coreLimitWidth, outputWidth - offsetX);
        LayerRegionPlan region = {};
        bool regionReady = false;
        for (;;) {
          PixelRect core = {};
          core.left = offsetX;
          core.top = bandFirstRow + bandOffsetY;
          core.right = offsetX + width;
          core.bottom = bandFirstRow + bandOffsetY + chunkHeight;
          auto regionResult = session->planRegion(core, limits, &control);
          if (regionResult.code == RegionPlanCode::Ready) {
            region = regionResult.region;
            regionReady = true;
            break;
          }
          if (regionResult.code == RegionPlanCode::Cancelled) {
            return cancel();
          }
          if (regionResult.code == RegionPlanCode::Failed) {
            if (control.failed()) {
              return failFromControl(control);
            }
            return fail(LayerImageErrorCode::DeviceLimitExceeded);
          }
          if (width <= 1) {
            break;
          }
          width = std::max(1, width / 2);
        }
        if (!regionReady) {
          planned = false;
          break;
        }
        regions.push_back(region);
        offsetX = region.coreRect.right;
        if (regions.size() < MaxRegionsPerBatch && offsetX < outputWidth) {
          continue;
        }
        LayerImageReadResult failure = {};
        auto outcome = drawRegionList(context, regions, bandFirstRow, destination, &control,
                                      &bandSurface, &failure);
        if (outcome == DrawOutcome::Failed) {
          return failure;
        }
        if (outcome == DrawOutcome::Cancelled || cancelRequested.load()) {
          return cancel();
        }
        regions.clear();
      }
      if (!planned) {
        if (chunkHeight <= 1) {
          return fail(LayerImageErrorCode::DeviceLimitExceeded);
        }
        // Earlier batches may already have written into the destination. The shorter strip
        // redraws from x = 0, and later strips overwrite any rows below it before publication.
        chunkHeight = std::max(1, chunkHeight / 2);
      }
    }
    bandOffsetY += chunkHeight;
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
  if (request.target == nullptr) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (request.outputSize.isEmpty()) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (request.outputSize.width > MaxLayerExportCoordinate ||
      request.outputSize.height > MaxLayerExportCoordinate) {
    return FailMake(error, LayerImageErrorCode::UnsupportedContent);
  }
  if (!request.targetLocalToOutput.isFinite() || !request.targetLocalToOutput.invertible()) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (!std::isfinite(request.alpha) || request.alpha < 0.0f || request.alpha > 1.0f) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
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
  auto outputColorSpace =
      request.outputColorSpace != nullptr ? request.outputColorSpace : request.renderColorSpace;
  auto bandHeight = std::min(options.bandRows, request.outputSize.height);
  auto bandInfo = ImageInfo::Make(request.outputSize.width, bandHeight, request.colorType,
                                  request.alphaType, 0, outputColorSpace);
  if (bandInfo.isEmpty()) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  auto bytesPerPixel = bandInfo.bytesPerPixel();
  auto maxSize = std::numeric_limits<size_t>::max();
  if (static_cast<uint64_t>(bandInfo.width()) > static_cast<uint64_t>(maxSize / bytesPerPixel)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput);
  }
  if (static_cast<uint64_t>(bandInfo.height()) >
      static_cast<uint64_t>(maxSize / bandInfo.rowBytes())) {
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
  auto session = LayerDrawSession::Prepare(context, drawRequest, &control);
  if (session == nullptr) {
    return FailMake(error, ToImageCode(control.firstFailure.code));
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
