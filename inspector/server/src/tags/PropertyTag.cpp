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

#include "PropertyTag.h"
#include "DataContext.h"
#include "TagUtils.h"

namespace inspector {
void ReadPropertyTag(DecodeStream* stream) {
  auto count = stream->readEncodedUint32();
  auto context = dynamic_cast<DataContext*>(stream->context);
  auto& properties = context->properties;
  for (uint32_t i = 0; i < count; ++i) {
    auto opIndex = stream->readEncodedUint32();
    auto ptr = std::make_shared<PropertyData>();
    ReadDataHead(ptr->summaryName, stream);
    ReadDataHead(ptr->processName, stream);

    auto summaryDataCount = stream->readEncodedUint32();
    ptr->summaryData.reserve(summaryDataCount);
    for (uint32_t j = 0; j < summaryDataCount; ++j) {
      auto data = stream->readData();
      ptr->summaryData.push_back(data);
    }

    auto processDataCount = stream->readEncodedUint32();
    for (uint32_t j = 0; j < processDataCount; ++j) {
      auto data = stream->readData();
      ptr->processData.push_back(data);
    }
    properties[opIndex] = ptr;
  }
}

TagType WritePropertyTag(EncodeStream* stream,
                         std::unordered_map<uint32_t, std::shared_ptr<PropertyData>>* properties) {
  stream->writeEncodedUint32(static_cast<uint32_t>(properties->size()));
  for (const auto& property : *properties) {
    stream->writeEncodedUint32(property.first);
    WriteDataHead(property.second->summaryName, stream);
    WriteDataHead(property.second->processName, stream);

    const auto& summaryData = property.second->summaryData;
    const auto summaryDataCount = static_cast<uint32_t>(summaryData.size());
    const auto& processData = property.second->processData;
    const auto processDataCount = static_cast<uint32_t>(processData.size());

    stream->writeEncodedUint32(summaryDataCount);
    for (auto& data : summaryData) {
      stream->writeData(data.get());
    }

    stream->writeEncodedUint32(processDataCount);
    for (auto& data : processData) {
      stream->writeData(data.get());
    }
  }
  return TagType::Property;
}
}  // namespace inspector