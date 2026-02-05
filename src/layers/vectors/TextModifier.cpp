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

#include "tgfx/layers/vectors/TextModifier.h"
#include <cmath>
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

static void ApplySkew(Matrix* matrix, float skew, float skewAxis) {
  if (skew == 0.0f && skewAxis == 0.0f) {
    return;
  }
  auto u = cosf(skewAxis);
  auto v = sinf(skewAxis);
  Matrix temp = {};
  temp.setAll(u, -v, 0, v, u, 0);
  matrix->postConcat(temp);
  auto w = tanf(skew);
  temp.setAll(1, w, 0, 0, 1, 0);
  matrix->postConcat(temp);
  temp.setAll(u, v, 0, -v, u, 0);
  matrix->postConcat(temp);
}

static Color BlendColor(const Color& base, const Color& overlay, float factor) {
  if (factor <= 0.0f) {
    return base;
  }
  float blendFactor = overlay.alpha * factor;
  if (blendFactor <= 0.0f) {
    return base;
  }
  float newAlpha = base.alpha + (1.0f - base.alpha) * blendFactor;
  if (blendFactor >= 1.0f) {
    return Color{overlay.red, overlay.green, overlay.blue, newAlpha};
  }
  float baseWeight = base.alpha * (1.0f - blendFactor);
  return Color{(base.red * baseWeight + overlay.red * blendFactor) / newAlpha,
               (base.green * baseWeight + overlay.green * blendFactor) / newAlpha,
               (base.blue * baseWeight + overlay.blue * blendFactor) / newAlpha, newAlpha};
}

std::shared_ptr<TextModifier> TextModifier::Make() {
  return std::shared_ptr<TextModifier>(new TextModifier());
}

void TextModifier::setSelectors(std::vector<std::shared_ptr<TextSelector>> value) {
  for (const auto& owner : owners) {
    for (const auto& selector : _selectors) {
      DEBUG_ASSERT(selector != nullptr);
      selector->detachFromLayer(owner);
    }
    for (const auto& selector : value) {
      DEBUG_ASSERT(selector != nullptr);
      selector->attachToLayer(owner);
    }
  }
  _selectors = std::move(value);
  invalidateContent();
}

void TextModifier::attachToLayer(Layer* layer) {
  VectorElement::attachToLayer(layer);
  for (const auto& selector : _selectors) {
    DEBUG_ASSERT(selector != nullptr);
    selector->attachToLayer(layer);
  }
}

void TextModifier::detachFromLayer(Layer* layer) {
  for (const auto& selector : _selectors) {
    DEBUG_ASSERT(selector != nullptr);
    selector->detachFromLayer(layer);
  }
  VectorElement::detachFromLayer(layer);
}

void TextModifier::setAnchor(Point value) {
  if (_anchor == value) {
    return;
  }
  _anchor = value;
  invalidateContent();
}

void TextModifier::setPosition(Point value) {
  if (_position == value) {
    return;
  }
  _position = value;
  invalidateContent();
}

void TextModifier::setScale(Point value) {
  if (_scale == value) {
    return;
  }
  _scale = value;
  invalidateContent();
}

void TextModifier::setSkew(float value) {
  if (_skew == value) {
    return;
  }
  _skew = value;
  invalidateContent();
}

void TextModifier::setSkewAxis(float value) {
  if (_skewAxis == value) {
    return;
  }
  _skewAxis = value;
  invalidateContent();
}

void TextModifier::setRotation(float value) {
  if (_rotation == value) {
    return;
  }
  _rotation = value;
  invalidateContent();
}

void TextModifier::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateContent();
}

void TextModifier::setFillColor(std::optional<Color> value) {
  if (_fillColor == value) {
    return;
  }
  _fillColor = value;
  invalidateContent();
}

void TextModifier::setStrokeColor(std::optional<Color> value) {
  if (_strokeColor == value) {
    return;
  }
  _strokeColor = value;
  invalidateContent();
}

void TextModifier::setStrokeWidth(std::optional<float> value) {
  if (_strokeWidth == value) {
    return;
  }
  _strokeWidth = value;
  invalidateContent();
}

void TextModifier::apply(VectorContext* context) {
  auto glyphGeometries = context->getGlyphGeometries();
  if (glyphGeometries.empty()) {
    return;
  }

  size_t totalCount = 0;
  for (auto* geometry : glyphGeometries) {
    totalCount += geometry->glyphs.size();
  }
  if (totalCount == 0) {
    return;
  }

  size_t globalIndex = 0;
  for (auto* geometry : glyphGeometries) {
    const auto& anchors = geometry->anchors;
    bool hasAnchors = !anchors.empty();
    size_t glyphIndex = 0;
    for (auto& glyph : geometry->glyphs) {
      float factor = TextSelector::CalculateCombinedFactor(_selectors, globalIndex, totalCount);
      if (factor == 0.0f) {
        globalIndex++;
        glyphIndex++;
        continue;
      }

      float absFactor = std::abs(factor);

      // Calculate the default anchor point: half of advance width
      float defaultAnchorX = glyph.font.getAdvance(glyph.glyphID) * 0.5f;

      // Get glyph anchor from geometry's anchors array
      Point glyphAnchor = hasAnchors ? anchors[glyphIndex] : Point::Zero();

      // Total anchor point = default anchor + glyph anchor + user-specified anchor offset (scaled by factor)
      float totalAnchorX = defaultAnchorX + glyphAnchor.x + _anchor.x * factor;
      float totalAnchorY = glyphAnchor.y + _anchor.y * factor;

      // Apply transform: anchor -> scale -> skew -> rotation -> anchor restore -> position
      Matrix transform = Matrix::I();
      transform.postTranslate(-totalAnchorX, -totalAnchorY);

      float scaleX = (_scale.x - 1.0f) * factor + 1.0f;
      float scaleY = (_scale.y - 1.0f) * factor + 1.0f;
      transform.postScale(scaleX, scaleY);

      if (_skew != 0.0f) {
        ApplySkew(&transform, DegreesToRadians(-_skew * factor), DegreesToRadians(_skewAxis));
      }

      transform.postRotate(_rotation * factor);
      transform.postTranslate(totalAnchorX, totalAnchorY);
      transform.postTranslate(_position.x * factor, _position.y * factor);

      glyph.matrix.preConcat(transform);

      // Apply alpha
      float alphaFactor = (_alpha - 1.0f) * absFactor + 1.0f;
      glyph.style.alpha *= std::max(0.0f, alphaFactor);

      // Apply fill color override
      if (_fillColor.has_value()) {
        glyph.style.fillColor = BlendColor(glyph.style.fillColor, *_fillColor, absFactor);
      }

      // Apply stroke color override
      if (_strokeColor.has_value()) {
        glyph.style.strokeColor = BlendColor(glyph.style.strokeColor, *_strokeColor, absFactor);
      }

      // Apply stroke width override
      if (_strokeWidth.has_value()) {
        float newFactor =
            glyph.style.strokeWidthFactor + (1.0f - glyph.style.strokeWidthFactor) * absFactor;
        if (newFactor > 0.0f) {
          float newWidth = glyph.style.strokeWidth +
                           (*_strokeWidth - glyph.style.strokeWidth) * absFactor / newFactor;
          glyph.style.strokeWidth = newWidth;
          glyph.style.strokeWidthFactor = newFactor;
        }
      }

      globalIndex++;
      glyphIndex++;
    }
  }
}

}  // namespace tgfx
