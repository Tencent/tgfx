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
#include <unordered_map>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/SVGIDMapper.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

// class SkCanvas;
// class SkPaint;
// class SkString;
// namespace skresources {
// class ResourceProvider;
// }

class SVGNode;

template <typename T>
class CopyOnWrite {
 public:
  explicit CopyOnWrite(const T& initial) : fObj(&initial) {
  }

  explicit CopyOnWrite(const T* initial) : fObj(initial) {
  }

  // Constructor for delayed initialization.
  CopyOnWrite() : fObj(nullptr) {
  }

  CopyOnWrite(const CopyOnWrite& that) {
    *this = that;
  }

  CopyOnWrite(CopyOnWrite&& that) noexcept {
    *this = std::move(that);
  }

  CopyOnWrite& operator=(const CopyOnWrite& that) {
    fLazy = that.fLazy;
    fObj = fLazy.has_value() ? &fLazy.value() : that.fObj;
    return *this;
  }

  CopyOnWrite& operator=(CopyOnWrite&& that) {
    fLazy = std::move(that.fLazy);
    fObj = fLazy.has_value() ? &fLazy.value() : that.fObj;
    return *this;
  }

  // Should only be called once, and only if the default constructor was used.
  void init(const T& initial) {
    SkASSERT(!fObj);
    SkASSERT(!fLazy.has_value());
    fObj = &initial;
  }

  // If not already initialized, in-place instantiates the writable object
  template <typename... Args>
  void initIfNeeded(Args&&... args) {
    if (!fObj) {
      SkASSERT(!fLazy.has_value());
      fObj = &fLazy.emplace(std::forward<Args>(args)...);
    }
  }

  /**
     * Returns a writable T*. The first time this is called the initial object is cloned.
     */
  T* writable() {
    if (!fLazy.has_value()) {
      fLazy = *fObj;
      fObj = &fLazy.value();
    }
    return &fLazy.value();
  }

  const T* get() const {
    return fObj;
  }

  /**
     * Operators for treating this as though it were a const pointer.
     */

  const T* operator->() const {
    return fObj;
  }

  operator const T*() const {
    return fObj;
  }

  const T& operator*() const {
    return *fObj;
  }

 private:
  const T* fObj;
  std::optional<T> fLazy;
};

class SVGLengthContext {
 public:
  explicit SVGLengthContext(const Size& viewport, float dpi = 90) : fViewport(viewport), fDPI(dpi) {
  }

  enum class LengthType {
    kHorizontal,
    kVertical,
    kOther,
  };

  const Size& viewPort() const {
    return fViewport;
  }
  void setViewPort(const Size& viewport) {
    fViewport = viewport;
  }

  float resolve(const SVGLength&, LengthType) const;
  Rect resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                   const SVGLength& h) const;

  void setPatternUnits(SVGPatternUnits unit) {
    fPatternUnit = unit;
  }

  void clearPatternUnits() {
    fPatternUnit.reset();
  }

  std::optional<SVGPatternUnits> getPatternUnits() const {
    return fPatternUnit;
  }

 private:
  Size fViewport;
  float fDPI;
  std::optional<SVGPatternUnits> fPatternUnit;
};

struct SkSVGPresentationContext {
  SkSVGPresentationContext();
  SkSVGPresentationContext(const SkSVGPresentationContext&) = default;
  SkSVGPresentationContext& operator=(const SkSVGPresentationContext&) = default;

  const std::unordered_map<std::string, SVGColorType>* fNamedColors = nullptr;

  // Inherited presentation attributes, computed for the current node.
  SkSVGPresentationAttributes fInherited;
};

class SVGRenderContext {
 public:
  // Captures data required for object bounding box resolution.
  struct OBBScope {
    const SVGNode* fNode;
    const SVGRenderContext* fCtx;
  };

  SVGRenderContext(Canvas*, const std::shared_ptr<SVGFontManager>&,
                   const std::shared_ptr<ResourceProvider>&, const SVGIDMapper&,
                   const SVGLengthContext&, const SkSVGPresentationContext&, const OBBScope&);
  SVGRenderContext(const SVGRenderContext&);
  SVGRenderContext(const SVGRenderContext&, Canvas*);
  SVGRenderContext(const SVGRenderContext&, const SVGLengthContext&);
  SVGRenderContext(const SVGRenderContext&, Canvas*, const SVGLengthContext&);
  // Establish a new OBB scope.  Normally used when entering a node's render scope.
  SVGRenderContext(const SVGRenderContext&, const SVGNode*);
  ~SVGRenderContext();

  const SVGLengthContext& lengthContext() const {
    return *fLengthContext;
  }
  SVGLengthContext* writableLengthContext() {
    return fLengthContext.writable();
  }

  const SkSVGPresentationContext& presentationContext() const {
    return *fPresentationContext;
  }

  Canvas* canvas() const {
    return fCanvas;
  }
  void saveOnce();

  enum ApplyFlags {
    kLeaf = 1 << 0,  // the target node doesn't have descendants
  };
  void applyPresentationAttributes(const SkSVGPresentationAttributes&, uint32_t flags);

