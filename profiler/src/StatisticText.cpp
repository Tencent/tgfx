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

#include "StatisticText.h"
#include <QQuickWindow>
#include <QSGImageNode>
#include "Utility.h"

StatisticsText::StatisticsText(QQuickItem* parent)
    : QQuickItem(parent), appHost(AppHostInstance::GetAppHostInstance()) {
  setFlag(ItemHasContents, true);
}

StatisticsText::~StatisticsText() = default;

tgfx::Rect StatisticsText::getTextBounds(const QString& text) const {
  if (text.isEmpty() || !appHost) {
    return tgfx::Rect::MakeEmpty();
  }

  std::string utf8Text = text.toStdString();
  auto bounds = getTextSize(appHost.get(), utf8Text.c_str(), utf8Text.size());
  return bounds;
}

void StatisticsText::setText(const QString& text) {
  if (sText != text) {
    sText = text;
    dirty = true;
    Q_EMIT textChanged();
    update();
  }
}

void StatisticsText::setColor(const QColor& color) {
  if (sColor != color) {
    sColor = color;
    Q_EMIT scolorChanged();
    update();
  }
}

void StatisticsText::setContrast(bool contrast) {
  if (sContrast != contrast) {
    sContrast = contrast;
    Q_EMIT contrastChanged();
    update();
  }
}

void StatisticsText::setAlignment(Qt::Alignment alignment) {
  if (sAlignment != alignment) {
    sAlignment = alignment;
    Q_EMIT alignmentChanged();
    update();
  }
}

void StatisticsText::setElideMode(int elideMode) {
  if (sElideMode != elideMode) {
    sElideMode = elideMode;
    Q_EMIT elideModeChanged();
    update();
  }
}

void StatisticsText::draw() {
  if (!tgfxWindow || !appHost || !dirty) {
    return;
  }

  auto device = tgfxWindow->getDevice();
  if (device == nullptr) {
    return;
  }

  auto context = device->lockContext();
  if (context == nullptr) {
    device->unlock();
    return;
  }

  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear(tgfx::Color::Transparent());
  canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
  drawStext(canvas);
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
  dirty = false;
}

void StatisticsText::drawStext(tgfx::Canvas* canvas) {
  if (!sText.isEmpty()) {
    QString displayText = sText;
    if (sElideMode != Qt::ElideNone) {
      displayText = elideText(sText, static_cast<float>(width()));
    }

    float x = 0.0f;
    float y = 0.0f;

    auto textBounds = getTextBounds(displayText);

    if (sAlignment & Qt::AlignHCenter) {
      float textWidth = textBounds.width();
      x = static_cast<float>((width() - textWidth)) / 2.0f - textBounds.left;
    } else if (sAlignment & Qt::AlignRight) {
      x = static_cast<float>(width() - textBounds.right - textBounds.right);
    } else {
      x = -textBounds.left;
    }

    if (sAlignment & Qt::AlignVCenter) {
      float textHeight = textBounds.height();
      y = static_cast<float>((height() - textHeight) / 2.0f - textBounds.top);
    } else if (sAlignment & Qt::AlignBottom) {
      y = static_cast<float>(height() - textBounds.bottom - textBounds.top);
    } else {
      y = -textBounds.top;
    }

    std::string utf8Text = displayText.toStdString();
    uint32_t color32 = colorToUint32(sColor);

    if (sContrast) {
      drawTextContrast(canvas, appHost.get(), x, y, color32, utf8Text.c_str());
    } else {
      drawText(canvas, appHost.get(), utf8Text, x, y, color32);
    }
  }
}

QSGNode* StatisticsText::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto node = dynamic_cast<QSGImageNode*>(oldNode);
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
  }
  auto pixelRatio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
  auto screenHeight = static_cast<int>(ceil(height() * pixelRatio));

  auto sizeChanged =
      appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRatio));
  if (sizeChanged) {
    tgfxWindow->invalidSize();
  }
  draw();
  auto texture = tgfxWindow->getQSGTexture();
  if (texture) {
    if (node == nullptr) {
      node = window()->createImageNode();
    }
    node->setTexture(texture);
    node->markDirty(QSGNode::DirtyMaterial);
    node->setRect(boundingRect());
  }
  return node;
}

void StatisticsText::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size()) {
    sGeometryChanged = false;
    update();
  }
}

QString StatisticsText::elideText(const QString& text, float maxWidth) {
  if (text.isEmpty() || sElideMode == Qt::ElideNone || maxWidth <= 0 || !appHost) {
    return text;
  }

  auto bounds = getTextBounds(text);
  if (bounds.width() <= maxWidth) {
    return text;
  }

  QString result = text;
  const QString ellipsis = "...";

  switch (sElideMode) {
    case Qt::ElideRight: {
      int length = static_cast<int>(text.length());
      while (length > 1) {
        length--;
        result = text.left(length) + ellipsis;
        bounds = getTextBounds(result);
        if (bounds.width() <= maxWidth) {
          break;
        }
      }
      break;
    }
    case Qt::ElideLeft: {
      int length = static_cast<int>(text.length());
      while (length > 1) {
        length--;
        result = ellipsis + text.right(length);
        bounds = getTextBounds(result);
        if (bounds.width() <= maxWidth) {
          break;
        }
      }
      break;
    }
    case Qt::ElideMiddle: {
      int halfLength = static_cast<int>(text.length()) / 2;
      int leftLength = halfLength;
      int rightLength = static_cast<int>(text.length()) - halfLength;

      while (leftLength > 0 && rightLength > 0) {
        result = text.left(leftLength) + ellipsis + text.right(rightLength);
        bounds = getTextBounds(result);
        if (bounds.width() <= maxWidth) {
          break;
        }
        if (leftLength > rightLength) {
          leftLength--;
        } else {
          rightLength--;
        }
      }
      break;
    }
  }
  return result;
}

uint32_t StatisticsText::colorToUint32(const QColor& color) const {
  uint8_t a = static_cast<uint8_t>(color.alpha());
  uint8_t r = static_cast<uint8_t>(color.red());
  uint8_t g = static_cast<uint8_t>(color.green());
  uint8_t b = static_cast<uint8_t>(color.blue());
  return static_cast<uint32_t>(a << 24) | static_cast<uint32_t>(b << 16) |
         static_cast<uint32_t>(g << 8) | static_cast<uint32_t>(r);
}
