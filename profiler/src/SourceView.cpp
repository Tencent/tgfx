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

#include "SourceView.h"
#include <QPainter>
#include <QPlainTextEdit>
#include <QVBoxLayout>

/////*source window*/////
SourceView::SourceView(QWidget* parent) : QWidget(parent, Qt::Window) {
  setWindowFlags(Qt::Window);
  resize(800, 600);

  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  codeEditor = new CodeEditor(this);
  codeEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
  codeEditor->setReadOnly(true);

  QFont font("Courier New");
  font.setFixedPitch(true);
  font.setPointSize(10);
  codeEditor->setFont(font);

  layout->addWidget(codeEditor);
}

void SourceView::loadSource(const QString& content, int highLightLine) {
  codeEditor->setPlainText(content);
  new SourceTextColor(codeEditor->document());
  QTextCursor cursor(codeEditor->document()->findBlockByLineNumber(highLightLine - 1));
  QTextBlockFormat format = cursor.blockFormat();
  format.setBackground(QColor(255, 255, 0, 50));
  cursor.setBlockFormat(format);

  codeEditor->setTextCursor(cursor);
  codeEditor->centerCursor();
}

/////*sourceText format*/////
void SourceTextColor::highlightBlock(const QString& text) {
  QByteArray byteArray = text.toUtf8();
  const char* cText = byteArray.constData();
  size_t len = static_cast<size_t>(byteArray.size());

  std::vector<Tokenizer::Token> tokens = tokenizer.tokenize(cText, cText + len);
  for (const auto& token : tokens) {
    int startIndex = static_cast<int>(token.begin - cText);
    int tokenLength = static_cast<int>(token.end - token.begin);
    QTextCharFormat format;

    switch (token.color) {
      case Tokenizer::TokenColor::Keyword:
        format.setForeground(QColor(86, 156, 214));
        break;

      case Tokenizer::TokenColor::Type:
        format.setForeground(QColor(78, 201, 176));
        break;

      case Tokenizer::TokenColor::Special:
        format.setForeground(QColor(212, 212, 212));
        break;

      case Tokenizer::TokenColor::Comment:
        format.setForeground(QColor(106, 153, 85));
        break;

      case Tokenizer::TokenColor::CharacterLiteral:
      case Tokenizer::TokenColor::String:
        format.setForeground(QColor(206, 145, 120));
        break;

      case Tokenizer::TokenColor::Number:
        format.setForeground(QColor(181, 206, 168));
        break;

      case Tokenizer::TokenColor::Punctuation:
        format.setForeground(QColor(255, 165, 0));
        break;

      case Tokenizer::TokenColor::Prprocessor:
        format.setForeground(QColor(197, 134, 192));
        break;

      default:
        format.setForeground(Qt::white);
        break;
    }
    setFormat(startIndex, tokenLength, format);
  }
}

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
  lineNumberArea = new LineNumberArea(this);

  connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
  connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
  connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

  updateLineNumberAreaWidth(0);
  highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() {
  int digits = 1;
  int max = qMax(1, blockCount());
  while (max >= 10) {
    max /= 10;
    ++digits;
  }
  int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  return space;
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
  QPainter painter(lineNumberArea);
  painter.fillRect(event->rect(), QColor(40, 40, 40));

  QTextBlock block = firstVisibleBlock();
  int blockNumber = block.blockNumber();
  int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int buttom = top + qRound(blockBoundingRect(block).height());

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && buttom >= event->rect().top()) {
      QString number = QString::number(blockNumber + 1);
      painter.setPen(Qt::white);
      painter.drawText(0, top, lineNumberArea->width() - 2, fontMetrics().height(), Qt::AlignRight,
                       number);
    }
    block = block.next();
    top = buttom;
    buttom = top + static_cast<int>(blockBoundingRect(block).height());
    ++blockNumber;
  }
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);
  QRect cr = contentsRect();
  lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */) {
  setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
  if (dy) {
    lineNumberArea->scroll(0, dy);
  } else {
    lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
  }

  if (rect.contains(viewport()->rect())) {
    updateLineNumberAreaWidth(0);
  }
}

void CodeEditor::highlightCurrentLine() {
  QList<QTextEdit::ExtraSelection> exSeleciton;
  if (!isReadOnly()) {
    QTextEdit::ExtraSelection selection;
    QColor lineColor = QColor(232, 232, 255, 60);
    selection.format.setBackground(lineColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    exSeleciton.append(selection);
  }
  setExtraSelections(exSeleciton);
}
