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
#include "SourceTokenizer.h"
#include <QWidget>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>


/////source window/////
class CodeEditor;
class SourceView : public QWidget {
  Q_OBJECT

public:
  explicit SourceView(QWidget* parent = nullptr);
  void loadSource(const QString& content, int highLightLine);

private:
  CodeEditor* codeEditor = nullptr;
};

/////sourcetext format the color/////
class SourceTextColor : public QSyntaxHighlighter {
public:
  explicit SourceTextColor(QTextDocument* parent): QSyntaxHighlighter(parent){}

protected:
  void highlightBlock(const QString& text) override;

private:
  Tokenizer tokenizer;
};

/////source linenumber/////
class CodeEditor : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit CodeEditor(QWidget* parent = nullptr);

  int lineNumberAreaWidth();
  void lineNumberAreaPaintEvent(QPaintEvent* event);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  Q_SLOT void updateLineNumberAreaWidth(int newBlockCount);
  Q_SLOT void updateLineNumberArea(const QRect &rect, int dy);
  Q_SLOT void highlightCurrentLine();

  QWidget *lineNumberArea;
};

class LineNumberArea : public QWidget {
public:
  explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), codeEditor(editor){}

  QSize sizeHint() const override { return QSize(codeEditor->lineNumberAreaWidth(), 0); }

protected:
  void paintEvent(QPaintEvent* event) override {codeEditor->lineNumberAreaPaintEvent(event);}

private:
  CodeEditor* codeEditor;
};















