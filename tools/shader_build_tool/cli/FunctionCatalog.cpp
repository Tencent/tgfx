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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "cli/BuildCommand.h"
#include "gpu/shaders/ShaderLibraryBundleFormat.h"

namespace tgfx {
namespace {

struct CatalogEntry {
  std::string name = {};
  std::string source = {};
  std::vector<std::string>
  requires = {};
  std::string opPrefix = {};
  std::vector<std::pair<std::string, std::string>> members = {};     // param -> member
  std::vector<std::pair<std::string, std::string>> sources = {};     // param -> enum override
  std::vector<std::pair<std::string, std::string>> conditions = {};  // param -> guard
};

// Drops comments and blank lines from a catalog function (the bundle ships into the binary).
// The include guard is a file-level include mechanism, not part of the function's own text: the
// bundle stores the function alone and the assembler dedupes by name, so a file wrapped in an
// #ifndef/#define pair with a trailing #endif contributes only its function. Stripping here
// keeps guards (and their matching, easily mismatched #endif lines) out of every assembled text;
// a file without the wrapper pattern passes through unchanged.
std::string StripIncludeGuard(const std::string& text) {
  std::vector<std::string> lines = {};
  size_t lineStart = 0;
  while (lineStart <= text.size()) {
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = text.size();
    }
    std::string line = text.substr(lineStart, lineEnd - lineStart);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
    if (lineEnd == text.size()) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  const auto leadingSpace = [](const std::string& line) {
    size_t at = 0;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
      ++at;
    }
    return at;
  };
  const auto directive = [&](const std::string& line, const char* name) -> std::string {
    const size_t at = leadingSpace(line);
    const std::string prefix = std::string("#") + name;
    if (line.compare(at, prefix.size(), prefix) != 0) {
      return {};
    }
    const auto end = line.find_first_not_of(" \t", at + prefix.size());
    if (end == std::string::npos) {
      return {};
    }
    const auto nameEnd = line.find_first_of(" \t", end);
    return line.substr(end, nameEnd == std::string::npos ? std::string::npos : nameEnd - end);
  };
  // The guard: a leading #ifndef NAME directly followed by #define NAME, closed by the file's
  // last #endif.
  size_t guardStart = std::string::npos;
  for (size_t index = 0; index + 1 < lines.size(); ++index) {
    const auto name = directive(lines[index], "ifndef");
    if (!name.empty() && directive(lines[index + 1], "define") == name) {
      guardStart = index;
      break;
    }
  }
  if (guardStart == std::string::npos) {
    return text;
  }
  size_t guardEnd = std::string::npos;
  for (size_t index = lines.size(); index-- > 0;) {
    if (leadingSpace(lines[index]) == lines[index].size()) {
      continue;  // blank tail lines
    }
    if (directive(lines[index], "endif").empty()) {
      return text;  // the file's last directive is not the guard's #endif: not a wrapper
    }
    guardEnd = index;
    break;
  }
  if (guardEnd == std::string::npos || guardEnd <= guardStart + 1) {
    return text;
  }
  lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(guardEnd));
  lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(guardStart),
              lines.begin() + static_cast<std::ptrdiff_t>(guardStart) + 2);
  std::string out = {};
  for (const auto& line : lines) {
    out += line;
    out.push_back('\n');
  }
  return out;
}

std::string CompactFunctionSource(const std::string& text) {
  std::string out = {};
  out.reserve(text.size());
  bool inBlock = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const char next = i + 1 < text.size() ? text[i + 1] : '\0';
    if (inBlock) {
      if (c == '*' && next == '/') {
        inBlock = false;
        ++i;
      }
      continue;
    }
    if (c == '/' && next == '/') {
      while (i + 1 < text.size() && text[i] != '\n') {
        ++i;
      }
      continue;
    }
    if (c == '/' && next == '*') {
      // /*@in*/ is a parameter annotation the signature parser reads, not a comment.
      if (text.compare(i, 7, "/*@in*/") == 0) {
        out += "/*@in*/";
        i += 6;
        continue;
      }
      inBlock = true;
      ++i;
      continue;
    }
    out.push_back(c);
  }
  std::string compact = {};
  compact.reserve(out.size());
  size_t lineStart = 0;
  while (lineStart <= out.size()) {
    size_t lineEnd = out.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = out.size();
    }
    const std::string line = out.substr(lineStart, lineEnd - lineStart);
    if (line.find_first_not_of(" \t\r") != std::string::npos) {
      compact += line;
      compact.push_back('\n');
    }
    if (lineEnd == out.size()) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  return compact;
}

