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

#include "TableItem.h"
#include <QSGImageNode>
#include "Utility.h"

TableItem::TableItem(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setAcceptHoverEvents(true);
  setAcceptedMouseButtons(Qt::AllButtons);
  createAppHost();
}
TableItem::~TableItem() = default;

void TableItem::setModel(StatisticsModel* model) {
  if(tModel != model) {
    if(tModel) {
      disconnect(tModel, &StatisticsModel::statisticsUpdated, this, nullptr);
    }
    tModel = model;
    if(tModel) {
      connect(tModel, &StatisticsModel::statisticsUpdated, this, [this]() {
        update();
        Q_EMIT rowHeightChanged();
      });
    }
    Q_EMIT modelChanged();
    update();
  }
}

void TableItem::setRowHeight(int height) {
  if(rowHeight != height) {
    rowHeight = height;
    Q_EMIT rowHeightChanged();
    update();
  }
}

void TableItem::setScrollPosition(int position) {
  if(scrollPosition != position) {
    scrollPosition = position;
    Q_EMIT scrollPositionChanged();
    update();
  }
}
void TableItem::setNameColumnWidth(int width) {
  if(nameColumnWidth != width) {
    nameColumnWidth = width;
    Q_EMIT nameColumnWidthChanged();
    update();
  }
}

void TableItem::setLocationColumnWidth(int width) {
  if(locationColumnWidth != width) {
    locationColumnWidth = width;
    Q_EMIT locationColumnWidthChanged();
    update();
  }
}

void TableItem::setTotalTimeWidth(int width) {
  if(totalTimeWidth != width) {
    totalTimeWidth = width;
    Q_EMIT totalTimeWidthChanged();
    update();
  }
}

void TableItem::setCountWidth(int width) {
  if(countWidth != width) {
    countWidth = width;
    Q_EMIT countWidthChanged();
    update();
  }
}

void TableItem::setMtpcWidth(int width) {
  if(mtpcWidth != width) {
    mtpcWidth = width;
    Q_EMIT mtpcWidthChanged();
    update();
  }
}

void TableItem::setThreadsWidth(int width) {
  if (threadsWidth != width) {
    threadsWidth = width;
    Q_EMIT threadsWidthChanged();
    update();
  }
}

void TableItem::setSortColumn(int column) {
  if (sortColumn != column) {
    sortColumn = column;
    Q_EMIT sortColumnChanged();
    if (tModel) {
      tModel->sort(sortColumn, static_cast<Qt::SortOrder>(sortOrder));
      update();
    }
  }
}

void TableItem::setSortOrder(int order) {
  if(sortOrder != order) {
    sortOrder = order;
    Q_EMIT sortOrderChanged();
    if(tModel) {
      tModel->sort(sortColumn, static_cast<Qt::SortOrder>(sortOrder));
      update();
    }
  }
}

void TableItem::handleMouseDoubleClick(int x, int y) {
  if (!tModel) {
    return;
  }

  int row = firstVisibleCount + (y / rowHeight);
  if (row >= 0 && row < tModel->rowCount(QModelIndex())) {
    int totalPreviousWidth = nameColumnWidth;
    if (x > totalPreviousWidth && x < totalPreviousWidth + locationColumnWidth) {
      tModel->openSource(row);
    }
  }
}

QSGNode* TableItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
  auto node = dynamic_cast<QSGImageNode*>(oldNode);
  if (!tgfxWindow) {
    tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
  }
  auto pixelRatio = window()->devicePixelRatio();
  auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
  auto screenHeight = static_cast<int>(ceil(height() * pixelRatio));

  auto sizeChanged =
      appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRatio));
  if (sizeChanged || geometryChanged) {
    tgfxWindow->invalidSize();
    geometryChanged = false;
  }

  if(tModel) {
    int totalRows = tModel->rowCount(QModelIndex());
    visibleRowCount = std::min(totalRows, static_cast<int>(ceil(height() / rowHeight)));
    firstVisibleCount = std::min(scrollPosition / rowHeight, totalRows - visibleRowCount > 0 ? totalRows - visibleRowCount : 0);
    Q_EMIT visibleRowCountChanged(visibleRowCount);
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

void TableItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size()) {
    geometryChanged = true;
    update();
  }
}

void TableItem::createAppHost() {
  appHost = std::make_unique<AppHost>();
#ifdef __APPLE__
  auto defaultTypeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
  auto emojiTypeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
#else
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
#endif
  appHost->addTypeface("default", defaultTypeface);
}

