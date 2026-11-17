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
#include "core/utils/Log.h"
#include "layers/LayerDrawSession.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

namespace {

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
  auto value = static_cast<int>(blendMode);
  return value >= static_cast<int>(BlendMode::Clear) &&
         value <= static_cast<int>(BlendMode::PlusDarker);
}

static LayerImageStage ToImageStage(DrawStage stage) {
  switch (stage) {
    case DrawStage::Validation:
      return LayerImageStage::Validation;
    case DrawStage::Planning:
      return LayerImageStage::Planning;
    case DrawStage::Background:
      return LayerImageStage::Background;
    case DrawStage::Rendering:
      return LayerImageStage::Rendering;
  }
  return LayerImageStage::Validation;
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

static LayerImageError MapFailure(const DrawFailure& failure, int firstRow) {
  LayerImageError error = {};
  error.code = ToImageCode(failure.code);
  error.stage = ToImageStage(failure.stage);
  error.firstRow = firstRow;
  error.surfaceSize = failure.surfaceSize;
  error.message = failure.message;
  return error;
}

static std::unique_ptr<LayerImageReader> FailMake(LayerImageError* error, LayerImageErrorCode code,
                                                  LayerImageStage stage, std::string message) {
  if (error != nullptr) {
    error->code = code;
    error->stage = stage;
    error->message = std::move(message);
  }
  return nullptr;
}

struct TileSurfaceCache {
  std::shared_ptr<Surface> surface = nullptr;
  int width = 0;
  int height = 0;
};

static std::shared_ptr<Surface> AcquireTileSurface(Context* context, TileSurfaceCache* cache,
                                                   int width, int height,
                                                   const std::shared_ptr<ColorSpace>& colorSpace) {
  if (cache->surface != nullptr && cache->width == width && cache->height == height) {
    return cache->surface;
  }
  auto surface =
      Surface::Make(context, width, height, ColorType::RGBA_8888, 1, false, 0, colorSpace);
  if (surface == nullptr) {
    return nullptr;
  }
  cache->surface = surface;
  cache->width = width;
  cache->height = height;
  return surface;
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
  bool failed = false;
  bool cancelled = false;
  LayerImageError error = {};

  LayerImageReadResult fail(LayerImageErrorCode code, LayerImageStage stage, std::string message,
                            ISize surfaceSize = {}) {
    failed = true;
    error.code = code;
    error.stage = stage;
    error.firstRow = rowsRead;
    error.surfaceSize = surfaceSize;
    error.message = std::move(message);
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
    error = MapFailure(control.firstFailure, rowsRead);
    failed = true;
    LayerImageReadResult result = {};
    result.code = LayerImageReadCode::Failed;
    result.firstRow = rowsRead;
    result.error = error;
    return result;
  }

  bool validateDestination(const Pixmap& destination, size_t destinationByteSize,
                           int requiredRows) const;
  DrawOutcome drawRegionList(Context* context, const std::vector<LayerRegionPlan>& regions,
                             int bandFirstRow, const Pixmap& destination, DrawControl* control,
                             TileSurfaceCache* surfaceCache, LayerImageReadResult* failure);
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
                                                   DrawControl* control,
                                                   TileSurfaceCache* surfaceCache,
                                                   LayerImageReadResult* failure) {
  auto batchResult = session->planBatch(
      regions, LayerDrawLimits{*context->gpu()->limits(), options.tileSizeLimit}, control);
  if (batchResult.code == BatchPlanCode::RequiresSplit) {
    if (regions.size() <= 1) {
      *failure = fail(LayerImageErrorCode::DeviceLimitExceeded, LayerImageStage::Planning,
                      "a single output region cannot share its background capture");
      return DrawOutcome::Failed;
    }
    using Difference = std::vector<LayerRegionPlan>::difference_type;
    auto mid = static_cast<Difference>(regions.size() / 2);
    std::vector<LayerRegionPlan> left(regions.begin(), regions.begin() + mid);
    std::vector<LayerRegionPlan> right(regions.begin() + mid, regions.end());
    auto first =
        drawRegionList(context, left, bandFirstRow, destination, control, surfaceCache, failure);
    if (first != DrawOutcome::Success) {
      return first;
    }
    return drawRegionList(context, right, bandFirstRow, destination, control, surfaceCache,
                          failure);
  }
  if (batchResult.code == BatchPlanCode::Cancelled) {
    return DrawOutcome::Cancelled;
  }
  if (batchResult.code != BatchPlanCode::Ready || batchResult.plan == nullptr) {
    *failure = fail(LayerImageErrorCode::DrawFailed, LayerImageStage::Planning,
                    "failed to plan a batch of output regions");
    return DrawOutcome::Failed;
  }
  auto batch = session->prepareBatch(context, *batchResult.plan, control);
  if (batch == nullptr) {
    if (control != nullptr && control->failed()) {
      *failure = failFromControl(*control);
    } else {
      *failure = fail(LayerImageErrorCode::AllocationFailed, LayerImageStage::Background,
                      "failed to prepare the background of a batch");
    }
    return DrawOutcome::Failed;
  }
  auto bytesPerPixel = bandInfo.bytesPerPixel();
  auto* basePixels = static_cast<uint8_t*>(destination.writablePixels());
  for (size_t i = 0; i < batch->regionCount(); ++i) {
    auto& region = regions[i];
    auto surface = AcquireTileSurface(context, surfaceCache, region.renderRect.width(),
                                      region.renderRect.height(), request.renderColorSpace);
    if (surface == nullptr) {
      *failure = fail(LayerImageErrorCode::AllocationFailed, LayerImageStage::Rendering,
                      "failed to allocate an output drawing surface",
                      ISize::Make(region.renderRect.width(), region.renderRect.height()));
      return DrawOutcome::Failed;
    }
    LayerDrawTarget target = {};
    target.canvas = surface->getCanvas();
    target.outputOrigin = Point::Make(static_cast<float>(region.renderRect.left),
                                      static_cast<float>(region.renderRect.top));
    target.clearBeforeDraw = true;
    auto outcome = session->drawRegion(context, *batch, i, target, control);
    if (outcome == DrawOutcome::Cancelled) {
      return DrawOutcome::Cancelled;
    }
    if (outcome != DrawOutcome::Success) {
      if (control != nullptr && control->failed()) {
        *failure = failFromControl(*control);
      } else {
        *failure = fail(LayerImageErrorCode::DrawFailed, LayerImageStage::Rendering,
                        "failed to draw an output region");
      }
      return DrawOutcome::Failed;
    }
    auto sourceX = region.coreRect.left - region.renderRect.left;
    auto sourceY = region.coreRect.top - region.renderRect.top;
    auto readInfo =
        ImageInfo::Make(region.coreRect.width(), region.coreRect.height(), bandInfo.colorType(),
                        bandInfo.alphaType(), destination.rowBytes(), bandInfo.colorSpace());
    if (readInfo.isEmpty()) {
      *failure = fail(LayerImageErrorCode::InvalidInput, LayerImageStage::Readback,
                      "the readback area is invalid");
      return DrawOutcome::Failed;
    }
    auto rowOffset = static_cast<size_t>(region.coreRect.top - bandFirstRow);
    auto* dstPixels = basePixels + rowOffset * destination.rowBytes() +
                      static_cast<size_t>(region.coreRect.left) * bytesPerPixel;
    if (!surface->readPixels(readInfo, dstPixels, sourceX, sourceY)) {
      *failure = fail(LayerImageErrorCode::ReadbackFailed, LayerImageStage::Readback,
                      "failed to read a finished output region back to CPU pixels",
                      ISize::Make(region.renderRect.width(), region.renderRect.height()));
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
  if (failed) {
    result.code = LayerImageReadCode::Failed;
    result.firstRow = rowsRead;
    result.error = error;
    return result;
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
    return fail(LayerImageErrorCode::ContextUnavailable, LayerImageStage::Rendering,
                "the GPU context is not usable");
  }
  if (context->uniqueID() != deviceID) {
    return fail(LayerImageErrorCode::ContextMismatch, LayerImageStage::Rendering,
                "the GPU context does not belong to the device bound at creation");
  }
  if (cancelRequested.load()) {
    return cancel();
  }
  const int outputHeight = request.outputSize.height;
  const int bandFirstRow = rowsRead;
  const int bandRows = std::min(bandInfo.height(), outputHeight - bandFirstRow);
  if (!validateDestination(destination, destinationByteSize, bandRows)) {
    return fail(LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                "the destination buffer does not match the band template");
  }
  LayerDrawLimits limits = {};
  limits.gpuLimits = *context->gpu()->limits();
  limits.outputSurfaceLimit = options.tileSizeLimit;
  auto surfaceLimit = LayerDrawSession::ResolveSurfaceLimit(limits);
  if (surfaceLimit.width <= 2 || surfaceLimit.height <= 2) {
    return fail(LayerImageErrorCode::DeviceLimitExceeded, LayerImageStage::Planning,
                "the device texture limit is too small for a tiled export");
  }
  const int coreLimitWidth = surfaceLimit.width - 2;
  const int coreLimitHeight = surfaceLimit.height - 2;
  DrawControl control = {};
  control.cancelRequested = &cancelRequested;
  TileSurfaceCache surfaceCache = {};
  int bandOffsetY = 0;
  while (bandOffsetY < bandRows) {
    int chunkHeight = std::min(coreLimitHeight, bandRows - bandOffsetY);
    std::vector<LayerRegionPlan> regions = {};
    bool planned = false;
    while (!planned) {
      regions.clear();
      planned = true;
      int offsetX = 0;
      while (offsetX < request.outputSize.width) {
        int width = std::min(coreLimitWidth, request.outputSize.width - offsetX);
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
            return fail(LayerImageErrorCode::DeviceLimitExceeded, LayerImageStage::Planning,
                        "an output region cannot be planned");
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
      }
      if (!planned) {
        if (chunkHeight <= 1) {
          return fail(LayerImageErrorCode::DeviceLimitExceeded, LayerImageStage::Planning,
                      "no output region fits inside the device texture limit");
        }
        chunkHeight = std::max(1, chunkHeight / 2);
      }
    }
    LayerImageReadResult failure = {};
    auto outcome = drawRegionList(context, regions, bandFirstRow, destination, &control,
                                  &surfaceCache, &failure);
    if (outcome == DrawOutcome::Failed) {
      if (failure.code == LayerImageReadCode::Failed) {
        return failure;
      }
      return fail(LayerImageErrorCode::DrawFailed, LayerImageStage::Rendering,
                  "failed to draw a band");
    }
    if (outcome == DrawOutcome::Cancelled || cancelRequested.load()) {
      return cancel();
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
                                                         LayerImageError* error) {
  if (error != nullptr) {
    *error = {};
  }
  if (context == nullptr || context->gpu() == nullptr) {
    return FailMake(error, LayerImageErrorCode::ContextUnavailable, LayerImageStage::Validation,
                    "the GPU context is not usable");
  }
  if (request.target == nullptr || request.sceneRoot == nullptr) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the request has no target or scene root");
  }
  if (!ImageInfo::IsValidSize(request.outputSize.width, request.outputSize.height)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the output size is invalid");
  }
  if (!request.targetLocalToOutput.isFinite() || !request.targetLocalToOutput.invertible()) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the target to output matrix is invalid");
  }
  if (!std::isfinite(request.alpha) || request.alpha < 0.0f || request.alpha > 1.0f) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "alpha is not a finite value in [0, 1]");
  }
  if (!IsFiniteColor(request.clearColor)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the clear color is not finite");
  }
  if (request.renderColorSpace == nullptr) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the render color space is null");
  }
  if (!IsSupportedColorType(request.colorType)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "only RGBA_8888 and BGRA_8888 output are supported");
  }
  if (!IsSupportedAlphaType(request.alphaType)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "only premultiplied and unpremultiplied output are supported");
  }
  if (!IsSupportedBlendMode(request.blendMode)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the blend mode is not a supported value");
  }
  if (options.bandRows <= 0) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "bandRows must be positive");
  }
  if (options.tileSizeLimit.width < 0 || options.tileSizeLimit.height < 0 ||
      (options.tileSizeLimit.width > 0) != (options.tileSizeLimit.height > 0)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "tileSizeLimit must be (0, 0) or positive on both dimensions");
  }
  if (options.tileSizeLimit.width > 0 && options.tileSizeLimit.width <= 2) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "tileSizeLimit leaves no room for a non-empty core region");
  }
  if (options.tileSizeLimit.height > 0 && options.tileSizeLimit.height <= 2) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "tileSizeLimit leaves no room for a non-empty core region");
  }
  auto outputColorSpace =
      request.outputColorSpace != nullptr ? request.outputColorSpace : request.renderColorSpace;
  auto bandHeight = std::min(options.bandRows, request.outputSize.height);
  auto bandInfo = ImageInfo::Make(request.outputSize.width, bandHeight, request.colorType,
                                  request.alphaType, 0, outputColorSpace);
  if (bandInfo.isEmpty()) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "the band template is invalid");
  }
  auto bytesPerPixel = bandInfo.bytesPerPixel();
  auto maxSize = std::numeric_limits<size_t>::max();
  if (static_cast<uint64_t>(bandInfo.width()) > static_cast<uint64_t>(maxSize / bytesPerPixel)) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "one band row does not fit in the address space");
  }
  if (static_cast<uint64_t>(bandInfo.height()) >
      static_cast<uint64_t>(maxSize / bandInfo.rowBytes())) {
    return FailMake(error, LayerImageErrorCode::InvalidInput, LayerImageStage::Validation,
                    "one band buffer does not fit in the address space");
  }

  LayerDrawRequest drawRequest = {};
  drawRequest.target = request.target.get();
  drawRequest.captureRoot = request.sceneRoot.get();
  drawRequest.context = context;
  drawRequest.targetLocalToOutput = request.targetLocalToOutput;
  drawRequest.outputSize = request.outputSize;
  drawRequest.renderColorSpace = request.renderColorSpace;
  drawRequest.clearColor = request.clearColor;
  drawRequest.alpha = request.alpha;
  drawRequest.blendMode = request.blendMode;
  drawRequest.samplingMode = LayerSamplingMode::ExportFixed;
  DrawControl control = {};
  auto session = LayerDrawSession::Prepare(context, drawRequest, &control);
  if (session == nullptr) {
    auto failure = MapFailure(control.firstFailure, 0);
    if (error != nullptr) {
      *error = failure;
    }
    return nullptr;
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