bool ParseAnnotatedFunction(const std::string& text, CatalogEntry* entry, std::string* error) {
  std::string source = {};
  size_t lineStart = 0;
  while (lineStart <= text.size()) {
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = text.size();
    }
    std::string line = text.substr(lineStart, lineEnd - lineStart);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string functionTag = "//@function ";

    const std::string requiresTag = "//@requires ";
    if (line.rfind(functionTag, 0) == 0) {
      if (!entry->name.empty()) {
        *error = "a file declares more than one //@function";
        return false;
      }
      const std::string rest = line.substr(functionTag.size());
      const auto space = rest.find(' ');
      if (space != std::string::npos) {
        entry->name = rest.substr(0, space);
        entry->opPrefix = rest.substr(space + 1);
      } else {
        entry->name = rest;
      }
    } else if (line.rfind("//@members ", 0) == 0 || line.rfind("//@member ", 0) == 0 ||
               line.rfind("//@source ", 0) == 0 || line.rfind("//@when ", 0) == 0 ||
               line.rfind("//@member-start ", 0) == 0 || line.rfind("//@requires ", 0) == 0) {
      *error = "retired annotation: " + line + " (see the //@function prefix form)";
      return false;
    } else if (line.rfind("#pragma tgfx_", 0) == 0) {
      // Build-tool directives (the spec reader marker and its gates): the runtime consumers
      // assemble plain GLSL, so the pragmas stay out of the emitted text.
    } else {
      source += line;
      source.push_back('\n');
    }
    if (lineEnd == text.size()) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  if (entry->name.empty()) {
    *error = "no //@function annotation";
    return false;
  }
  entry->source = CompactFunctionSource(StripIncludeGuard(source));
  if (entry->source.empty()) {
    *error = "the function body is empty";
    return false;
  }
  return true;
}

// The parameters of the function's signature, in order, as (type, name).
std::vector<std::pair<std::string, std::string>> SignatureParams(const std::string& source,
                                                                 const std::string& name) {
  std::vector<std::pair<std::string, std::string>> params = {};
  const auto nameAt = source.find(name + "(");
  if (nameAt == std::string::npos) {
    return params;
  }
  const auto open = source.find('(', nameAt);
  const auto close = source.find(')', open);
  if (open == std::string::npos || close == std::string::npos) {
    return params;
  }
  std::string list = source.substr(open + 1, close - open - 1);
  size_t start = 0;
  while (start <= list.size()) {
    size_t end = list.find(',', start);
    if (end == std::string::npos) {
      end = list.size();
    }
    std::string item = list.substr(start, end - start);
    const auto first = item.find_first_not_of(" \t\n");
    const auto last = item.find_last_not_of(" \t\n");
    if (first != std::string::npos) {
      item = item.substr(first, last - first + 1);
      const auto space = item.find_last_of(" \t\n");
      if (space != std::string::npos) {
        std::string name = item.substr(space + 1);
        std::string type = item.substr(0, space);
        // An array parameter: the name may carry the count ("scales[8]") or the type may
        // ("vec4[8]"). Extract the count and normalize both.
        size_t arrayCount = 0;
        const auto nameBracket = name.find('[');
        if (nameBracket != std::string::npos) {
          arrayCount = static_cast<size_t>(std::atoi(name.c_str() + nameBracket + 1));
          name = name.substr(0, nameBracket);
        }
        const auto typeBracket = type.find('[');
        if (typeBracket != std::string::npos && arrayCount == 0) {
          arrayCount = static_cast<size_t>(std::atoi(type.c_str() + typeBracket + 1));
        }
        type = type.substr(0, typeBracket == std::string::npos ? type.size() : typeBracket);
        if (arrayCount > 0) {
          type += "[" + std::to_string(arrayCount) + "]";
        }
        // A trailing /*@in*/ marks a call argument; strip it and remember it as a prefix.
        const auto marker = name.find("/*@in*/");
        if (marker != std::string::npos) {
          name = name.substr(0, marker);
          const auto trim = name.find_last_not_of(" \t");
          name = (trim == std::string::npos) ? "" : name.substr(0, trim + 1);
          type += " /*@in*/";
        }
        params.emplace_back(type, name);
      }
    }
    if (end == list.size()) {
      break;
    }
    start = end + 1;
  }
  return params;
}

