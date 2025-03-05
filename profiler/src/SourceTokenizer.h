#pragma once

#include <stdint.h>
#include <vector>

class Tokenizer {
public:
  enum class TokenColor : uint8_t {
    Default,
    Comment,
    Prprocessor,
    String,
    CharacterLiteral,
    Keyword,
    Number,
    Punctuation,
    Type,
    Special,
  };

  struct Token {
    const char* begin;
    const char* end;
    TokenColor color;
  };

  struct Line {
    const char* begin;
    const char* end;
    std::vector<Token> tokens;
  };

  Tokenizer();
  ~Tokenizer();

  std::vector<Token> tokenize(const char* begin, const char* end);

private:
  TokenColor identifyToken(const char*& begin, const char* end);

  bool isInComment;
  bool isInPreprocessor;
};