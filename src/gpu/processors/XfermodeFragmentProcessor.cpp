/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "XfermodeFragmentProcessor.h"
#include <algorithm>
#include "gpu/UniformData.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromSrcProcessor(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> src, BlendMode mode) {
  return MakeFromTwoProcessors(allocator, std::move(src), nullptr, mode);
}

PlacementPtr<FragmentProcessor> XfermodeFragmentProcessor::MakeFromDstProcessor(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> dst, BlendMode mode) {
  return MakeFromTwoProcessors(allocator, nullptr, std::move(dst), mode);
}

std::string XfermodeFragmentProcessor::name() const {
  switch (child) {
    case Child::TwoChild:
      return "XfermodeFragmentProcessor - two";
    case Child::DstChild:
      return "XfermodeFragmentProcessor - dst";
    case Child::SrcChild:
      return "XfermodeFragmentProcessor - src";
  }
  return "XfermodeFragmentProcessor";
}

XfermodeFragmentProcessor::XfermodeFragmentProcessor(PlacementPtr<FragmentProcessor> src,
                                                     PlacementPtr<FragmentProcessor> dst,
                                                     BlendMode mode)
    : FragmentProcessor(ClassID()), mode(mode) {
  if (src && dst) {
    child = Child::TwoChild;
    registerChildProcessor(std::move(src));
    registerChildProcessor(std::move(dst));
  } else if (src) {
    child = Child::SrcChild;
    registerChildProcessor(std::move(src));
  } else {
    child = Child::DstChild;
    registerChildProcessor(std::move(dst));
  }
}

void XfermodeFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  if (fragmentUniformData == nullptr) {
    return;
  }
  if (fragmentUniformData->hasField("BlendModeValue")) {
    int modeInt = static_cast<int>(mode);
    fragmentUniformData->setData("BlendModeValue", modeInt);
  }
  if (fragmentUniformData->hasField("HasClip")) {
    int hasClip = 0;
    fragmentUniformData->setData("HasClip", hasClip);
  }
  // Populate the per-child subset uniforms declared by the precompiled BlendMergeShader. Each plain
  // TextureEffect child that requires subset clamping writes its normalized subset rect into a
  // dedicated uniform (Child0Subset for child[0], Child1Subset for child[1]). Distinct names are
  // required because the precompiled path strips uniform name suffixes: if we relied on the
  // children's own "Subset" emission, the two texture children would collide on a single field and
  // the last writer would overwrite the first. On the ProgramBuilder path these fields do not
  // exist, so hasField() returns false and the writes are skipped.
  static const char* const subsetFieldNames[] = {"Child0Subset", "Child1Subset"};
  size_t childCount = std::min<size_t>(numChildProcessors(), 2);
  for (size_t i = 0; i < childCount; i++) {
    auto childFP = childProcessor(i);
    if (childFP == nullptr || childFP->name() != "TextureEffect") {
      continue;
    }
    if (!fragmentUniformData->hasField(subsetFieldNames[i])) {
      continue;
    }
    auto childTE = static_cast<const TextureEffect*>(childFP);
    float rect[4];
    childTE->computeSubsetRect(rect);
    fragmentUniformData->setData(subsetFieldNames[i], rect);
  }
}

void XfermodeFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(mode) | (static_cast<uint32_t>(child) << 16));
}
}  // namespace tgfx
