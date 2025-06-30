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

#include "NameMapTag.h"
#include "DataContext.h"

namespace inspector {
void ReadNameMapTag(DecodeStream* stream) {
  auto count = stream->readEncodedUint64();
  auto context = dynamic_cast<DataContext*>(stream->context);
  auto& nameMap = context->nameMap;
  for (uint32_t i = 0; i < count; ++i) {
    auto namePtr = stream->readEncodedUint64();
    auto name = stream->readUTF8String();
    nameMap[namePtr] = name;
  }
}

TagType WriteNameMapTag(EncodeStream* stream,
                        const std::unordered_map<uint64_t, std::string>* nameMap) {
  stream->writeEncodedUint64(nameMap->size());
  for (const auto& nameMapIter : *nameMap) {
    stream->writeEncodedUint64(nameMapIter.first);
    stream->writeUTF8String(nameMapIter.second);
  }
  return TagType::NameMap;
}
}  // namespace inspector