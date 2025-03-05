#pragma once

#include "TracyWorker.hpp"
#include <vector>
#include "SourceTokenizer.h"

class View;

class SourceContents {
public:
  SourceContents();
  ~SourceContents();

  void Parse(const char* fileName, const tracy::Worker& worker, const View* view);
  void Parse(const char* source);

  const std::vector<Tokenizer::Line>& get() const {return lines;}
  bool empty() const {return lines.empty();}

  const char* filename() const {return files;}
  uint32_t idx() const {return fileStringIdx;}
  bool isCached() const {return mdata != dataBuf;}
  const char* data() const {return mdata;}
  size_t dataSize() const {return mdataSize;}

private:
  void Tokenize(const char* txt, size_t sz);

  const char* files;
  uint32_t fileStringIdx;

  const char* mdata;
  size_t mdataSize;

  char* dataBuf;
  size_t dataBufSize;

  std::vector<Tokenizer::Line> lines;
};
