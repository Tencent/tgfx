/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "AtlasSource.h"

#include "core/PixelBuffer.h"
#include "core/atlas/AtlasManager.h"
#include "core/utils/MathExtra.h"
#include "gpu/Quad.h"
#include "tgfx/core/Buffer.h"
#include "utils/PlacementBuffer.h"
#include "vectors/freetype/FTMask.h"

namespace tgfx {
static void computeAtlasKey(GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                            BytesKey& key) {
  float fontSize = 0.f;
  uint32_t typeFaceID = 0;
  float strokeWidth = 0.f;
  bool fauxBold = false;
  bool fauxItalic = false;
  if (stroke != nullptr) {
    strokeWidth = stroke->width;
  }
  Font font;
  if (glyphFace->asFont(&font)) {
    fontSize = font.getSize();
    typeFaceID = font.getTypeface()->uniqueID();
    fauxBold = font.isFauxBold();
    fauxItalic = font.isFauxItalic();
  }
  key.write(fontSize);
  key.write(typeFaceID);
  key.write(strokeWidth);
  key.write(fauxBold);
  key.write(fauxItalic);//matrix
  key.write(glyphID);
}

AtlasSource::AtlasSource(AtlasManager* atlasManager, std::shared_ptr<GlyphRunList> glyphRunList,
                         const Matrix& viewMatrix, const Stroke* _stroke)
    : atlasManager(atlasManager), viewMatrix(viewMatrix),
      glyphRunList(std::move(glyphRunList)) {
  if (_stroke) {
    stroke = std::make_shared<Stroke>(*_stroke);
  }
  computeAtlasLocator();
}

std::shared_ptr<AtlasBuffer> AtlasSource::getData() const {
  //auto aaType = AAType::Coverage;
  for (auto& [_, pageGlyphMap] : drawGlyphs) {
    for (auto& [_, drawGlyphArray] : pageGlyphMap) {
      for (auto& drawGlyph : drawGlyphArray) {
        std::shared_ptr<ImageBuffer> imageBuffer;
        if (stroke == nullptr) {
          imageBuffer = drawGlyph->glyphFace->generateImage(drawGlyph->glyphId);
        }else {
          auto width = static_cast<int>(drawGlyph->locator.getLocation().width());
          auto height = static_cast<int>(drawGlyph->locator.getLocation().height());
          auto mask = Mask::Make(width, height,true);
          if (!mask) {
            continue;;
          }
          mask->setAntiAlias(true);
          Path path;
          if (!drawGlyph->glyphFace->getPath(drawGlyph->glyphId, &path)) {
            continue;
          }
          auto scale = viewMatrix.getMaxScale();
          auto scaleMatrix = Matrix::MakeScale(1.0f/ scale,1.0f/ scale);
          path.transform(scaleMatrix);
          auto glyphBounds = drawGlyph->glyphFace->getBounds(drawGlyph->glyphId);
          glyphBounds.scale(1.f / scale, 1.f / scale);
          stroke->applyToBounds(&glyphBounds);
          glyphBounds.scale(scale, scale);
          auto offsetX = -glyphBounds.x();
          auto offsetY = -glyphBounds.y();

          auto rasterizerMatrix = Matrix::MakeScale(scale,scale);
          rasterizerMatrix.postTranslate(offsetX,offsetY);

          mask->setMatrix(rasterizerMatrix);
          mask->fillPath(path, stroke.get());
          imageBuffer =  mask->makeBuffer();
        }

        if (imageBuffer == nullptr) {
          continue;
        }
        auto pixelBuffer = std::static_pointer_cast<PixelBuffer>(imageBuffer);
        if (pixelBuffer == nullptr) {
          continue;
        }
        atlasManager->fillGlyphImage(drawGlyph->maskFormat, drawGlyph->locator,
                                     pixelBuffer->lockPixels());
        pixelBuffer->unlockPixels();
      }
    }
  }
  return AtlasBuffer::MakeFrom(makeGeometries());
}

void AtlasSource::computeAtlasLocator() {
  auto scale = viewMatrix.getMaxScale();
  auto hasScale = !FloatNearlyEqual(scale, 1.0f);
  for (auto& run : glyphRunList->glyphRuns()) {
    auto glyphFace = run.glyphFace;
    if (hasScale) {
      // Scale the glyphs before measuring to prevent precision loss with small font sizes.
      glyphFace = glyphFace->makeScaled(scale);
      DEBUG_ASSERT(glyphFace != nullptr);
    }
    size_t index = 0;
    for (auto& glyphID : run.glyphs) {
      auto bounds = glyphFace->getBounds(glyphID);
      auto originBounds = bounds;
      auto positionOffsetX = 0.f;
      auto positionOffsetY = 0.f;
      if (stroke != nullptr) {
        bounds.scale(1.f / scale, 1.f / scale);
        stroke->applyToBounds(&bounds);
        bounds.scale(scale, scale);
        positionOffsetX = (bounds.x() - originBounds.x()) / scale;
        positionOffsetY = (bounds.y() - originBounds.y()) / scale;
      }

      bounds.roundOut();

      auto drawGlyph = atlasManager->getContext()->drawingBuffer()->make<DrawGlyph>();
      drawGlyph->position = run.positions[index];
      drawGlyph->position.offset(positionOffsetX, positionOffsetY);
      drawGlyph->glyphFace = glyphFace;
      drawGlyph->glyphId = glyphID;

      BytesKey glyphKey;
      computeAtlasKey(glyphFace.get(), glyphID, stroke.get(), glyphKey);
      auto maskFormat = glyphFace->hasColor() ? MaskFormat::RGBA : MaskFormat::A8;
      drawGlyph->maskFormat = maskFormat;
      unsigned numPage = 0;
      if (atlasManager->getTextureProxy(maskFormat, &numPage) == nullptr) {
        continue;
      }

      if (!atlasManager->hasGlyph(maskFormat, glyphKey)) {
        auto glyph = atlasManager->glyphCacheBuffer()->make<Glyph>(glyphKey);
        glyph->_maskFormat = maskFormat;
        glyph->_glyphId = glyphID;
        glyph->_width = static_cast<int>(bounds.width());
        glyph->_height = static_cast<int>(bounds.height());
        if (std::max(glyph->width(), glyph->height()) < 256) {
          atlasManager->addGlyphToAtlasWithoutFillImage(std::move(glyph));
          AtlasLocator locator;
          if (atlasManager->getGlyphLocator(maskFormat, glyphKey, locator)) {
            drawGlyph->locator = locator;
          }
          drawGlyphs[maskFormat][locator.pageIndex()].push_back(std::move(drawGlyph));
        } else {
          LOGE("glyph too large ");
        }
      } else {
        AtlasLocator locator;
        if (atlasManager->getGlyphLocator(maskFormat, glyphKey, locator)) {
          drawGlyph->locator = locator;
          drawGlyphs[maskFormat][locator.pageIndex()].push_back(std::move(drawGlyph));
        }
      }
      ++index;
    }
  }
}

std::vector<AtlasGeometryData> AtlasSource::makeGeometries() const {
  auto scale = viewMatrix.getMaxScale();
  std::vector<AtlasGeometryData> geometries;
  for (auto& [maskFormat, maskDrawGlyphs] : drawGlyphs) {
    for (auto& [pageIndex, glyphArray] : maskDrawGlyphs) {
      size_t perVertexCount = 4;
      auto floatCount = glyphArray.size() * perVertexCount * 4;
      Buffer buffer(floatCount * sizeof(float));
      auto vertices = reinterpret_cast<float*>(buffer.data());
      auto index = 0;
      for (auto& drawGlyph : glyphArray) {
        auto glyphBounds = drawGlyph->glyphFace->getBounds(drawGlyph->glyphId);
        auto rect = drawGlyph->locator.getLocation();
        auto matrix = Matrix::MakeTrans(glyphBounds.x(), glyphBounds.y());
        matrix.postScale(1.f/scale,1.f/scale);
        matrix.postTranslate(drawGlyph->position.x, drawGlyph->position.y);
        matrix.postConcat(viewMatrix);
        matrix.preTranslate(-rect.x(),-rect.y());
        auto quad = Quad::MakeFrom(rect, &matrix);
        auto uvQuad = Quad::MakeFrom(rect);
        for (size_t j = 4; j >= 1; --j) {
          vertices[index++] = quad.point(j - 1).x;
          vertices[index++] = quad.point(j - 1).y;
          vertices[index++] = uvQuad.point(j - 1).x;
          vertices[index++] = uvQuad.point(j - 1).y;
        }
      }

      size_t patternSize = 6;
      auto reps = glyphArray.size();
      auto size = static_cast<size_t>(reps * patternSize * sizeof(uint16_t));
      static uint16_t pattern[] = {0, 1, 2, 2, 1, 3};
      Buffer indexBuffer(size);
      uint16_t vertCount = 4;
      auto* data = reinterpret_cast<uint16_t*>(indexBuffer.data());
      for (uint16_t i = 0; i < reps; ++i) {
        uint16_t baseIndex = static_cast<uint16_t>(i * patternSize);
        auto baseVert = static_cast<uint16_t>(i * vertCount);
        for (uint16_t j = 0; j < patternSize; ++j) {
          data[baseIndex + j] = baseVert + pattern[j];
        }
      }

      geometries.push_back({maskFormat, pageIndex, buffer.release(), indexBuffer.release()});
    }
  }
  return geometries;
}

}  // namespace tgfx