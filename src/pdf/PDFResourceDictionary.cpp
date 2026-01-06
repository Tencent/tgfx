/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "PDFResourceDictionary.h"
#include "pdf/PDFTypes.h"

namespace tgfx {

namespace {
std::string GetResourceName(PDFResourceType type, int index) {
  constexpr char ResourceTypePrefixes[] = {
      'G',  // ExtGState
      'P',  // Pattern
      'X',  // XObject
      'F'   // Font
  };
  return ResourceTypePrefixes[static_cast<size_t>(type)] + std::to_string(index);
}

std::unique_ptr<PDFArray> MakeProcSet() {
  auto procSets = MakePDFArray();
  constexpr const char* Procs[] = {"PDF", "Text", "ImageB", "ImageC", "ImageI"};
  procSets->reserve(std::size(Procs));
  for (const char* proc : Procs) {
    procSets->appendName(proc);
  }
  return procSets;
}

const char* ResourceName(PDFResourceType type) {
  constexpr const char* ResourceTypeNames[] = {"ExtGState", "Pattern", "XObject", "Font"};
  return ResourceTypeNames[static_cast<size_t>(type)];
}

void AddSubDictionary(const std::vector<PDFIndirectReference>& resourceList, PDFResourceType type,
                      PDFDictionary* destination) {
  if (!resourceList.empty()) {
    auto resources = PDFDictionary::Make();
    for (auto ref : resourceList) {
      resources->insertRef(GetResourceName(type, ref.value), ref);
    }
    destination->insertObject(ResourceName(type), std::move(resources));
  }
}

}  // namespace

std::unique_ptr<PDFDictionary> MakePDFResourceDictionary(
    const std::vector<PDFIndirectReference>& graphicStateResources,
    const std::vector<PDFIndirectReference>& shaderResources,
    const std::vector<PDFIndirectReference>& xObjectResources,
    const std::vector<PDFIndirectReference>& fontResources) {
  auto dict = PDFDictionary::Make();
  dict->insertObject("ProcSet", MakeProcSet());
  AddSubDictionary(graphicStateResources, PDFResourceType::ExtGState, dict.get());
  AddSubDictionary(shaderResources, PDFResourceType::Pattern, dict.get());
  AddSubDictionary(xObjectResources, PDFResourceType::XObject, dict.get());
  AddSubDictionary(fontResources, PDFResourceType::Font, dict.get());
  return dict;
}

void PDFWriteResourceName(const std::shared_ptr<WriteStream>& stream, PDFResourceType type,
                          int key) {
  stream->writeText("/");
  stream->writeText(GetResourceName(type, key));
}

}  // namespace tgfx
