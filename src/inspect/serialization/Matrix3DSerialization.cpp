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

#include "MatrixSerialization.h"

namespace tgfx::Matrix3DSerialization {

static void SerializeMatrix3DImpl(flexbuffers::Builder& fbb, const Matrix3D* matrix) {
  float buffer[16] = {0.0f};
  matrix->getColumnMajor(buffer);
  std::string key = "";
  for (int i = 0; i < 16; i++) {
    key = "[" + std::to_string(i) + "]";
    SerializeUtils::SetFlexBufferMap(fbb, key.c_str(), buffer[i]);
  }
}

std::shared_ptr<Data> Serialize(const Matrix3D* matrix) {
  DEBUG_ASSERT(matrix != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute, startMap,
                                 contentMap);
  SerializeMatrix3DImpl(fbb, matrix);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

}  // namespace tgfx::Matrix3DSerialization