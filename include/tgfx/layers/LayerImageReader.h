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
  /**
   * No error occurred.
   */
  None,
  /**
   * The request, options or destination buffer are invalid. For example: a null target or
   * render color space, an empty output size, a non-finite clear color, an alpha outside [0, 1], a
   * non-invertible output matrix, an unsupported color type or alpha type, a non-positive
   * bandRows, an illegal tileSizeLimit, a band buffer that exceeds the addressable memory, or a
   * destination that does not match the band template or is too small.
   */
  InvalidInput,
  /**
   * The layer tree or output mapping cannot be exported by the reader. For example: a hidden
   * target branch, 3D content that participates in scene drawing or masking, a perspective output
   * matrix, or output dimensions, translations or background capture bounds beyond 2^23.
   */
  UnsupportedContent,
  /**
   * The given context is null or has no GPU.
   */
  ContextUnavailable,
  /**
   * The given context does not belong to the device bound when the reader was created.
   */
  ContextMismatch,
  /**
   * The device cannot draw the content within its limits. For example: the resolved surface limit
   * leaves no room for the one-pixel sampling margin, a 1x1 output tile or its background capture
   * still exceeds the device texture limit or the 32 MiB capture budget, or an offscreen layer
   * exceeds the device texture limit.
   */
  DeviceLimitExceeded,
  /**
   * Allocating a GPU drawing surface, background capture or offscreen target failed.
   */
  AllocationFailed,
  /**
   * Drawing the layer tree failed, for example when a filter, background style or mask could not
   * produce its result.
   */
  DrawFailed,
  /**
   * Reading the drawn pixels back into the destination buffer failed.
   */
  ReadbackFailed
};

/**
 * Description of one layer image: which layer to draw, in which scene context and with which
 * fixed output mapping. The request is fixed when the reader is created and must stay valid for
 * the whole read sequence.
 */
struct LayerImageRequest {
  // The layer to draw in its local coordinate space. The request supplies the output matrix,
  // alpha and blend mode. The scene root supplies the backdrop for an attached target. A detached
  // target is exported on its own. The target branch must participate in scene drawing. Attached
  // targets inherit ancestor scroll rects and masks. The output mapping can cover local content
  // beyond the target's own scroll rect.
  std::shared_ptr<Layer> target;

  // Size of the final image in pixels, rounded by the caller. Each dimension supports up to 2^23.
  ISize outputSize = {};
  // Complete 2D mapping from the target's local space to final output pixels, including the
  // export scale and the crop translation.
  Matrix targetLocalToOutput = Matrix::I();

  // Opacity passed to the target draw. Must be finite and in [0, 1].
  float alpha = 1.0f;
  // Blend mode passed to the target draw.
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
  // Upper bound for one output tile's drawing surface, including its one-pixel sampling margin.
  // With (0, 0), each dimension is capped at the smaller of 2048 pixels and the device's texture
  // limit.
  // For an explicit limit, both dimensions must be at least 3 pixels. The device limit still
  // applies to each dimension.
  // The shared capture surface for each batch has a 32 MiB RGBA8 pixel budget and must fit the
  // device texture dimensions.
  ISize tileSizeLimit = {0, 0};
};

/**
 * Result code of one band read.
 */
enum class LayerImageReadCode {
  /**
   * A band of rowCount complete rows was written to the destination and published. lastBand is
   * true when this band contains the final output row; the reader is then done.
   */
  BandReady,
  /**
   * All output rows were already delivered by earlier calls. Nothing is written.
   */
  Done,
  /**
   * A cancellation request was observed before the band was published. The current band must be
   * discarded, and all later calls return Cancelled.
   */
  Cancelled,
  /**
   * The read failed with the reported error code. The current band must be discarded, and all
   * later calls return the same failure.
   */
  Failed
};

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
  // Error code when code is Failed.
  LayerImageErrorCode error = LayerImageErrorCode::None;
};

/**
 * Generates pixels of one layer tree on demand, band by band, using a sequence of small GPU
 * targets. The caller owns the CPU band buffer and the encoder; TGFX holds no encoder, queue or
 * callback. All methods except requestCancel() must be called on the scene's rendering thread
 * while a valid Context of the device bound at creation is locked.
 *
 * The reader reports UnsupportedContent for hidden targets or 3D content that participates in
 * scene drawing or masking. Output dimensions, tile origins, and output-space translations are
 * limited to a magnitude of 2^23.
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
   * @param error optional error code.
   */
  static std::unique_ptr<LayerImageReader> Make(Context* context, const LayerImageRequest& request,
                                                const LayerImageReadOptions& options,
                                                LayerImageErrorCode* error = nullptr);

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
   * Synchronously draws the next band into the caller's buffer. BandReady delivers rowCount
   * complete rows. A subsequent call after the last band returns Done. A failed or cancelled
   * read may have written part of the current band. Discard that band. Subsequent calls return
   * the same failure or cancellation without writing to the buffer.
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
