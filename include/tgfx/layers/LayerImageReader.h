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

#include <cstddef>
#include <memory>
#include <string>
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ColorType.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Size.h"

namespace tgfx {
class Context;
class Layer;
class Pixmap;

/**
 * Error codes produced while generating pixels from a layer tree. They only cover the pixel
 * generation itself; encoding, delivery and scene version problems belong to the caller.
 */
enum class LayerImageErrorCode {
  None,
  InvalidInput,
  UnsupportedContent,
  ContextUnavailable,
  ContextMismatch,
  DeviceLimitExceeded,
  AllocationFailed,
  DrawFailed,
  ReadbackFailed
};

/**
 * Phase in which a layer image error was observed.
 */
enum class LayerImageStage { Validation, Planning, Background, Rendering, Readback };

/**
 * Diagnostic information of a failed layer image request.
 */
struct LayerImageError {
  LayerImageErrorCode code = LayerImageErrorCode::None;
  LayerImageStage stage = LayerImageStage::Validation;
  // First output row that could not be delivered.
  int firstRow = 0;
  // Size of the failing target when it is relevant, (0, 0) otherwise.
  ISize surfaceSize = {};
  // Owned description of the failure.
  std::string message = {};
};

/**
 * Description of one layer image: which layer to draw, in which scene context and with which
 * fixed output mapping. The request is fixed when the reader is created and must stay valid for
 * the whole read sequence.
 */
struct LayerImageRequest {
  // The layer drawn with the existing Layer::draw() semantics. Its own matrix, alpha, blend mode,
  // visible flag and scroll rect are not applied automatically; the fields below carry them.
  std::shared_ptr<Layer> target;
  // Keeps the background capture context alive and defines the scene the target belongs to. For
  // an attached target this must be the display list's root layer; for a detached target it may be
  // the target itself. Other ancestors are rejected because their coordinate space would differ
  // from the one the layer bounds are defined in.
  std::shared_ptr<Layer> sceneRoot;

  // Size of the final image in pixels. It is already limited and rounded by the caller.
  ISize outputSize = {};
  // Complete 2D mapping from the target's local space to final output pixels, including the
  // export scale and the crop translation.
  Matrix targetLocalToOutput = Matrix::I();

  // Explicit drawing parameters matching Layer::draw(canvas, alpha, blendMode).
  float alpha = 1.0f;
  BlendMode blendMode = BlendMode::SrcOver;

  // Color space used for drawing and intermediate effects. Must not be null.
  std::shared_ptr<ColorSpace> renderColorSpace = ColorSpace::SRGB();
  // Color space of the delivered pixels. A null value is normalized to renderColorSpace.
  std::shared_ptr<ColorSpace> outputColorSpace = nullptr;

  // Delivered pixel format. RGBA_8888 and BGRA_8888 are supported.
  ColorType colorType = ColorType::RGBA_8888;
  // Delivered alpha type. Premultiplied and Unpremultiplied are supported; JPEG matte is left to
  // the encoding layer.
  AlphaType alphaType = AlphaType::Premultiplied;

  // Virtual scene background, used both as the initial output color and as part of the captured
  // backdrop of background-sourced effects.
  Color clearColor = Color::Transparent();
};

/**
 * Options that control how many rows are pulled per call and how large a drawing surface may be.
 */
struct LayerImageReadOptions {
  // Maximum number of rows returned by one successful read. Must be positive.
  int bandRows = 0;
  // Upper bound for the drawing surface of one region, including its sampling margin. (0, 0)
  // lets the device limit decide. Otherwise both dimensions must be positive.
  ISize tileSizeLimit = {0, 0};
};

/**
 * Result code of one band read.
 */
enum class LayerImageReadCode { BandReady, Done, Cancelled, Failed };

/**
 * Result of one band read.
 */
struct LayerImageReadResult {
  LayerImageReadCode code = LayerImageReadCode::Failed;
  // First row covered by the delivered band.
  int firstRow = 0;
  // Number of delivered rows, counting from index 0 of the destination buffer.
  int rowCount = 0;
  // True when the delivered band contains the final output row.
  bool lastBand = false;
  // Failure details when code is Failed.
  LayerImageError error = {};
};

/**
 * Generates pixels of one layer tree on demand, band by band, using a sequence of small GPU
 * targets. The caller owns the CPU band buffer and the encoder; TGFX holds no encoder, queue or
 * callback. All methods except requestCancel() must be called on the scene's rendering thread
 * while a valid Context of the device bound at creation is locked.
 *
 * The reader holds strong references to the target and scene root, but that does not freeze the
 * scene: the caller must keep the layers, styles, fonts and image resources unchanged until the
 * last band has been read.
 */
class LayerImageReader {
 public:
  /**
   * Validates the request and creates a reader bound to the device of the given context. Only CPU
   * planning state is created here; GPU pixels are allocated while reading. On failure this
   * returns nullptr and fills the optional error output.
   * @param context the locked GPU context of the scene's device.
   * @param request the fixed layer image description.
   * @param options band and surface limits.
   * @param error optional failure details.
   */
  static std::unique_ptr<LayerImageReader> Make(Context* context, const LayerImageRequest& request,
                                                const LayerImageReadOptions& options,
                                                LayerImageError* error = nullptr);

  ~LayerImageReader();

  LayerImageReader(const LayerImageReader&) = delete;
  LayerImageReader& operator=(const LayerImageReader&) = delete;

  /**
   * Returns the final image size in pixels.
   */
  ISize outputSize() const;

  /**
   * Returns the normalized template of one band buffer: output width, min(bandRows, outputHeight)
   * rows, the requested format, alpha and color space, and a compact row stride. The caller may use
   * a larger legal stride.
   */
  const ImageInfo& bandInfo() const;

  /**
   * Returns the number of rows already delivered.
   */
  int rowsRead() const;

  /**
   * Returns true once all output rows have been delivered.
   */
  bool isDone() const;

  /**
   * Draws and reads the next band into the caller's buffer. The call is synchronous and returns
   * after the whole band is written; it never hands CPU pixels to a background worker.
   *
   * The destination must be writable, have the template's width, format, alpha type and color
   * space, at least the required number of rows and a valid stride. destinationByteSize must
   * describe the writable range starting at destination.writablePixels(). The buffer must stay
   * valid and unshared for the duration of the call.
   * @param context the locked GPU context; it must belong to the device bound at creation.
   * @param destination writable CPU pixels that receive the band.
   * @param destinationByteSize writable byte count from destination.writablePixels().
   */
  LayerImageReadResult readNextBand(Context* context, const Pixmap& destination,
                                    size_t destinationByteSize);

  /**
   * Records a cancellation request. It is the only method that may be called from another thread
   * and it never performs GPU work. The request is observed at safe boundaries: before a band is
   * published or between regions of a band. It does not promise to interrupt an already running
   * GPU or readback operation, and it does not change a task that already reached its final state.
   */
  void requestCancel() noexcept;

 private:
  class Impl;
  explicit LayerImageReader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl;
};

}  // namespace tgfx