const char* FragmentTypeOf(const std::string& glslType) {
  const auto bracket = glslType.find('[');
  const std::string base = bracket == std::string::npos ? glslType : glslType.substr(0, bracket);
  if (base == "int") {
    return "Int";
  }
  if (base == "float") {
    return "Float";
  }
  if (base == "vec2") {
    return "Float2";
  }
  if (base == "vec3") {
    return "Float3";
  }
  if (base == "vec4") {
    return "Float4";
  }
  if (base == "ivec4") {
    return "Int4";
  }
  if (base == "mat3") {
    return "Matrix3";
  }
  return nullptr;
}

}  // namespace

namespace {

bool CollectCatalogEntries(const std::string& shaderDir, std::vector<CatalogEntry>* collected,
                           std::string* error) {
  namespace fs = std::filesystem;
  if (collected == nullptr || error == nullptr) {
    return false;
  }
  const fs::path root = fs::path(shaderDir) / "fn";
  if (!fs::exists(root)) {
    *error = "no fn directory under " + shaderDir;
    return false;
  }
  // The geometry directory carries the per-pixel edge-coverage formulas beside the vertex-side
  // layout fragments (gp_*_layout.inc, plain text the catalog cannot parse); only the coverage
  // files join the scan, by name.
  const fs::path geometryRoot = fs::path(shaderDir) / "geometry";
  std::vector<CatalogEntry> entries = {};
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
       ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    std::ifstream file(it->path(), std::ios::binary);
    if (!file.is_open()) {
      *error = "cannot read " + it->path().string();
      return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    CatalogEntry entry = {};
    std::string parseError;
    if (!ParseAnnotatedFunction(buffer.str(), &entry, &parseError)) {
      *error = it->path().string() + ": " + parseError;
      return false;
    }
    for (const auto& existing : entries) {
      if (existing.name == entry.name) {
        *error = "duplicate function " + entry.name + " (" + it->path().string() + ")";
        return false;
      }
    }
    entries.push_back(std::move(entry));
  }
  if (fs::exists(geometryRoot)) {
    for (auto it = fs::directory_iterator(geometryRoot); it != fs::directory_iterator(); ++it) {
      if (!it->is_regular_file() ||
          it->path().filename().string().rfind("_coverage.inc") == std::string::npos) {
        continue;
      }
      std::ifstream file(it->path(), std::ios::binary);
      if (!file.is_open()) {
        *error = "cannot read " + it->path().string();
        return false;
      }
      std::ostringstream buffer;
      buffer << file.rdbuf();
      CatalogEntry entry = {};
      std::string parseError;
      if (!ParseAnnotatedFunction(buffer.str(), &entry, &parseError)) {
        *error = it->path().string() + ": " + parseError;
        return false;
      }
      for (const auto& existing : entries) {
        if (existing.name == entry.name) {
          *error = "duplicate function " + entry.name + " (" + it->path().string() + ")";
          return false;
        }
      }
      entries.push_back(std::move(entry));
    }
  }
  if (entries.empty()) {
    *error = "the fn directory declares no functions";
    return false;
  }
  // Dependencies come from the bodies: every identifier followed by '(' that names another
  // catalog function is a call. Declared edges cannot drift because there are none to declare.
  std::set<std::string> names = {};
  for (const auto& entry : entries) {
    names.insert(entry.name);
  }
  for (auto& entry : entries) {
    std::set<std::string> called = {};
    const std::string& body = entry.source;
    for (size_t index = 0; index < body.size();) {
      const unsigned char c = static_cast<unsigned char>(body[index]);
      if (std::isalpha(c) || c == '_') {
        size_t end = index;
        while (end < body.size() &&
               (std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_')) {
          ++end;
        }
        const std::string token = body.substr(index, end - index);
        size_t next = end;
        while (next < body.size() && (body[next] == ' ' || body[next] == '\t')) {
          ++next;
        }
        if (next < body.size() && body[next] == '(' && token != entry.name &&
            names.count(token) != 0) {
          called.insert(token);
        }
        index = end;
      } else {
        ++index;
      }
    }
    entry.requires.assign(called.begin(), called.end());
  }
  std::vector<int> state(entries.size(), 0);  // -1 visiting, 1 done
  std::function<bool(size_t)> visit = [&](size_t index) {
    if (state[index] == 1) {
      return true;
    }
    if (state[index] == -1) {
      *error = "requirement cycle through " + entries[index].name;
      return false;
    }
    state[index] = -1;
    for (const auto& required : entries[index].requires) {
      for (size_t other = 0; other < entries.size(); ++other) {
        if (entries[other].name == required && !visit(other)) {
          return false;
        }
      }
    }
    state[index] = 1;
    return true;
  };
  for (size_t index = 0; index < entries.size(); ++index) {
    if (!visit(index)) {
      return false;
    }
  }
  *collected = std::move(entries);
  return true;
}

bool WriteChainOpRegistry(const std::vector<CatalogEntry>& entries, const std::string& outPath,
                          std::string* error) {
  std::vector<const CatalogEntry*> sorted = {};
  for (const auto& entry : entries) {
    sorted.push_back(&entry);
  }
  std::sort(sorted.begin(), sorted.end(), [](const CatalogEntry* left, const CatalogEntry* right) {
    return left->name < right->name;
  });
  std::ostringstream registry;
  registry << "// Generated by shader_build_tool --library-bundle. Do not edit.\n";
  registry << "// Each section is guarded by TGFX_REGISTER_<function>: the includer defines the "
              "macro for\n"
              "// the one entry it registers (no include guard, the file is included once per "
              "entry).\n\n";
  for (const CatalogEntry* entry : sorted) {
    if (entry->opPrefix.empty()) {
      continue;  // a helper, not an op entry: it registers no members
    }
    const auto params = SignatureParams(entry->source, entry->name);
    if (params.empty()) {
      *error = entry->name + ": the signature is unreadable";
      return false;
    }
    registry << "#if defined(TGFX_REGISTER_" << entry->name << ")\n";
    const std::string& prefix = entry->opPrefix;
    // A parameter whose type carries /*@in*/ is a call argument; everything else is a block
    // member. The type carries the array count (vec4[8] -> one member, count 8); the name is
    // the parameter name, capitalized and prefixed. The signature IS the structure.
    for (size_t index = 0; index < params.size(); ++index) {
      const std::string& glslType = params[index].first;
      if (glslType.find("/*@in*/") != std::string::npos) {
        continue;
      }
      const std::string& paramName = params[index].second;
      if (glslType.rfind("sampler", 0) == 0) {
        continue;  // a texture is an argument, not a uniform member
      }
      size_t arrayCount = 0;
      const auto bracket = glslType.find('[');
      if (bracket != std::string::npos) {
        arrayCount = static_cast<size_t>(std::atoi(glslType.c_str() + bracket + 1));
      }
      const char* fragmentType = FragmentTypeOf(glslType);
      if (fragmentType == nullptr) {
        *error = entry->name + ": unsupported parameter type " + glslType;
        return false;
      }
      std::string memberName = prefix;
      memberName.push_back(
          static_cast<char>(std::toupper(static_cast<unsigned char>(paramName[0]))));
      memberName += paramName.substr(1);
      registry << "  registry->uniform(\"" << memberName << "\", FragmentType::" << fragmentType
               << ", ChainBindingSource::" << memberName << ", -1";
      if (arrayCount > 1) {
        registry << ", -1, " << arrayCount;
      }
      registry << ");\n";
    }
    registry << "#endif\n";
  }
  std::ofstream registryOut(outPath, std::ios::binary);
  if (!registryOut.is_open()) {
    *error = "cannot write " + outPath;
    return false;
  }
  registryOut << registry.str();
  return true;
}

void AppendU32(std::string* out, uint32_t value) {
  out->push_back(static_cast<char>(value & 0xFFu));
  out->push_back(static_cast<char>((value >> 8) & 0xFFu));
  out->push_back(static_cast<char>((value >> 16) & 0xFFu));
  out->push_back(static_cast<char>((value >> 24) & 0xFFu));
}

void AppendU64(std::string* out, uint64_t value) {
  AppendU32(out, static_cast<uint32_t>(value & 0xFFFFFFFFu));
  AppendU32(out, static_cast<uint32_t>(value >> 32));
}

uint64_t HashSource(const std::string& text) {
  uint64_t hash = 1469598103934665603ull;
  for (const char character : text) {
    hash ^= static_cast<uint64_t>(static_cast<unsigned char>(character));
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string RequiresOf(const CatalogEntry& entry) {
  std::string requires = {};
  for (const auto& dependency : entry.requires) {
    if (!requires.empty()) {
      requires.push_back(',');
    }
    requires += dependency;
  }
  return requires;
}

}  // namespace

bool WriteShaderLibraryBundle(const std::string& shaderDir, const std::string& outPath,
                              std::string* error) {
  if (error == nullptr) {
    return false;
  }
  std::vector<CatalogEntry> entries = {};
  if (!CollectCatalogEntries(shaderDir, &entries, error)) {
    return false;
  }
  std::vector<const CatalogEntry*> sorted = {};
  for (const auto& entry : entries) {
    sorted.push_back(&entry);
  }
  std::sort(sorted.begin(), sorted.end(), [](const CatalogEntry* left, const CatalogEntry* right) {
    return left->name < right->name;
  });

  struct FunctionOffset {
    uint32_t name = 0;
    uint32_t source = 0;
    uint32_t sourceSize = 0;
    uint32_t requires = 0;
  };
  std::string pool = {};
  std::vector<FunctionOffset> offsets = {};
  offsets.reserve(sorted.size());
  for (const CatalogEntry* entry : sorted) {
    FunctionOffset offset = {};
    offset.name = static_cast<uint32_t>(pool.size());
    pool += entry->name;
    pool.push_back('\0');
    offset.source = static_cast<uint32_t>(pool.size());
    offset.sourceSize = static_cast<uint32_t>(entry->source.size());
    pool += entry->source;
    pool.push_back('\0');
    const std::string requires = RequiresOf(*entry);
    offset.requires = static_cast<uint32_t>(pool.size());
    pool += requires;
    pool.push_back('\0');
    offsets.push_back(offset);
  }

  const uint32_t sectionOffset = SHADER_LIBRARY_BUNDLE_HEADER_SIZE;
  const uint32_t entryTableOffset = sectionOffset + SHADER_LIBRARY_BUNDLE_SECTION_SIZE;
  const uint32_t poolOffset =
      entryTableOffset +
      static_cast<uint32_t>(offsets.size() * SHADER_LIBRARY_BUNDLE_FUNCTION_SIZE);
  std::string bundle = {};
  bundle.append(SHADER_LIBRARY_BUNDLE_MAGIC, sizeof(SHADER_LIBRARY_BUNDLE_MAGIC) - 1);
  AppendU32(&bundle, SHADER_LIBRARY_BUNDLE_VERSION);
  AppendU32(&bundle, SHADER_LIBRARY_BUNDLE_HEADER_SIZE);
  AppendU32(&bundle, 1);
  AppendU32(&bundle, ShaderLibraryLanguageMask(ShaderLibraryLanguage::GLSL));
  AppendU64(&bundle, HashSource(pool));
  AppendU32(&bundle, static_cast<uint32_t>(ShaderLibraryLanguage::GLSL));
  AppendU32(&bundle, static_cast<uint32_t>(offsets.size()));
  AppendU32(&bundle, entryTableOffset);
  AppendU32(&bundle, poolOffset);
  AppendU32(&bundle, static_cast<uint32_t>(pool.size()));
  AppendU32(&bundle, 0);
  for (const auto& offset : offsets) {
    AppendU32(&bundle, offset.name);
    AppendU32(&bundle, offset.source);
    AppendU32(&bundle, offset.sourceSize);
    AppendU32(&bundle, offset.requires);
  }
  bundle += pool;
  std::ofstream out(outPath, std::ios::binary);
  if (!out.is_open()) {
    *error = "cannot write " + outPath;
    return false;
  }
  out.write(bundle.data(), static_cast<std::streamsize>(bundle.size()));
  if (!out) {
    *error = "cannot write " + outPath;
    return false;
  }
  const std::filesystem::path outputPath(outPath);
  const std::filesystem::path registryPath = outputPath.parent_path() / "ChainOpRegistry.inc";
  return WriteChainOpRegistry(entries, registryPath.string(), error);
}

}  // namespace tgfx
