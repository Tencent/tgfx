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

#include "gpu/shaders/ShaderLibraryBundle.h"
#include <cstring>
#include "core/utils/Log.h"

namespace tgfx {
#if defined(TGFX_EMBED_SHADER_LIBRARY)
extern const uint8_t* GetEmbeddedShaderLibraryBundle(size_t* size);
#endif

namespace {

uint32_t ReadU32(const uint8_t* data, size_t size, size_t offset, bool* ok) {
  if (!*ok || offset > size || size - offset < sizeof(uint32_t)) {
    *ok = false;
    return 0;
  }
  const uint8_t* bytes = data + offset;
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t ReadU64(const uint8_t* data, size_t size, size_t offset, bool* ok) {
  if (!*ok || offset > size || size - offset < sizeof(uint64_t)) {
    *ok = false;
    return 0;
  }
  const uint64_t low = ReadU32(data, size, offset, ok);
  const uint64_t high = ReadU32(data, size, offset + sizeof(uint32_t), ok);
  return low | (high << 32);
}

bool IsKnownLanguage(uint32_t value) {
  return value == static_cast<uint32_t>(ShaderLibraryLanguage::GLSL) ||
         value == static_cast<uint32_t>(ShaderLibraryLanguage::MSL) ||
         value == static_cast<uint32_t>(ShaderLibraryLanguage::HLSL) ||
         value == static_cast<uint32_t>(ShaderLibraryLanguage::WGSL) ||
         value == static_cast<uint32_t>(ShaderLibraryLanguage::SPIRV);
}

const char* FindString(const uint8_t* data, size_t poolBegin, size_t poolSize, uint32_t offset,
                       bool* ok) {
  if (!*ok || offset >= poolSize) {
    *ok = false;
    return nullptr;
  }
  const size_t begin = poolBegin + offset;
  const void* terminator = std::memchr(data + begin, 0, poolSize - offset);
  if (terminator == nullptr) {
    *ok = false;
    return nullptr;
  }
  return reinterpret_cast<const char*>(data + begin);
}

}  // namespace

ShaderLibraryBundle ShaderLibraryBundle::CreateEmbedded() {
#if defined(TGFX_EMBED_SHADER_LIBRARY)
  size_t size = 0;
  const uint8_t* data = GetEmbeddedShaderLibraryBundle(&size);
  return ShaderLibraryBundle(data, size);
#else
  return ShaderLibraryBundle(nullptr, 0);
#endif
}

ShaderLibraryBundle::ShaderLibraryBundle(const uint8_t* data, size_t size) {
  if (data == nullptr || size < SHADER_LIBRARY_BUNDLE_HEADER_SIZE ||
      std::memcmp(data, SHADER_LIBRARY_BUNDLE_MAGIC, sizeof(SHADER_LIBRARY_BUNDLE_MAGIC) - 1) !=
          0) {
    return;
  }
  bool ok = true;
  const uint32_t version = ReadU32(data, size, 8, &ok);
  const uint32_t headerSize = ReadU32(data, size, 12, &ok);
  const uint32_t sectionCount = ReadU32(data, size, 16, &ok);
  const uint32_t languageMask = ReadU32(data, size, 20, &ok);
  const uint64_t sourceHash = ReadU64(data, size, 24, &ok);
  (void)sourceHash;
  if (!ok || version != SHADER_LIBRARY_BUNDLE_VERSION ||
      headerSize != SHADER_LIBRARY_BUNDLE_HEADER_SIZE || sectionCount == 0 ||
      sectionCount > (size - headerSize) / SHADER_LIBRARY_BUNDLE_SECTION_SIZE ||
      languageMask == 0) {
    return;
  }
  size_t sectionOffset = headerSize;
  for (uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
    const uint32_t languageValue = ReadU32(data, size, sectionOffset, &ok);
    const uint32_t functionCount = ReadU32(data, size, sectionOffset + 4, &ok);
    const uint32_t entryOffset = ReadU32(data, size, sectionOffset + 8, &ok);
    const uint32_t poolOffset = ReadU32(data, size, sectionOffset + 12, &ok);
    const uint32_t poolSize = ReadU32(data, size, sectionOffset + 16, &ok);
    ReadU32(data, size, sectionOffset + 20, &ok);
    if (!ok || !IsKnownLanguage(languageValue) || entryOffset > size ||
        functionCount > (size - entryOffset) / SHADER_LIBRARY_BUNDLE_FUNCTION_SIZE ||
        poolOffset > size || poolSize > size - poolOffset) {
      _sections.clear();
      return;
    }
    Section section = {};
    section.language = static_cast<ShaderLibraryLanguage>(languageValue);
    section.functions.reserve(functionCount);
    for (uint32_t functionIndex = 0; functionIndex < functionCount; ++functionIndex) {
      const size_t entryOffsetInBundle =
          static_cast<size_t>(entryOffset) +
          static_cast<size_t>(functionIndex) * SHADER_LIBRARY_BUNDLE_FUNCTION_SIZE;
      const uint32_t nameOffset = ReadU32(data, size, entryOffsetInBundle, &ok);
      const uint32_t sourceOffset = ReadU32(data, size, entryOffsetInBundle + 4, &ok);
      const uint32_t sourceSize = ReadU32(data, size, entryOffsetInBundle + 8, &ok);
      const uint32_t requiresOffset = ReadU32(data, size, entryOffsetInBundle + 12, &ok);
      if (!ok || sourceOffset > poolSize || sourceSize > poolSize - sourceOffset) {
        _sections.clear();
        return;
      }
      const char* name = FindString(data, poolOffset, poolSize, nameOffset, &ok);
      const char* requires = FindString(data, poolOffset, poolSize, requiresOffset, &ok);
      if (!ok || name == nullptr || requires == nullptr) {
        _sections.clear();
        return;
      }
      Function function = {};
      function.name = name;
      function.source = std::string_view(
          reinterpret_cast<const char*>(data + poolOffset + sourceOffset), sourceSize);
      function.requires = requires;
      section.functions.push_back(function);
    }
    _sections.push_back(std::move(section));
    sectionOffset += SHADER_LIBRARY_BUNDLE_SECTION_SIZE;
  }
}

const ShaderLibraryBundle& ShaderLibraryBundle::Get() {
  static const ShaderLibraryBundle bundle = CreateEmbedded();
  return bundle;
}

const ShaderLibraryBundle::Section* ShaderLibraryBundle::findSection(
    ShaderLibraryLanguage language) const {
  for (const auto& section : _sections) {
    if (section.language == language) {
      return &section;
    }
  }
  return nullptr;
}

bool ShaderLibraryBundle::hasLanguage(ShaderLibraryLanguage language) const {
  return findSection(language) != nullptr;
}

size_t ShaderLibraryBundle::functionCount(ShaderLibraryLanguage language) const {
  const auto* section = findSection(language);
  return section == nullptr ? 0 : section->functions.size();
}

bool ShaderLibraryBundle::containsFunction(ShaderLibraryLanguage language, const char* name) const {
  const auto* section = findSection(language);
  if (section == nullptr || name == nullptr) {
    return false;
  }
  for (const auto& function : section->functions) {
    if (function.name == name) {
      return true;
    }
  }
  return false;
}

ShaderLibraryBundle::OperatorModule ShaderLibraryBundle::findOperatorModule(OperatorID op) const {
  const auto* desc = FindOperator(op);
  if (desc == nullptr || desc->glslSymbol == nullptr) {
    return {};
  }
  const auto* section = findSection(ShaderLibraryLanguage::SPIRV);
  if (section == nullptr) {
    return {};
  }
  for (const auto& function : section->functions) {
    if (function.name == desc->glslSymbol) {
      return {reinterpret_cast<const uint8_t*>(function.source.data()), function.source.size()};
    }
  }
  return {};
}

bool ShaderLibraryBundle::appendFunction(ShaderLibraryLanguage language, const char* name,
                                         std::set<std::string>* emitted, std::string* out) const {
  const auto* section = findSection(language);
  if (section == nullptr) {
    LOGE("ShaderLibraryBundle: the bundle carries no section for language %u",
         static_cast<unsigned>(language));
    return false;
  }
  return appendInSection(*section, name, emitted, out);
}

bool ShaderLibraryBundle::appendInSection(const Section& section, const char* name,
                                          std::set<std::string>* emitted, std::string* out) const {
  if (name == nullptr || name[0] == '\0' || emitted == nullptr || out == nullptr) {
    return false;
  }
  if (!emitted->insert(name).second) {
    return true;
  }
  for (const auto& function : section.functions) {
    if (function.name != name) {
      continue;
    }
    size_t start = 0;
    while (start <= function.requires.size()) {
      const size_t comma = function.requires.find(',', start);
      const std::string dependency(function.requires.substr(
          start, comma == std::string::npos ? std::string::npos : comma - start));
      if (!dependency.empty() && !appendInSection(section, dependency.c_str(), emitted, out)) {
        return false;
      }
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
    out->append(function.source.data(), function.source.size());
    out->push_back('\n');
    return true;
  }
  LOGE("ShaderLibraryBundle: '%s' is not in the catalog!", name);
  return false;
}

}  // namespace tgfx