void TableItem::draw() {
  if (!appHost || !tgfxWindow) {
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

  drawTable(canvas);

  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

void TableItem::drawTable(tgfx::Canvas* canvas) {
  if(!tModel) {
    return;
  }

  float y = 0;
  int rowCount = 0;
  for(int i = 0; i < visibleRowCount; ++i) {
    int rowIndex = firstVisibleCount + i;
    if(rowIndex < tModel->rowCount(QModelIndex())) {
      drawRow(canvas, rowIndex, y);
      rowCount++;
    }
    y += rowHeight;
  }

  if(rowCount == 0){
    return;
  }

  float lineHeight = rowCount * rowHeight;

  tgfx::Paint linePaint;
  linePaint.setColor(getTgfxColor(0xFF545454));
  linePaint.setStrokeWidth(2.0f);
  linePaint.setStyle(tgfx::PaintStyle::Stroke);

  float x = 0;

  x +=nameColumnWidth;
  canvas->drawLine(x, 0, x, lineHeight, linePaint);

  x += locationColumnWidth;
  canvas->drawLine(x, 0, x , lineHeight, linePaint);

  x += totalTimeWidth;
  canvas->drawLine(x, 0, x , lineHeight, linePaint);

  x += countWidth;
  canvas->drawLine(x, 0, x, lineHeight, linePaint);

  x += mtpcWidth;
  canvas->drawLine(x, 0, x, lineHeight, linePaint);
}

void TableItem::drawRow(tgfx::Canvas* canvas, int rowIndex, float y) {
  tgfx::Paint backgroundPaint;
  backgroundPaint.setColor(getTgfxColor(0xFF2D2D2D));
  auto textRect = tgfx::Rect::MakeXYWH(0, (int)y, static_cast<int>(width()), static_cast<int>(y + rowHeight));
  canvas->drawRect(textRect, backgroundPaint);

  tgfx::Paint linePaint;
  linePaint.setColor(getTgfxColor(0xFF606060));
  canvas->drawLine(0, (float)(y + rowHeight - 1), static_cast<float>(width()), y + rowHeight - 1, linePaint);

  if(!tModel) {
    return;
  }

  QModelIndex index = tModel->index(rowIndex, 0);
  if(!index.isValid()) {
    return;
  }

  float x = 0;
  QColor textColor = QColor(255, 255, 255);
  QColor statusColor = index.data(StatisticsModel::colorRole).value<QColor>();
  uint32_t color32 = ((uint32_t)(statusColor.alpha()) << 24) |
                     ((uint32_t)(statusColor.blue()) << 16) |
                     ((uint32_t)(statusColor.green())<< 8) |
                     (uint32_t)(statusColor.red());
  tgfx::Paint statusPaint;
  statusPaint.setColor(getTgfxColor(color32));
  canvas->drawCircle(x + 16, y + rowHeight/2, 8, statusPaint);


  QString nameText = index.data(StatisticsModel::nameRole).toString();
  drawCell(canvas, nameText, x + 30, y - rowHeight/2 + 5, nameColumnWidth - 30, textColor, false, Qt::AlignLeft);
  x += nameColumnWidth;


  QString locationText = index.data(StatisticsModel::locationRole).toString();
  drawCell(canvas, locationText, x, y - rowHeight/2 + 5, locationColumnWidth, textColor, false, Qt::AlignLeft);
  x += locationColumnWidth;

  QString totalTimeText = index.data(StatisticsModel::totalTimeRole).toString();
  drawCell(canvas, totalTimeText, x, y - rowHeight/2 + 5, totalTimeWidth, textColor, false, Qt::AlignLeft);
  x += totalTimeWidth;


  QString countText = index.data(StatisticsModel::countRole).toString();
  drawCell(canvas, countText, x, y - rowHeight/2 + 5, countWidth, textColor, false, Qt::AlignLeft);
  x += countWidth;


  QString mtpcText = index.data(StatisticsModel::mtpcRole).toString();
  drawCell(canvas, mtpcText, x, y - rowHeight/2 + 5, mtpcWidth, textColor, false, Qt::AlignLeft);
  x += mtpcWidth;


  QString threadsText = index.data(StatisticsModel::threadCountRole).toString();
  drawCell(canvas, threadsText, x, y - rowHeight/2 + 5, threadsWidth, textColor, false, Qt::AlignLeft);

}

void TableItem::drawCell(tgfx::Canvas* canvas, const QString& text, float x, float y, float width,
                         const QColor& textColor, bool contrast, Qt::Alignment alignment) {
  if(text.isEmpty()) {
    return;
  }

  QString displayText = elideText(text, width - 16, Qt::ElideRight);
  std::string utf8Text = displayText.toStdString();

  float textX = x + 8;
  auto textBounds = getTextBounds(displayText);

  if(alignment & Qt::AlignHCenter) {
    textX = x + (width - textBounds.width()) / 2;
  }
  else if(alignment & Qt::AlignRight) {
    textX = x + width - textBounds.width() - 8;
  }

  float textY = y + (rowHeight + textBounds.height()) / 2 - textBounds.top;
  uint32_t color32 = ((uint32_t)(textColor.alpha()) << 24) |
                      ((uint32_t)(textColor.blue()) << 16) |
                        ((uint32_t)(textColor.green())<< 8) |
                          (uint32_t)(textColor.red());
  if(contrast) {
    drawTextContrast(canvas, appHost.get(), textX, textY, color32, utf8Text.c_str());
  }
  else {
    drawText(canvas, appHost.get(), utf8Text, textX, textY, color32);
  }
}

tgfx::Rect TableItem::getTextBounds(const QString& text) const {
  if(text.isEmpty() || !appHost) {
    return tgfx::Rect::MakeEmpty();
  }

  std::string utf8Text = text.toStdString();
  auto bounds = getTextSize(appHost.get(), utf8Text.c_str(), utf8Text.size());
  return bounds;
}

QString TableItem::elideText(const QString& text, float maxWidth,
                             Qt::TextElideMode elideMode) const {
  if (text.isEmpty() || elideMode == Qt::ElideNone || maxWidth <= 0 || !appHost) {
    return text;
  }

  auto bounds = getTextBounds(text);
  if (bounds.width() <= maxWidth) {
    return text;
  }

  QString result = text;
  const QString ellipsis = "...";
  switch (elideMode) {
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
    default:
      break;
  }
  return result;
}

