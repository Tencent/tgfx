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
class ShaderSerialization {
 public:
  static std::shared_ptr<Data> Serialize(const Shader* shader, SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap);

 private:
  static void SerializeBasicShaderImpl(flexbuffers::Builder& fbb, const Shader* shader);

  static void SerializeColorShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                       SerializeUtils::ComplexObjSerMap* map);

  static void SerializeColorFilterShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                             SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap);

  static void SerializeImageShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                       SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap);

  static void SerializeBlendShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                       SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap);

  static void SerializeMatrixShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                        SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap);

  static void SerializeGradientShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                          SerializeUtils::ComplexObjSerMap* map);
};
}  // namespace tgfx
