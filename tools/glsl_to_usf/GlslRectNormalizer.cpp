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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GlslRectNormalizer.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace tgfx {
namespace {

constexpr const char* kInvSizePrefix = "TGFX_InvRectSize_";
constexpr const char* kGB1DMacro = "TGFX_GB1D_SAMPLE";

// Helpers whose rect-sampler argument pairs with a coord argument that must be wrapped when
// the sampler is one of the rewritten uniforms. PorterDuff is deliberately absent: its
// sampler2D overload already consumes a `(1/w, 1/h)` coord scale that UE supplies at runtime,
// so no call-site rewrite is required.
struct HelperSpec {
  const char* name;
  size_t samplerIdx;
  size_t coordIdx;
};

constexpr HelperSpec kHelpers[] = {
    {"TGFX_TextureEffect_RGBA", 2, 1},
    {"TGFX_TiledTextureEffect", 2, 1},
    {"TGFX_AtlasText_SampleAtlas", 0, 1},
};

bool IsIdentStart(char c) {
  return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_';
}

bool IsIdentChar(char c) {
  return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && (std::isspace(static_cast<unsigned char>(s[start])) != 0)) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && (std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)) {
    --end;
  }
  return s.substr(start, end - start);
}

std::vector<std::string> SplitLines(const std::string& s) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n') {
      lines.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start <= s.size()) {
    lines.emplace_back(s.substr(start));
  }
  return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::ostringstream out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    if (i + 1 < lines.size()) out << '\n';
  }
  return out.str();
}

// Match `<indent>uniform sampler2DRect <Name>;` allowing arbitrary whitespace and an optional
// trailing line comment. Returns true with the captured indent+name on success.
bool MatchRectUniformDecl(const std::string& line, std::string* indent, std::string* name) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  size_t indentEnd = i;
  const char* kKw = "uniform";
  if (line.compare(i, 7, kKw) != 0) return false;
  i += 7;
  if (i >= line.size() || (std::isspace(static_cast<unsigned char>(line[i])) == 0)) return false;
  while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
  const char* kType = "sampler2DRect";
  if (line.compare(i, 13, kType) != 0) return false;
  i += 13;
  if (i >= line.size() || (std::isspace(static_cast<unsigned char>(line[i])) == 0)) return false;
  while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
  if (i >= line.size() || !IsIdentStart(line[i])) return false;
  size_t nameStart = i;
  while (i < line.size() && IsIdentChar(line[i])) ++i;
  size_t nameEnd = i;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i >= line.size() || line[i] != ';') return false;
  ++i;
  // Trailing whitespace / optional line comment is allowed.
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i < line.size()) {
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
      // comment runs to end of line, fine
    } else {
      return false;
    }
  }
  *indent = line.substr(0, indentEnd);
  *name = line.substr(nameStart, nameEnd - nameStart);
  return true;
}

// Walk `src` from `startPos`, consuming a top-level function signature that opens with
// `sigStartKeyword` (typically `vec4` or `void`) followed by an identifier, an argument list,
// and a body `{ ... }`. Returns the past-the-end position of the body, or std::string::npos if
// the signature does not balance. Used to locate the full extent of a function so we can erase
// it wholesale.
size_t ScanFunctionExtent(const std::string& src, size_t sigStart) {
  // Find the first '(' after the signature start.
  size_t paren = src.find('(', sigStart);
  if (paren == std::string::npos) return std::string::npos;
  int depth = 1;
  size_t i = paren + 1;
  while (i < src.size() && depth > 0) {
    if (src[i] == '(') ++depth;
    else if (src[i] == ')')
      --depth;
    ++i;
  }
  if (depth != 0) return std::string::npos;
  // Skip whitespace to the '{' of the body.
  while (i < src.size() && (std::isspace(static_cast<unsigned char>(src[i])) != 0)) ++i;
  if (i >= src.size() || src[i] != '{') return std::string::npos;
  depth = 1;
  ++i;
  while (i < src.size() && depth > 0) {
    if (src[i] == '{') ++depth;
    else if (src[i] == '}')
      --depth;
    ++i;
  }
  if (depth != 0) return std::string::npos;
  return i;
}

