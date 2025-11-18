/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include <optional>
#include <string>
#include <unordered_map>
#include "../../include/tgfx/core/Log.h"
#include "core/utils/AddressOf.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/SVGLengthContext.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/TextShaper.h"

namespace tgfx {

class SVGNode;

template <typename T>
class CopyOnWrite {
 public:
  explicit CopyOnWrite(const T& initial) : object(&initial) {
  }

  explicit CopyOnWrite(const T* initial) : object(initial) {
  }

  // Constructor for delayed initialization.
  CopyOnWrite() : object(nullptr) {
  }

  CopyOnWrite(const CopyOnWrite& that) {
    *this = that;
  }

  CopyOnWrite(CopyOnWrite&& that) noexcept {
    *this = std::move(that);
  }

  CopyOnWrite& operator=(const CopyOnWrite& that) {
    optional = that.optional;
    object = optional.has_value() ? AddressOf(optional) : that.object;
    return *this;
  }

  CopyOnWrite& operator=(CopyOnWrite&& that) {
    optional = std::move(that.optional);
    object = optional.has_value() ? AddressOf(optional) : that.object;
    return *this;
  }

  /**
   * Returns a writable T*. The first time this is called the initial object is cloned.
   */
  T* writable() {
    if (!optional.has_value()) {
      optional = *object;
      object = AddressOf(optional);
    }
    return AddressOf(optional);
  }

  const T* get() const {
    return object;
  }

  /**
     * Operators for treating this as though it were a const pointer.
     */

  const T* operator->() const {
    return object;
  }

  const T& operator*() const {
    return *object;
  }

 private:
  const T* object;
  std::optional<T> optional;
};

struct SVGPresentationContext {
  SVGPresentationContext();
  SVGPresentationContext(const SVGPresentationContext& other) = default;
  SVGPresentationContext& operator=(const SVGPresentationContext& other) = default;

  const std::unordered_map<std::string, SVGColorType>* _namedColors = nullptr;
  // Inherited presentation attributes, computed for the current node.
  SVGPresentationAttributes _inherited;
};

class SVGRenderContext {
 public:
  // Captures data required for object bounding box resolution.
  struct OBBScope {
    const SVGNode* node;
    const SVGRenderContext* context;
  };

  SVGRenderContext(Canvas* canvas, const std::shared_ptr<TextShaper>& textShaper,
                   const SVGIDMapper& mapper, const SVGLengthContext& lengthContext,
                   const SVGPresentationContext& presentContext, const OBBScope& scope,
                   const Matrix& matrix);
  SVGRenderContext(const SVGRenderContext& other);
  SVGRenderContext(const SVGRenderContext& other, Canvas* canvas);
  SVGRenderContext(const SVGRenderContext& other, const SVGLengthContext& lengthContext);
  SVGRenderContext(const SVGRenderContext& other, Canvas* canvas,
                   const SVGLengthContext& lengthContext);
  // Establish a new OBB scope.  Normally used when entering a node's render scope.
  SVGRenderContext(const SVGRenderContext& other, const SVGNode* node);
  ~SVGRenderContext();

  static SVGRenderContext CopyForPaint(const SVGRenderContext& context, Canvas* canvas,
                                       const SVGLengthContext& lengthContext);

  const SVGLengthContext& lengthContext() const {
    return *_lengthContext;
  }

  SVGLengthContext* writableLengthContext() {
    return _lengthContext.writable();
  }

  const SVGPresentationContext& presentationContext() const {
    return *_presentationContext;
  }

  Canvas* canvas() const {
    return _canvas;
  }

  void saveOnce();

  void concat(const Matrix& inputMatrix) {
    _matrix.preConcat(inputMatrix);
  }

  const Matrix& matrix() const {
    return _matrix;
  }

  enum class ApplyFlags {
    Leaf = 1 << 0,  // the target node doesn't have descendants
  };

  void applyPresentationAttributes(const SVGPresentationAttributes& attrs, uint32_t flags);

  // Note: the id->node association is cleared for the lifetime of the returned value
  // (effectively breaks reference cycles, assuming appropriate return value scoping).
  std::shared_ptr<SVGNode> findNodeById(const SVGIRI&) const;

  std::optional<Paint> fillPaint() const;

  std::optional<Paint> strokePaint() const;

  std::shared_ptr<PathEffect> strokePathEffect() const;

  SVGColorType resolveSVGColor(const SVGColor&) const;

  float resolveFontSize() const;

  std::shared_ptr<Typeface> resolveTypeface() const;

  // The local computed clip path (not inherited).
  Path clipPath() const {
    return _clipPath.value_or(Path());
  };

  const std::shared_ptr<TextShaper>& textShaper() const {
    return _textShaper;
  }

  // Returns the translate/scale transformation required to map into the current OBB scope,
  // with the specified units.
  struct OBBTransform {
    Point offset;
    Point scale;
  };

  OBBTransform transformForCurrentBoundBox(SVGObjectBoundingBoxUnits) const;

  Rect resolveOBBRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                      const SVGLength& h, SVGObjectBoundingBoxUnits unit) const;

  // Stack-only
  void* operator new(size_t) = delete;
  void* operator new(size_t, void*) = delete;
  SVGRenderContext& operator=(const SVGRenderContext&) = delete;

 private:
  float applyOpacity(float opacity, uint32_t flags, bool hasFilter);
  std::shared_ptr<ImageFilter> applyFilter(const SVGFuncIRI& filter) const;
  Path applyClip(const SVGFuncIRI& clip) const;
  std::shared_ptr<MaskFilter> applyMask(const SVGFuncIRI& mask);

  std::optional<Paint> commonPaint(const SVGPaint& paint, float opacity) const;

  std::shared_ptr<TextShaper> _textShaper;

  const SVGIDMapper& nodeIDMapper;
  CopyOnWrite<SVGLengthContext> _lengthContext;
  CopyOnWrite<SVGPresentationContext> _presentationContext;
  Canvas* _canvas;
  int canvasSaveCount = 0;

  // clipPath, if present for the current context (not inherited).
  std::optional<Path> _clipPath;

  // Deferred opacity optimization for leaf nodes.
  float deferredPaintOpacity = 1;

  // Current object bounding box scope.
  const OBBScope scope;

  Matrix _matrix = {};
};
}  // namespace tgfx
