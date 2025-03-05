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