// Confirm that the signature starting at `sigStart` belongs to a function named with the
// `TGFX_` prefix and has `sampler2DRect` appearing inside its argument list (not inside the
// body, not inside a nested comment). A structurally shallow scan is sufficient because the
// dumped shaders always declare one function per top-level region.
bool SignatureReferencesRectSampler(const std::string& src, size_t sigStart) {
  size_t open = src.find('(', sigStart);
  if (open == std::string::npos) return false;
  if (src.compare(sigStart, 5, "TGFX_") != 0) {
    // Look for `TGFX_` after the return type, e.g. `vec4 TGFX_...`.
    size_t nameStart = sigStart;
    while (nameStart < open && (std::isspace(static_cast<unsigned char>(src[nameStart])) != 0 ||
                                IsIdentChar(src[nameStart]))) {
      ++nameStart;
    }
    // Backtrack to the last identifier before '('. Simpler: require that the 16 chars
    // immediately before '(' contain `TGFX_` somewhere.
    size_t searchStart = open > 32 ? open - 32 : sigStart;
    if (src.find("TGFX_", searchStart) >= open) return false;
  }
  int depth = 1;
  size_t i = open + 1;
  std::string args;
  while (i < src.size() && depth > 0) {
    if (src[i] == '(') ++depth;
    else if (src[i] == ')')
      --depth;
    if (depth > 0) args.push_back(src[i]);
    ++i;
  }
  return args.find("sampler2DRect") != std::string::npos;
}

// Strip every top-level function whose signature declares a `sampler2DRect` parameter. After
// the uniform-type rewrite these overloads are unreachable (GLSL overload resolution will
// always pick the sampler2D sibling) and their `sampler2DRect` parameter type is invalid once
// the rect extension is no longer implicitly enabled.
std::string StripRectOverloads(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  size_t pos = 0;
  while (pos < src.size()) {
    // Look for a top-level function signature: either `vec4 TGFX_` or `void TGFX_` at the
    // start of a line (optionally after whitespace). We scan for the two keywords anywhere,
    // then check that the surrounding signature contains `sampler2DRect`.
    size_t vec4Hit = src.find("\nvec4 TGFX_", pos);
    size_t voidHit = src.find("\nvoid TGFX_", pos);
    // Also handle the case where the signature begins at file start without a preceding '\n'.
    if (pos == 0) {
      if (src.compare(0, 11, "vec4 TGFX_") == 0 || src.compare(0, 11, "void TGFX_") == 0) {
        // Treat as a zero offset match.
        if (SignatureReferencesRectSampler(src, 0)) {
          size_t endPos = ScanFunctionExtent(src, 0);
          if (endPos != std::string::npos) {
            pos = endPos;
            // Skip trailing whitespace/newline that visually belonged to the deleted block.
            while (pos < src.size() && (src[pos] == '\n' || src[pos] == ' ' || src[pos] == '\t')) {
              ++pos;
            }
            continue;
          }
        }
      }
    }
    size_t hit = std::min(vec4Hit, voidHit);
    if (hit == std::string::npos) {
      out.append(src, pos, std::string::npos);
      break;
    }
    size_t sigStart = hit + 1;  // skip the leading '\n'
    if (!SignatureReferencesRectSampler(src, sigStart)) {
      // Not a rect overload; emit up to and including this signature's first character and
      // advance.
      out.append(src, pos, sigStart - pos);
      pos = sigStart;
      // Emit at least one character so we don't infinite loop on the same position.
      out.push_back(src[pos]);
      ++pos;
      continue;
    }
    size_t endPos = ScanFunctionExtent(src, sigStart);
    if (endPos == std::string::npos) {
      // Can't balance; leave the rest untouched.
      out.append(src, pos, std::string::npos);
      break;
    }
    // Emit the region before the signature, then skip the function + any trailing blank line.
    out.append(src, pos, hit - pos);
    pos = endPos;
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;
    if (pos < src.size() && src[pos] == '\n') ++pos;
  }
  return out;
}

// Split `inner` (the interior of a helper call's argument list) on top-level commas. Nested
// parens introduced by `vec4(...)`, `vec2(0.0)`, and single-level arithmetic are respected.
std::vector<std::string> SplitTopLevelCommas(const std::string& inner) {
  std::vector<std::string> args;
  int depth = 0;
  size_t start = 0;
  for (size_t i = 0; i < inner.size(); ++i) {
    char c = inner[i];
    if (c == '(') ++depth;
    else if (c == ')')
      --depth;
    else if (c == ',' && depth == 0) {
      args.emplace_back(inner.substr(start, i - start));
      start = i + 1;
    }
  }
  args.emplace_back(inner.substr(start));
  return args;
}

// Return true iff `s` (already trimmed) is exactly an identifier. The whitelist lookup relies
// on this so `TextureSampler_0_P1` matches but `doSomething(TextureSampler_0_P1)` does not.
bool IsBareIdentifier(const std::string& s) {
  if (s.empty() || !IsIdentStart(s[0])) return false;
  for (char c : s) {
    if (!IsIdentChar(c)) return false;
  }
  return true;
}

