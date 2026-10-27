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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLDialect.h"

#include <cctype>
#include <charconv>
#include <cstring>
#include <iostream>
#include <regex>

namespace tgfx {

namespace {

// Recursive-descent evaluator behind FoldIntegerExpression. The grammar: sum := product
// (('+'|'-') product)*, product := unary ('*' unary)*, unary := ('+'|'-') unary | '(' sum ')'
// | number. Writing the recursion as methods instead of mutually recursive std::function
// lambdas keeps the parser debuggable and the call chain explicit.
class IntegerExpressionParser {
 public:
  static bool Evaluate(const std::string& expression, int64_t* value);

 private:
  explicit IntegerExpressionParser(const std::string& expression) : expression(expression) {}

  bool ParseSum(int64_t* value);
  bool ParseProduct(int64_t* value);
  bool ParseUnary(int64_t* value);
  void SkipSpaces();

  const std::string& expression;
  size_t pos = 0;
};

bool IntegerExpressionParser::Evaluate(const std::string& expression, int64_t* value) {
  IntegerExpressionParser parser(expression);
  int64_t result = 0;
  if (!parser.ParseSum(&result)) {
    return false;
  }
  parser.SkipSpaces();
  if (parser.pos != expression.size()) {
    return false;
  }
  *value = result;
  return true;
}

bool IntegerExpressionParser::ParseSum(int64_t* value) {
  if (!ParseProduct(value)) {
    return false;
  }
  while (true) {
    SkipSpaces();
    if (pos >= expression.size() || (expression[pos] != '+' && expression[pos] != '-')) {
      return true;
    }
    auto op = expression[pos++];
    int64_t rhs = 0;
    if (!ParseProduct(&rhs)) {
      return false;
    }
    *value = op == '+' ? *value + rhs : *value - rhs;
  }
}

bool IntegerExpressionParser::ParseProduct(int64_t* value) {
  if (!ParseUnary(value)) {
    return false;
  }
  while (true) {
    SkipSpaces();
    if (pos >= expression.size() || expression[pos] != '*') {
      return true;
    }
    ++pos;
    int64_t rhs = 0;
    if (!ParseUnary(&rhs)) {
      return false;
    }
    *value *= rhs;
  }
}

bool IntegerExpressionParser::ParseUnary(int64_t* value) {
  SkipSpaces();
  if (pos >= expression.size()) {
    return false;
  }
  auto c = expression[pos];
  if (c == '+' || c == '-') {
    ++pos;
    int64_t inner = 0;
    if (!ParseUnary(&inner)) {
      return false;
    }
    *value = c == '-' ? -inner : inner;
    return true;
  }
  if (c == '(') {
    ++pos;
    if (!ParseSum(value)) {
      return false;
    }
    SkipSpaces();
    if (pos >= expression.size() || expression[pos] != ')') {
      return false;
    }
    ++pos;
    return true;
  }
  if (isdigit(static_cast<unsigned char>(c))) {
    auto start = pos;
    while (pos < expression.size() && isdigit(static_cast<unsigned char>(expression[pos]))) {
      ++pos;
    }
    // std::from_chars rejects a literal beyond the int64 range instead of throwing, so an
    // oversized layout id fails the fold and the caller keeps the original text.
    int64_t number = 0;
    auto begin = expression.data() + start;
    auto end = expression.data() + pos;
    auto conversion = std::from_chars(begin, end, number);
    if (conversion.ec != std::errc() || conversion.ptr != end) {
      return false;
    }
    *value = number;
    return true;
  }
  return false;
}

void IntegerExpressionParser::SkipSpaces() {
  while (pos < expression.size() && isspace(static_cast<unsigned char>(expression[pos]))) {
    ++pos;
  }
}

// GL and GLES store their own spelling of the template instead of text regenerated from SPIR-V:
// the regenerated form's call-site temporaries (param/param_1) and normalized block names
// measured +79% lines on the chain interpreter. This pass runs after the standard GLSL
// preprocessor (PreprocessGLSL), so every #if block is already resolved for the variant and the
// stored text carries only live code. It rewrites the preprocessed 450 text into the target
// dialect: lower the version header, drop the `set = N` / `binding = N` layout members (the
// runtime binds the UBO at a fixed point and the samplers in declaration order), drop varying
// locations, and fold location ids to literals. Reflection keeps coming from the common SPIR-V
// (same template, same names, same std140 layout), so the loader contract and the ABI are
// unchanged. Returns an empty string when the source does not carry the expected version, so
// the caller fails the variant closed.
std::string EmitDirectGLSL330Impl(const std::string& source, ShaderStageType stage,
                                  const std::string& versionLine, bool esDialect,
                                  bool fbfVariant) {
  const std::string versionToken = "#version 450";
  auto versionPos = source.find(versionToken);
  if (versionPos == std::string::npos) {
    return {};
  }
  auto lineStart = source.rfind('\n', versionPos);
  bool atFileHead =
      versionPos == 0 || (lineStart != std::string::npos &&
                          source.compare(lineStart + 1, versionToken.size(), versionToken) == 0);
  if (!atFileHead) {
    return {};
  }
  std::string result = source;
  result.replace(versionPos, versionToken.size(), versionLine);
  if (esDialect) {
    // Default precision right after the version line, mirroring the regenerated ES-300 form
    // (int is highp-capable everywhere; float must be stated — ES has no implicit default).
    auto versionEnd = result.find('\n', versionPos);
    if (versionEnd != std::string::npos) {
      // Insert after the #version line itself — the templates open with a license comment
      // block, so the file's first newline sits BEFORE the version token; inserting there
      // would put a precision statement ahead of #version, which ES rejects.
      result.insert(versionEnd + 1, "precision mediump float;\nprecision highp int;\n");
    }
    if (fbfVariant) {
      // The framebuffer-fetch dialect: the extension line goes right after the precision
      // defaults, ahead of every declaration that references it.
      auto insertPos = result.find("precision highp int;\n", versionPos);
      if (insertPos != std::string::npos) {
        result.insert(insertPos + sizeof("precision highp int;\n") - 1,
                      "#extension GL_EXT_shader_framebuffer_fetch : require\n");
      }
    }
  }

  std::string out;
  out.reserve(result.size());

  size_t cursor = 0;
  while (cursor < result.size()) {
    auto lineEnd = result.find('\n', cursor);
    auto line =
        result.substr(cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor);
    auto directive = line.find_first_not_of(" \t");
    if (directive != std::string::npos && line[directive] == '#') {
      // Only #version, #extension and #pragma survive preprocessing; they pass through.
      auto directiveComment = line.find("//");
      out += directiveComment == std::string::npos ? line : line.substr(0, directiveComment);
      out += '\n';
      cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
      continue;
    }
    // Drop lines left empty by the preprocessor.
    auto comment = line.find("//");
    auto code = comment == std::string::npos ? line : line.substr(0, comment);
    if (code.find_first_not_of(" \t\r") == std::string::npos) {
      cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
      continue;
    }
    // Rewrite layout(...) lists: drop set/binding, drop varying locations, fold location ids.
    {
      auto open = code.find("layout(");
      while (open != std::string::npos) {
        // The matching parenthesis: a layout id may be a parenthesized expression.
        auto close = std::string::npos;
        int depth = 0;
        for (auto i = open + 6; i < code.size(); ++i) {
          if (code[i] == '(') {
            ++depth;
          } else if (code[i] == ')' && --depth == 0) {
            close = i;
            break;
          }
        }
        if (close == std::string::npos) {
          break;
        }
        std::string inner = code.substr(open + 7, close - open - 7);
        std::string kept;
        size_t tokenCursor = 0;
        while (tokenCursor <= inner.size()) {
          auto comma = inner.find(',', tokenCursor);
          auto token = inner.substr(
              tokenCursor, comma == std::string::npos ? std::string::npos : comma - tokenCursor);
          auto headWs = token.find_first_not_of(" \t");
          if (headWs == std::string::npos) {
            if (comma == std::string::npos) break;
            tokenCursor = comma + 1;
            continue;
          }
          std::string member = token.substr(headWs);
          if (member.compare(0, 4, "set ") != 0 && member.compare(0, 8, "binding ") != 0) {
            // GLSL 330 allows layout(location) only on vertex inputs and fragment outputs;
            // varyings (vertex out / fragment in) must link by name — 4.1 introduced explicit
            // varying locations. spirv-cross already strips them in its 330 output; the direct
            // emission drops the member instead. Attribute/out declarations are identified by
            // the `in`/`out` keyword on the declaration line.
            auto isVarying =
                code.find(" in ") != std::string::npos
                    ? stage == ShaderStageType::Fragment
                    : (code.find(" out ") != std::string::npos && stage == ShaderStageType::Vertex);
            auto eq = member.find("= ");
            if (member.rfind("location", 0) == 0 && eq != std::string::npos) {
              if (isVarying) {
                member.clear();  // drop the location member from a varying declaration
              } else {
                // Fold `location = <expr>` to a literal: GLSL 330 / ES 300 require one, and the
                // preprocessor leaves macro-built ids as arithmetic such as `2 + 1`.
                auto expr = member.substr(eq + 2);
                int64_t folded = 0;
                if (FoldIntegerExpression(expr, &folded)) {
                  member = "location = " + std::to_string(folded);
                }
              }
            }
            if (!member.empty()) {
              if (!kept.empty()) {
                kept += ", ";
              }
              kept += member;
            }
          }
          if (comma == std::string::npos) break;
          tokenCursor = comma + 1;
        }
        std::string replacement = kept.empty() ? "" : "layout(" + kept + ")";
        code.replace(open, close - open + 1, replacement);
        open = code.find("layout(", open + replacement.size());
      }
    }
    out += code;
    out += '\n';
    cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
  }
  if (esDialect) {
    // Blanket `highp` decoration on every float-family declaration. The regenerated ES-300
    // output decorates uniformly (45k occurrences, zero exceptions — the fragment default is
    // mediump, and many devices implement it as fp16, so the decoration is a numerical
    // contract). Decoration sites: declarations (globals/locals/UBO members/params), not
    // usages; the pattern is a type keyword at a declaration position not already decorated
    // and not a struct field separator. int-family stays as written (default highp).
    static const std::regex floatType(R"((^|[^A-Za-z0-9_])(float|vec[234]|mat[234])\s+[A-Za-z_])");
    std::string decorated;
    decorated.reserve(out.size() + 4096);
    size_t cursor2 = 0;
    while (cursor2 < out.size()) {
      auto lineEnd2 = out.find('\n', cursor2);
      auto line2 = out.substr(
          cursor2, lineEnd2 == std::string::npos ? std::string::npos : lineEnd2 - cursor2);
      // Preprocessor lines, precision statements, and the extension line pass through.
      auto nonSpace = line2.find_first_not_of(" \t");
      bool passthrough = nonSpace == std::string::npos || line2[nonSpace] == '#' ||
                         line2.compare(nonSpace, 9, "precision") == 0;
      if (!passthrough) {
        std::string rebuilt;
        size_t pos = 0;
        while (pos < line2.size()) {
          std::smatch match;
          std::string rest = line2.substr(pos);
          if (std::regex_search(rest, match, floatType) && !match.empty()) {
            auto at = pos + static_cast<size_t>(match.position(2));
            // Already decorated (the char run before the type is `highp `/`mediump `) or a
            // usage inside an expression: only decorate when the preceding token chain ends
            // with a declarator context. Heuristic sufficient for these sources: the match is
            // a declaration when the line's first token is a type/qualifier or we are inside
            // a parameter list — verified by the calibration diff against the regenerated
            // text, which fails the build on any mismatch.
            rebuilt += line2.substr(pos, at - pos);
            auto prefix = line2.substr(0, at);
            auto endsWith = [](const std::string& s, const char* suffix) {
              auto len = strlen(suffix);
              return s.size() >= len && s.compare(s.size() - len, len, suffix) == 0;
            };
            bool decoratedAlready = endsWith(prefix, "highp ") || endsWith(prefix, "mediump ") ||
                                    endsWith(prefix, "lowp ");
            if (!decoratedAlready) {
              rebuilt += "highp ";
            }
            // Skip exactly the matched type keyword (its length is known; do not re-scan the
            // text — 'float' contains characters outside any type charset and would stall the
            // cursor).
            auto typeLength = static_cast<size_t>(match.length(2));
            rebuilt += line2.substr(at, typeLength);
            pos = at + typeLength;
          } else {
            rebuilt += line2.substr(pos);
            pos = line2.size();
          }
        }
        line2 = rebuilt;
      }
      decorated += line2;
      if (lineEnd2 != std::string::npos) {
        decorated += '\n';
      }
      cursor2 = lineEnd2 == std::string::npos ? out.size() : lineEnd2 + 1;
    }
    out = decorated;
    if (fbfVariant) {
      // Framebuffer-fetch dialect on the fragment stage: subpass input declaration removed,
      // subpassLoad() reads the inout output, and the output declaration becomes inout. The
      // templates confine these to xp_porter_duff_fbf.inc plus the single output declaration,
      // so targeted replacements cover the whole surface.
      auto declPos = out.find("layout(input_attachment_index = 0");
      if (declPos != std::string::npos) {
        auto declEnd = out.find('\n', declPos);
        out.erase(declPos,
                  declEnd == std::string::npos ? out.size() - declPos : declEnd - declPos + 1);
      }
      auto outPos = out.find("layout(location = 0) out ");
      if (outPos != std::string::npos) {
        out.replace(outPos, 24, "layout(location = 0) inout ");
      }
      auto loadPos = out.find("subpassLoad(");
      while (loadPos != std::string::npos) {
        auto argOpen = out.find('(', loadPos);
        auto argClose = out.find(')', argOpen);
        auto arg = out.substr(argOpen + 1, argClose - argOpen - 1);
        // Strip the sampler name to its base (tgfx_SubpassInput -> the inout variable).
        out.replace(loadPos, argClose - loadPos + 1, arg);
        loadPos = out.find("subpassLoad(", loadPos + arg.size());
      }
      // `vec4 dstColor = tgfx_SubpassInput;` — the erased declaration leaves the uniform
      // sampler name as a plain identifier; rename it to the fragment output.
      auto dstPos = out.find("= tgfx_SubpassInput;");
      while (dstPos != std::string::npos) {
        out.replace(dstPos, 20, "= fragColor;");
        dstPos = out.find("= tgfx_SubpassInput;", dstPos + 13);
      }
    }
  }
  return out;
}

}  // namespace

bool FoldIntegerExpression(const std::string& expr, int64_t* value) {
  return IntegerExpressionParser::Evaluate(expr, value);
}

std::string EmitDirectGLSL330(const std::string& source, ShaderStageType stage) {
  std::string error;
  auto preprocessed = PreprocessGLSL(source, stage, &error);
  if (preprocessed.empty()) {
    std::cerr << "  direct-GL 330 preprocessing failed: " << error << "\n";
    return {};
  }
  return EmitDirectGLSL330Impl(preprocessed, stage, "#version 330", false, false);
}

std::string EmitDirectGLSLES300(const std::string& source, ShaderStageType stage, bool fbfVariant) {
  std::string error;
  auto preprocessed = PreprocessGLSL(source, stage, &error);
  if (preprocessed.empty()) {
    std::cerr << "  direct-ES 300 preprocessing failed: " << error << "\n";
    return {};
  }
  return EmitDirectGLSL330Impl(preprocessed, stage, "#version 300 es", true, fbfVariant);
}

}  // namespace tgfx
