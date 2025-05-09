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

#pragma once

#include <tgfx/core/Data.h>
#include "SerializationUtils.h"

namespace tgfx {

class ColorFilterSerialization {
 public:
  static std::shared_ptr<Data> serializeColorFilter(ColorFilter* colorFilter);

 private:
  static void serializeColorFilterImpl(flexbuffers::Builder& fbb, ColorFilter* colorFilter);
  static void serializeComposeColorFilterImpl(flexbuffers::Builder& fbb, ColorFilter* colorFilter);
  static void serializeAlphaThreadholdColorFilterImpl(flexbuffers::Builder& fbb,
                                                      ColorFilter* colorFilter);
  static void serializeMatrixColorFilterImpl(flexbuffers::Builder& fbb, ColorFilter* colorFilter);
  static void serializeModeColorFilterImpl(flexbuffers::Builder& fbb, ColorFilter* colorFilter);
};
}  // namespace tgfx
