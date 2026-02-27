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

#include "tgfx/pdf/PDFMetadata.h"
#include <algorithm>
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFTag.h"
#include "pdf/PDFTypes.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

PDFAttributeList::PDFAttributeList() = default;

PDFAttributeList::~PDFAttributeList() = default;

void PDFAttributeList::appendInt(const std::string& owner, const std::string& name, int value) {
  if (!attributes) {
    attributes = MakePDFArray();
  }
  auto attrDict = PDFDictionary::Make();
  attrDict->insertName("O", owner);
  attrDict->insertInt(name.c_str(), value);
  attributes->appendObject(std::move(attrDict));
}

void PDFAttributeList::appendFloat(const std::string& owner, const std::string& name, float value) {
  if (!attributes) {
    attributes = MakePDFArray();
  }
  auto attrDict = PDFDictionary::Make();
  attrDict->insertName("O", owner);
  attrDict->insertScalar(name.c_str(), value);
  attributes->appendObject(std::move(attrDict));
}

void PDFAttributeList::appendName(const std::string& owner, const std::string& name,
                                  const std::string& value) {
  if (!attributes) {
    attributes = MakePDFArray();
  }
  auto attrDict = PDFDictionary::Make();
  attrDict->insertName("O", owner);
  attrDict->insertName(name.c_str(), value);
  attributes->appendObject(std::move(attrDict));
}

void PDFAttributeList::appendFloatArray(const std::string& owner, const std::string& name,
                                        const std::vector<float>& value) {
  if (!attributes) {
    attributes = MakePDFArray();
  }
  auto attrDict = PDFDictionary::Make();
  attrDict->insertName("O", owner);
  auto pdfArray = MakePDFArray();
  for (float element : value) {
    pdfArray->appendScalar(element);
  }
  attrDict->insertObject(name, std::move(pdfArray));
  attributes->appendObject(std::move(attrDict));
}

void PDFAttributeList::appendNodeIdArray(const std::string& owner, const std::string& name,
                                         const std::vector<int>& nodeIds) {
  if (!attributes) {
    attributes = MakePDFArray();
  }
  auto attrDict = PDFDictionary::Make();
  attrDict->insertName("O", owner);
  auto pdfArray = MakePDFArray();
  for (int nodeId : nodeIds) {
    auto idString = PDFTagNode::nodeIdToString(nodeId);
    pdfArray->appendTextString(idString);
  }
  attrDict->insertObject(name, std::move(pdfArray));
  attributes->appendObject(std::move(attrDict));
}

/////////////////////////////////////////////////////////////////////////////////////////////////

std::string DateTime::toISO8601() const {
  int timeZone = static_cast<int>(timeZoneMinutes);
  char timezoneSign = timeZone >= 0 ? '+' : '-';
  int timeZoneHours = std::abs(timeZone) / 60;
  timeZone = std::abs(timeZone) % 60;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u%c%02d:%02d",
           static_cast<uint32_t>(year), static_cast<uint32_t>(month), static_cast<uint32_t>(day),
           static_cast<uint32_t>(hour), static_cast<uint32_t>(minute),
           static_cast<uint32_t>(second), timezoneSign, timeZoneHours, timeZoneMinutes);
  return std::string(buffer);
}

}  // namespace tgfx