// Rewrite helper calls of the form `helperName(arg0, arg1, ...)` whose argument at
// `samplerIdx` is a bare rect-uniform identifier. The coord argument at `coordIdx` is wrapped
// with `* TGFX_InvRectSize_<name>`.
std::string WrapHelperCallSites(const std::string& src, const HelperSpec& helper,
                                const std::unordered_set<std::string>& rects) {
  std::string out;
  out.reserve(src.size());
  std::string token = std::string(helper.name) + "(";
  size_t pos = 0;
  while (pos < src.size()) {
    size_t hit = src.find(token, pos);
    if (hit == std::string::npos) {
      out.append(src, pos, std::string::npos);
      break;
    }
    // The char immediately before `hit` must not be an ident char; otherwise `helperName` is
    // the tail of a longer identifier and we must not rewrite it.
    if (hit > 0 && IsIdentChar(src[hit - 1])) {
      out.append(src, pos, (hit + token.size()) - pos);
      pos = hit + token.size();
      continue;
    }
    // Locate the balanced ')'.
    int depth = 1;
    size_t i = hit + token.size();
    while (i < src.size() && depth > 0) {
      if (src[i] == '(') ++depth;
      else if (src[i] == ')')
        --depth;
      if (depth > 0) ++i;
    }
    if (depth != 0) {
      out.append(src, pos, std::string::npos);
      break;
    }
    size_t closeParen = i;
    std::string argList = src.substr(hit + token.size(), closeParen - (hit + token.size()));
    auto args = SplitTopLevelCommas(argList);
    if (args.size() <= helper.samplerIdx || args.size() <= helper.coordIdx) {
      // Argument count mismatch — leave untouched and move past this call.
      out.append(src, pos, (closeParen + 1) - pos);
      pos = closeParen + 1;
      continue;
    }
    std::string samplerTrimmed = Trim(args[helper.samplerIdx]);
    if (!IsBareIdentifier(samplerTrimmed) || rects.count(samplerTrimmed) == 0) {
      out.append(src, pos, (closeParen + 1) - pos);
      pos = closeParen + 1;
      continue;
    }
    // Wrap the coord arg. Preserve the original leading whitespace so diffs stay minimal.
    std::string coord = args[helper.coordIdx];
    // Find the first non-space char's offset inside `coord` to preserve its indent.
    size_t coordStart = 0;
    while (coordStart < coord.size() &&
           (std::isspace(static_cast<unsigned char>(coord[coordStart])) != 0)) {
      ++coordStart;
    }
    std::string leading = coord.substr(0, coordStart);
    std::string coordBody = coord.substr(coordStart);
    std::string wrapped = leading + "(" + coordBody + ") * " + kInvSizePrefix + samplerTrimmed;
    args[helper.coordIdx] = wrapped;
    std::string rebuilt;
    for (size_t a = 0; a < args.size(); ++a) {
      if (a > 0) rebuilt += ",";
      rebuilt += args[a];
    }
    out.append(src, pos, hit - pos);
    out += helper.name;
    out += '(';
    out += rebuilt;
    out += ')';
    pos = closeParen + 1;
  }
  return out;
}

