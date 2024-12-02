/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <tuple>
#include <unordered_map>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/SVGIDMapper.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGNode;

template <typename T>
class CopyOnWrite {
 public:
  explicit CopyOnWrite(const T& initial) : _obj(&initial) {
  }

  explicit CopyOnWrite(const T* initial) : _obj(initial) {
  }

  // Constructor for delayed initialization.
  CopyOnWrite() : _obj(nullptr) {
  }

  CopyOnWrite(const CopyOnWrite& that) {
    *this = that;
  }

  CopyOnWrite(CopyOnWrite&& that) noexcept {
    *this = std::move(that);
  }

  CopyOnWrite& operator=(const CopyOnWrite& that) {
    _optional = that._optional;
    _obj = _optional.has_value() ? &_optional.value() : that._obj;
    return *this;
  }

  CopyOnWrite& operator=(CopyOnWrite&& that) {
    _optional = std::move(that._optional);
    _obj = _optional.has_value() ? &_optional.value() : that._obj;
    return *this;
  }

  /**
     * Returns a writable T*. The first time this is called the initial object is cloned.
     */
  T* writable() {
    if (!_optional.has_value()) {
      _optional = *_obj;
      _obj = &_optional.value();
    }
    return &_optional.value();
  }

  const T* get() const {
    return _obj;
  }

  /**
     * Operators for treating this as though it were a const pointer.
     */

  const T* operator->() const {
    return _obj;
  }

  const T& operator*() const {
    return *_obj;
  }

 private:
  const T* _obj;
  std::optional<T> _optional;
};

class SVGLengthContext {
 public:
  explicit SVGLengthContext(const Size& viewport, float dpi = 90) : _viewport(viewport), _dpi(dpi) {
  }

  enum class LengthType {
    Horizontal,
    Vertical,
    Other,
  };

  const Size& viewPort() const {
    return _viewport;
  }
  void setViewPort(const Size& viewport) {
    _viewport = viewport;
  }

  float resolve(const SVGLength&, LengthType) const;
  Rect resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                   const SVGLength& h) const;

  void setPatternUnits(SVGObjectBoundingBoxUnits unit) {
    _patternUnit = unit;
  }

  void clearPatternUnits() {
    _patternUnit.reset();
  }

  std::optional<SVGObjectBoundingBoxUnits> getPatternUnits() const {
    return _patternUnit;
  }

 private:
  Size _viewport;
  float _dpi;
  std::optional<SVGObjectBoundingBoxUnits> _patternUnit;
};

struct SkSVGPresentationContext {
  SkSVGPresentationContext();
  SkSVGPresentationContext(const SkSVGPresentationContext&) = default;
  SkSVGPresentationContext& operator=(const SkSVGPresentationContext&) = default;

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

  SVGRenderContext(Context*, Canvas*, const std::shared_ptr<SVGFontManager>&, const SVGIDMapper&,
                   const SVGLengthContext&, const SkSVGPresentationContext&, const OBBScope&,
                   const Matrix& matrix);
  SVGRenderContext(const SVGRenderContext&);
  SVGRenderContext(const SVGRenderContext&, Canvas*);
  SVGRenderContext(const SVGRenderContext&, const SVGLengthContext&);
  SVGRenderContext(const SVGRenderContext&, Canvas*, const SVGLengthContext&);
  // Establish a new OBB scope.  Normally used when entering a node's render scope.
  SVGRenderContext(const SVGRenderContext&, const SVGNode*);
  ~SVGRenderContext();

  static SVGRenderContext CopyForPaint(const SVGRenderContext&, Canvas*, const SVGLengthContext&);

  const SVGLengthContext& lengthContext() const {
    return *_lengthContext;
  }
  SVGLengthContext* writableLengthContext() {
    return _lengthContext.writable();
  }

  const SkSVGPresentationContext& presentationContext() const {
    return *_presentationContext;
  }

  Context* deviceContext() const {
    return _deviceContext;
  }

  Canvas* canvas() const {
    return _canvas;
  }
  void saveOnce();

  void concat(const Matrix& matrix) {
    _matrix.preConcat(matrix);
  }

  enum ApplyFlags {
    kLeaf = 1 << 0,  // the target node doesn't have descendants
  };
  void applyPresentationAttributes(const SVGPresentationAttributes& attrs, uint32_t flags);

  // Note: the id->node association is cleared for the lifetime of the returned value
  // (effectively breaks reference cycles, assuming appropriate return value scoping).
  std::shared_ptr<SVGNode> findNodeById(const SVGIRI&) const;

  std::optional<Paint> fillPaint() const;
  std::optional<Paint> strokePaint() const;

  SVGColorType resolveSvgColor(const SVGColor&) const;

  // The local computed clip path (not inherited).
  Path clipPath() const {
    return _clipPath.value_or(Path());
  };

  const std::shared_ptr<SVGFontManager>& fontMgr() const {
    return _fontMgr;
  }

  std::shared_ptr<SVGFontManager>& fontMgr() {
    // It is probably an oversight to try to render <text> without having set the SVGFontManager.
    // We will assert this in debug mode, but fallback to an empty _fontMgr in release builds.
    return _fontMgr;
  }

  // Returns the translate/scale transformation required to map into the current OBB scope,
  // with the specified units.
  struct OBBTransform {
    Point offset, scale;
  };

  OBBTransform transformForCurrentOBB(SVGObjectBoundingBoxUnits) const;

  Rect resolveOBBRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                      const SVGLength& h, SVGObjectBoundingBoxUnits unit) const;

  // Stack-only
  void* operator new(size_t) = delete;
  void* operator new(size_t, void*) = delete;
  SVGRenderContext& operator=(const SVGRenderContext&) = delete;

 private:
  float applyOpacity(float opacity, uint32_t flags, bool hasFilter);
  std::shared_ptr<ImageFilter> applyFilter(const SVGFuncIRI&);
  Path applyClip(const SVGFuncIRI&);
  std::shared_ptr<MaskFilter> applyMask(const SVGFuncIRI&);

  std::optional<Paint> commonPaint(const SVGPaint&, float opacity) const;

  std::shared_ptr<SVGFontManager> _fontMgr;
  const SVGIDMapper& _nodeIDMapper;
  CopyOnWrite<SVGLengthContext> _lengthContext;
  CopyOnWrite<SkSVGPresentationContext> _presentationContext;
  Canvas* _renderCanvas;

  Recorder _recorder;
  Canvas* _canvas;

  // clipPath, if present for the current context (not inherited).
  std::optional<Path> _clipPath;

  // Deferred opacity optimization for leaf nodes.
  float _deferredPaintOpacity = 1;

  // Current object bounding box scope.
  const OBBScope _scope;

  Context* _deviceContext;
  Paint _picturePaint;

  Matrix _matrix = Matrix::I();
};
}  // namespace tgfx
