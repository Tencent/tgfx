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

#include "core/ShaderSources.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace tgfx {

namespace {

// Reads files once per load: the guardless slot headers are included many times by design.
class FileCache {
 public:
  // Returns false when the file cannot be opened. An existing empty file reads as "".
  bool read(const std::string& path, std::string* contents) {
    // Callers pass normalized paths for includes; the top-level files are read once each anyway.
    auto it = files.find(path);
    if (it == files.end()) {
      std::ifstream file(path);
      if (!file.is_open()) {
        return false;
      }
      std::stringstream buffer;
      buffer << file.rdbuf();
      it = files.emplace(path, buffer.str()).first;
    }
    *contents = it->second;
    return true;
  }

 private:
  std::map<std::string, std::string> files;
};

std::string FormatChain(const std::vector<std::string>& chain) {
  std::string text;
  for (const auto& path : chain) {
    text += (text.empty() ? "" : " -> ") + path;
  }
  return text;
}

// Resolves "." and "name/.." segments and repeated slashes without touching the file system, so
// two spellings of one path compare equal: the cycle check and the file cache key on this form.
// A ".." that has nothing to remove stays (a relative path can legitimately start with one) and
// is dropped after a root. Symbolic links are not followed, which is the same text-level view a C
// preprocessor has of an include path.
std::string NormalizePath(const std::string& path) {
  bool absolute = !path.empty() && path[0] == '/';
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= path.size()) {
    auto slash = path.find('/', start);
    auto end = slash == std::string::npos ? path.size() : slash;
    auto part = path.substr(start, end - start);
    if (part.empty() || part == ".") {
      // Nothing to add.
    } else if (part == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (!absolute) {
        parts.push_back(part);
      }
    } else {
      parts.push_back(part);
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  std::string text = absolute ? "/" : "";
  for (size_t i = 0; i < parts.size(); ++i) {
    text += (i == 0 ? "" : "/") + parts[i];
  }
  return text.empty() ? "." : text;
}

// Expands #include "file" lines. An include resolves against the shader root — the directory the
// tool was pointed at — never against the directory of the file containing the include, so a
// module keeps the same include line wherever it is included from and the organized tree's
// subdirectories need no "../" spelling. The output format is unchanged: an expanded include is
// followed by an extra newline, because the expanded text feeds both the compiler and the
// published bundles' source digests.
//
// chain holds the normalized paths of the files being expanded, outermost first. A file that is
// already on the chain is a cycle, however its path is spelled. Repeating a file that is not on
// the chain is legal: the slot bind/unbind headers are guardless and included repeatedly on
// purpose, so no visited set is applied.
bool ExpandIncludes(const std::string& source, const std::string& shaderRoot, FileCache* files,
                    std::vector<std::string>* chain, std::string* result, std::string* error) {
  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    auto firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos && line.compare(firstNonSpace, 9, "#include ") == 0) {
      auto quoteStart = line.find('"', firstNonSpace + 9);
      auto quoteEnd =
          quoteStart == std::string::npos ? std::string::npos : line.find('"', quoteStart + 1);
      if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
        auto includePath = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        auto fullPath = NormalizePath(shaderRoot + "/" + includePath);
        if (std::find(chain->begin(), chain->end(), fullPath) != chain->end()) {
          chain->push_back(fullPath);
          *error = "include cycle: " + FormatChain(*chain);
          return false;
        }
        if (static_cast<int>(chain->size()) > MaxIncludeDepth) {
          *error = "includes nested deeper than " + std::to_string(MaxIncludeDepth) + ": " +
                   FormatChain(*chain) + " -> " + fullPath;
          return false;
        }
        std::string includeContent;
        if (!files->read(fullPath, &includeContent)) {
          *error = "cannot read #include \"" + includePath + "\": " + FormatChain(*chain) +
                   " -> " + fullPath;
          return false;
        }
        chain->push_back(fullPath);
        std::string expanded;
        if (!ExpandIncludes(includeContent, shaderRoot, files, chain, &expanded, error)) {
          return false;
        }
        chain->pop_back();
        *result += expanded + "\n";
        continue;
      }
    }
    *result += line + "\n";
  }
  return true;
}

}  // namespace


bool ReadGeneratorContract(const std::string& shaderDir, std::string* text, std::string* error) {
  if (text == nullptr || error == nullptr) {
    return false;
  }
  text->clear();
  FileCache files;
  auto contractRoot = NormalizePath(shaderDir + "/..");
  for (const auto* file : GeneratorContractFiles) {
    auto path = NormalizePath(contractRoot + "/" + file);
    std::string contents;
    if (!files.read(path, &contents)) {
      *error = "cannot read the generator contract " + path;
      return false;
    }
    *text += "// " + std::string(file) + "\n" + contents + "\n";
  }
  return true;
}

std::string ExpandStageIncludes(const std::string& source, const std::string& shaderDir,
                                std::string* error) {
  FileCache files;
  std::vector<std::string> chain;
  std::string result;
  if (!ExpandIncludes(source, shaderDir, &files, &chain, &result, error)) {
    return "";
  }
  return result;
}

}  // namespace tgfx