// Rewrite single-line `#define <MACRO>(coord) texture(<S>, coord)` definitions where `<S>` is
// a rect uniform. The macro body's coord arg gains a `* TGFX_InvRectSize_<S>` multiplier so
// every expansion automatically samples at the normalized coordinate. Any other `#define`
// that mentions a rect uniform but does not match this exact shape is a structural surprise
// and is surfaced as an error so future emitter changes are caught immediately.
bool RewriteSamplingMacros(std::vector<std::string>& lines,
                           const std::unordered_set<std::string>& rects,
                           std::string* errorMessage) {
  const std::string kDefineKw = "#define";
  const std::string kTextureCall = "texture(";
  for (auto& line : lines) {
    std::string trimmed = Trim(line);
    if (trimmed.rfind(kDefineKw, 0) != 0) continue;
    // Does this #define reference any rect uniform name? If yes, it must match our
    // recognized shape; otherwise we flag it.
    bool mentionsRect = false;
    std::string mentionedName;
    for (const auto& name : rects) {
      // Must be a standalone identifier (not a substring of a longer one).
      size_t p = 0;
      while ((p = trimmed.find(name, p)) != std::string::npos) {
        bool leftOk = (p == 0) || !IsIdentChar(trimmed[p - 1]);
        bool rightOk =
            (p + name.size() == trimmed.size()) || !IsIdentChar(trimmed[p + name.size()]);
        if (leftOk && rightOk) {
          mentionsRect = true;
          mentionedName = name;
          break;
        }
        p += name.size();
      }
      if (mentionsRect) break;
    }
    if (!mentionsRect) continue;

    // Try to match `#define <MacroName>(coord) texture(<Name>, coord)`.
    // Step past `#define`.
    size_t i = line.find(kDefineKw);
    if (i == std::string::npos) continue;
    i += kDefineKw.size();
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    size_t macroNameStart = i;
    while (i < line.size() && IsIdentChar(line[i])) ++i;
    if (i == macroNameStart) {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    std::string macroName = line.substr(macroNameStart, i - macroNameStart);
    // We only recognize macros whose name is `TGFX_GB1D_SAMPLE` today. Any other name that
    // references a rect uniform is treated as structurally unknown.
    if (macroName != kGB1DMacro) {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    if (i >= line.size() || line[i] != '(') {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    ++i;
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    size_t paramStart = i;
    while (i < line.size() && IsIdentChar(line[i])) ++i;
    std::string paramName = line.substr(paramStart, i - paramStart);
    if (paramName != "coord") {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    if (i >= line.size() || line[i] != ')') {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    ++i;
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    if (line.compare(i, kTextureCall.size(), kTextureCall) != 0) {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    i += kTextureCall.size();
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    size_t samplerStart = i;
    while (i < line.size() && IsIdentChar(line[i])) ++i;
    std::string samplerName = line.substr(samplerStart, i - samplerStart);
    if (rects.count(samplerName) == 0) {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    if (i >= line.size() || line[i] != ',') {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    ++i;
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    size_t coordStart = i;
    while (i < line.size() && IsIdentChar(line[i])) ++i;
    std::string coordArg = line.substr(coordStart, i - coordStart);
    if (coordArg != "coord") {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    while (i < line.size() && (std::isspace(static_cast<unsigned char>(line[i])) != 0)) ++i;
    if (i >= line.size() || line[i] != ')') {
      *errorMessage = "unrecognized #define referencing rect uniform: " + line;
      return false;
    }
    size_t indentEnd = line.find_first_not_of(" \t");
    if (indentEnd == std::string::npos) indentEnd = 0;
    std::string indent = line.substr(0, indentEnd);
    line = indent + "#define " + kGB1DMacro + "(coord) texture(" + samplerName + ", (coord) * " +
           kInvSizePrefix + samplerName + ")";
    (void)mentionedName;
  }
  return true;
}

}  // namespace

RectNormalizeResult NormalizeRectSamplers(const std::string& glsl, GlslStage stage) {
  RectNormalizeResult result;
  // VS dumps never contain rect uniforms; return the source unchanged so callers can run the
  // normalizer for both stages uniformly.
  if (stage == GlslStage::Vertex) {
    result.glsl = glsl;
    return result;
  }

  auto lines = SplitLines(glsl);
  std::unordered_set<std::string> rectSet;
  size_t firstRectLine = std::string::npos;
  std::string firstRectIndent;
  for (size_t li = 0; li < lines.size(); ++li) {
    std::string indent;
    std::string name;
    if (MatchRectUniformDecl(lines[li], &indent, &name)) {
      rectSet.insert(name);
      if (firstRectLine == std::string::npos) {
        firstRectLine = li;
        firstRectIndent = indent;
      }
      lines[li] = indent + "uniform sampler2D " + name + ";";
    }
  }
  if (rectSet.empty()) {
    // Nothing to do — no declarations touched. Return the original text byte-for-byte to keep
    // non-rect programs stable.
    result.glsl = glsl;
    return result;
  }

  // Record rects in a deterministic order (sorted by name) so manifest output is stable
  // across runs.
  std::vector<std::string> sortedNames(rectSet.begin(), rectSet.end());
  std::sort(sortedNames.begin(), sortedNames.end());
  result.rects.reserve(sortedNames.size());
  for (auto& name : sortedNames) {
    result.rects.push_back({name, std::string(kInvSizePrefix) + name});
  }

  // Emit the companion inverse-size uniforms as members of a dedicated std140 UBO, placed on
  // the line immediately preceding the first rewritten sampler. Vulkan's GLSL dialect forbids
  // non-opaque uniforms at file scope, so a UBO is the only legal container. Using a dedicated
  // block avoids perturbing the layout of the shader's existing FragmentUniformBlock (some
  // rect shaders don't declare one at all).
  std::ostringstream block;
  block << firstRectIndent << "layout(std140) uniform TgfxRectFragmentUniformBlock {";
  for (const auto& name : sortedNames) {
    block << "\n" << firstRectIndent << "    vec2 " << kInvSizePrefix << name << ";";
  }
  block << "\n" << firstRectIndent << "};";
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(firstRectLine), block.str());

  std::string error;
  if (!RewriteSamplingMacros(lines, rectSet, &error)) {
    result.errorMessage = error;
    return result;
  }

  std::string rewritten = JoinLines(lines);
  rewritten = StripRectOverloads(rewritten);
  for (const auto& helper : kHelpers) {
    rewritten = WrapHelperCallSites(rewritten, helper, rectSet);
  }

  result.glsl = std::move(rewritten);
  return result;
}

}  // namespace tgfx
