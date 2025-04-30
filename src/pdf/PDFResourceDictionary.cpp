/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
std::string get_resource_name(PDFResourceType type, int index) {
  constexpr char ResourceTypePrefixes[] = {
      'G',  // ExtGState
      'P',  // Pattern
      'X',  // XObject
      'F'   // Font
  };
  return ResourceTypePrefixes[static_cast<size_t>(type)] + std::to_string(index);
}

std::unique_ptr<PDFArray> make_proc_set() {
  auto procSets = MakePDFArray();
  constexpr const char* Procs[] = {"PDF", "Text", "ImageB", "ImageC", "ImageI"};
  procSets->reserve(std::size(Procs));
  for (const char* proc : Procs) {
    procSets->appendName(proc);
  }
  return procSets;
}

const char* resource_name(PDFResourceType type) {
  constexpr const char* ResourceTypeNames[] = {"ExtGState", "Pattern", "XObject", "Font"};
  return ResourceTypeNames[static_cast<size_t>(type)];
}

void add_subdict(const std::vector<PDFIndirectReference>& resourceList, PDFResourceType type,
                 PDFDictionary* destination) {
  if (!resourceList.empty()) {
    auto resources = PDFDictionary::Make();
    for (auto ref : resourceList) {
      resources->insertRef(get_resource_name(type, ref.fValue), ref);
    }
    destination->insertObject(resource_name(type), std::move(resources));
  }
}

}  // namespace

std::unique_ptr<PDFDictionary> MakePDFResourceDictionary(
    const std::vector<PDFIndirectReference>& graphicStateResources,
    const std::vector<PDFIndirectReference>& shaderResources,
    const std::vector<PDFIndirectReference>& xObjectResources,
    const std::vector<PDFIndirectReference>& fontResources) {
  auto dict = PDFDictionary::Make();
  dict->insertObject("ProcSet", make_proc_set());
  add_subdict(graphicStateResources, PDFResourceType::ExtGState, dict.get());
  add_subdict(shaderResources, PDFResourceType::Pattern, dict.get());
  add_subdict(xObjectResources, PDFResourceType::XObject, dict.get());
  add_subdict(fontResources, PDFResourceType::Font, dict.get());
  return dict;
}

void PDFWriteResourceName(const std::shared_ptr<WriteStream>& stream, PDFResourceType type,
                          int key) {
  stream->writeText("/");
  stream->writeText(get_resource_name(type, key));
}

}  // namespace tgfx