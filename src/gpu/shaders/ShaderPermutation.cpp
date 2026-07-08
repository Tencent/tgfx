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

#include "gpu/shaders/ShaderPermutation.h"
#include <cassert>

namespace tgfx {

struct ValueCountVisitor {
  int operator()(const PermutationBool& d) const {
    return d.valueCount();
  }
  int operator()(const PermutationEnum& d) const {
    return d.valueCount();
  }
  int operator()(const PermutationInt& d) const {
    return d.valueCount();
  }
};

struct DefineNameVisitor {
  const char* operator()(const PermutationBool& d) const {
    return d.defineName;
  }
  const char* operator()(const PermutationEnum& d) const {
    return d.defineName;
  }
  const char* operator()(const PermutationInt& d) const {
    return d.defineName;
  }
};

static int GetValueCount(const PermutationDimension& dim) {
  return std::visit(ValueCountVisitor{}, dim);
}

static const char* GetDefineName(const PermutationDimension& dim) {
  return std::visit(DefineNameVisitor{}, dim);
}

PermutationDomain::PermutationDomain(std::vector<PermutationDimension> dimensions)
    : dimensions(std::move(dimensions)) {
}

static std::string TrimWhitespace(const std::string& str, size_t start, size_t end) {
  while (start < end && str[start] == ' ') {
    start++;
  }
  while (end > start && str[end - 1] == ' ') {
    end--;
  }
  return str.substr(start, end - start);
}

PermutationDomain PermutationDomain::FromBoolNames(const char* commaSeparatedNames) {
  // Parse comma-separated names, allocating persistent storage for each name string.
  // This is intentionally leaked because shader declarations have program lifetime.
  std::vector<PermutationDimension> dims;
  std::string input(commaSeparatedNames);
  size_t start = 0;
  while (start < input.size()) {
    auto comma = input.find(',', start);
    if (comma == std::string::npos) {
      comma = input.size();
    }
    auto name = TrimWhitespace(input, start, comma);
    if (!name.empty()) {
      auto* persistent = new char[name.size() + 1];
      std::copy(name.begin(), name.end(), persistent);
      persistent[name.size()] = '\0';
      dims.emplace_back(PermutationBool(persistent));
    }
    start = comma + 1;
  }
  return PermutationDomain(std::move(dims));
}

uint32_t PermutationDomain::totalCount() const {
  uint32_t count = 1;
  for (const auto& dim : dimensions) {
    count *= static_cast<uint32_t>(GetValueCount(dim));
  }
  return count;
}

std::vector<int> PermutationDomain::decode(uint32_t index) const {
  std::vector<int> values(dimensions.size());
  for (size_t i = 0; i < dimensions.size(); i++) {
    auto radix = static_cast<uint32_t>(GetValueCount(dimensions[i]));
    values[i] = static_cast<int>(index % radix);
    index /= radix;
  }
  return values;
}

uint32_t PermutationDomain::encode(const std::vector<int>& values) const {
  assert(values.size() == dimensions.size());
  uint32_t index = 0;
  uint32_t stride = 1;
  for (size_t i = 0; i < dimensions.size(); i++) {
    index += static_cast<uint32_t>(values[i]) * stride;
    stride *= static_cast<uint32_t>(GetValueCount(dimensions[i]));
  }
  return index;
}

std::vector<std::string> PermutationDomain::defineListFor(uint32_t index) const {
  auto values = decode(index);
  std::vector<std::string> defines;
  defines.reserve(dimensions.size());
  for (size_t i = 0; i < dimensions.size(); i++) {
    std::string entry = GetDefineName(dimensions[i]);
    entry += "=";
    entry += std::to_string(values[i]);
    defines.push_back(std::move(entry));
  }
  return defines;
}

}  // namespace tgfx