  // Scoped wrapper that temporarily clears the original node reference.
  // class BorrowedNode {
  //  public:
  //   explicit BorrowedNode(std::shared_ptr<SVGNode>* node) : fOwner(node) {
  //     if (fOwner) {
  //       fBorrowed = std::move(*fOwner);
  //       *fOwner = nullptr;
  //     }
  //   }

  //   ~BorrowedNode() {
  //     if (fOwner) {
  //       *fOwner = std::move(fBorrowed);
  //     }
  //   }

  //   const SVGNode* get() const {
  //     return fBorrowed.get();
  //   }
  //   const SVGNode* operator->() const {
  //     return fBorrowed.get();
  //   }
  //   const SVGNode& operator*() const {
  //     return *fBorrowed;
  //   }

  //   explicit operator bool() const {
  //     return !!fBorrowed;
  //   }

  //   // noncopyable
  //   BorrowedNode(const BorrowedNode&) = delete;
  //   BorrowedNode& operator=(BorrowedNode&) = delete;

  //  private:
  //   std::shared_ptr<SVGNode>* fOwner;
  //   std::shared_ptr<SVGNode> fBorrowed;
  // };

  // Note: the id->node association is cleared for the lifetime of the returned value
  // (effectively breaks reference cycles, assuming appropriate return value scoping).
  std::shared_ptr<SVGNode> findNodeById(const SVGIRI&) const;

  std::optional<Paint> fillPaint() const;
  std::optional<Paint> strokePaint() const;

  SVGColorType resolveSvgColor(const SVGColor&) const;

  // The local computed clip path (not inherited).
  Path clipPath() const {
    return fClipPath.value_or(Path());
  }

  const std::shared_ptr<ResourceProvider>& resourceProvider() const {
    return fResourceProvider;
  }

  const std::shared_ptr<SVGFontManager>& fontMgr() const {
    return fFontMgr;
  }

  std::shared_ptr<SVGFontManager>& fontMgr() {
    // It is probably an oversight to try to render <text> without having set the SkFontMgr.
    // We will assert this in debug mode, but fallback to an empty fontmgr in release builds.
    return fFontMgr;
  }

  // Returns the translate/scale transformation required to map into the current OBB scope,
  // with the specified units.
  struct OBBTransform {
    Point offset, scale;
  };

  OBBTransform transformForCurrentOBB(SVGObjectBoundingBoxUnits) const;

  Rect resolveOBBRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                      const SVGLength& h, SVGObjectBoundingBoxUnits) const;

  //   std::unique_ptr<SkShaper> makeShaper() const {
  //     SkASSERT(fTextShapingFactory);
  //     return fTextShapingFactory->makeShaper(this->fontMgr());
  //   }

  //   std::unique_ptr<SkShaper::BiDiRunIterator> makeBidiRunIterator(const char* utf8, size_t utf8Bytes,
  //                                                                  uint8_t bidiLevel) const {
  //     SkASSERT(fTextShapingFactory);
  //     return fTextShapingFactory->makeBidiRunIterator(utf8, utf8Bytes, bidiLevel);
  //   }

  //   std::unique_ptr<SkShaper::ScriptRunIterator> makeScriptRunIterator(const char* utf8,
  //                                                                      size_t utf8Bytes) const {
  //     SkASSERT(fTextShapingFactory);
  //     constexpr SkFourByteTag unknownScript = SkSetFourByteTag('Z', 'z', 'z', 'z');
  //     return fTextShapingFactory->makeScriptRunIterator(utf8, utf8Bytes, unknownScript);
  //   }

 private:
  // Stack-only
  void* operator new(size_t) = delete;
  void* operator new(size_t, void*) = delete;
  SVGRenderContext& operator=(const SVGRenderContext&) = delete;

  void applyOpacity(float opacity, uint32_t flags, bool hasFilter);
  void applyFilter(const SVGFuncIRI&);
  void applyClip(const SVGFuncIRI&);
  void applyMask(const SVGFuncIRI&);

  std::optional<Paint> commonPaint(const SVGPaint&, float opacity) const;

  std::shared_ptr<SVGFontManager> fFontMgr;
  // const sk_sp<SkShapers::Factory>& fTextShapingFactory;
  const std::shared_ptr<ResourceProvider>& fResourceProvider;

  const SVGIDMapper& fIDMapper;
  CopyOnWrite<SVGLengthContext> fLengthContext;
  CopyOnWrite<SkSVGPresentationContext> fPresentationContext;
  Canvas* fCanvas;
  // The save count on 'fCanvas' at construction time.
  // A restoreToCount() will be issued on destruction.
  size_t fCanvasSaveCount;

  // clipPath, if present for the current context (not inherited).
  std::optional<Path> fClipPath;

  // Deferred opacity optimization for leaf nodes.
  float fDeferredPaintOpacity = 1;

  // Current object bounding box scope.
  const OBBScope fOBBScope;
};
}  // namespace tgfx
