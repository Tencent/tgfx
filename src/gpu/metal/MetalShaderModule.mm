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

#include "MetalShaderModule.h"
#include <string>
#include "MetalGPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderInterface.h"

namespace tgfx {

std::shared_ptr<MetalShaderModule> MetalShaderModule::Make(
    MetalGPU* gpu, const ShaderModuleDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }

  return gpu->makeResource<MetalShaderModule>(gpu, descriptor);
}

MetalShaderModule::MetalShaderModule(MetalGPU* gpu, const ShaderModuleDescriptor& descriptor)
    : VaryingShaderModule(ExtractModuleVaryingDecls(descriptor), {}), _stage(descriptor.stage) {
  // Precompiled artifacts and native formats carry no parseable GLSL for the runtime slot
  // collection to scan, so the block bindings arrive via the descriptor.
  setUniformSlots(descriptor.uniformSlots);
  NSError* error = nil;
  switch (descriptor.format) {
    case ShaderCodeFormat::METALLIB: {
      // Precompiled metallib: load directly without compilation.
      if (descriptor.binaryData.empty()) {
        LOGE("MetalShaderModule: precompiled metallib is empty");
        return;
      }
      dispatch_data_t data =
          dispatch_data_create(descriptor.binaryData.data(), descriptor.binaryData.size(),
                               dispatch_get_global_queue(0, 0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
      library = [gpu->device() newLibraryWithData:data error:&error];
      dispatch_release(data);
      break;
    }
    case ShaderCodeFormat::MSL: {
      if (descriptor.code.empty()) {
        LOGE("MetalShaderModule: MSL source is empty");
        return;
      }
      NSString* source = [NSString stringWithUTF8String:descriptor.code.c_str()];
      library = [gpu->device() newLibraryWithSource:source options:nil error:&error];
      break;
    }
    default:
      LOGE("MetalShaderModule: unsupported shader format for the Metal backend");
      return;
  }
  if (!library) {
    LOGE("MetalShaderModule: library creation failed: %s",
         error != nil ? error.localizedDescription.UTF8String : "unknown error");
  }
}

void MetalShaderModule::onRelease(MetalGPU*) {
  if (library != nil) {
    [library release];
    library = nil;
  }
}

}  // namespace tgfx
