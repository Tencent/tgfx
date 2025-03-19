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

#pragma once

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"
#include "Utility.h"

class StatisticsText : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QString text READ getText WRITE setText NOTIFY textChanged)
  Q_PROPERTY(QColor color READ getColor WRITE setColor NOTIFY scolorChanged)
  Q_PROPERTY(bool contrast READ getContrast WRITE setContrast NOTIFY contrastChanged)
  Q_PROPERTY(Qt::Alignment alignment READ getAlignment WRITE setAlignment NOTIFY alignmentChanged)
  Q_PROPERTY(int elideMode READ getElideMode WRITE setElideMode NOTIFY elideModeChanged)

public:
  explicit StatisticsText(QQuickItem* parent = nullptr);
  ~StatisticsText() override;

  QString getText() const {return sText;}
  QColor getColor() const {return sColor;}
  bool getContrast() const {return sContrast;}
  Qt::Alignment getAlignment() const {return sAlignment;}
  int getElideMode() const {return sElideMode;}

  void setText(const QString& text);
  void setColor(const QColor& color);
  void setContrast(bool contrast);
  void setAlignment(Qt::Alignment alignment);
  void setElideMode(int elideMode);

  void draw();
  void drawStext(tgfx::Canvas* canvas);
  void createAppHost();


  Q_SIGNALS:
  void textChanged();
  void scolorChanged();
  void contrastChanged();
  void alignmentChanged();
  void elideModeChanged();

protected:
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  QString elideText(const QString &text, float maxWidth);
  uint32_t colorToUint32(const QColor &color) const;
  tgfx::Rect getTextBounds(const QString& text) const;


private:
  QString sText;
  QColor sColor = Qt::white;
  bool sContrast = false;
  Qt::Alignment sAlignment = Qt::AlignLeft | Qt::AlignVCenter;
  int sElideMode = Qt::ElideRight;
  int sFontSize = 0;
  bool sGeometryChanged = false;

  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
  bool dirty = false;
};
