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

#include "ShaderCaps.h"
#include "core/utils/Log.h"

namespace tgfx {
static bool HasExtension(const GPUInfo* info, const std::string& extension) {
  auto& extensions = info->extensions;
  return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

static void PrintGPUInfo(const GPUInfo* info) {
  std::string backend;
  switch (info->backend) {
    case Backend::OpenGL:
      backend = "OpenGL";
      break;
    case Backend::Metal:
      backend = "Metal";
      break;
    case Backend::Vulkan:
      backend = "Vulkan";
      break;
    case Backend::WebGPU:
      backend = "WebGPU";
      break;
    case Backend::Unknown:
      backend = "Unknown";
      break;
  }
  LOGI("[GPUInfo] Backend: %s | Version: %s | Renderer: %s | Vendor: %s", backend.c_str(),
       info->version.c_str(), info->renderer.c_str(), info->vendor.c_str());
}

ShaderCaps::ShaderCaps(GPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  auto info = gpu->info();
  PrintGPUInfo(info);
  if (info->backend == Backend::OpenGL) {
    if (info->version.find("OpenGL ES") != std::string::npos) {
      usesPrecisionModifiers = true;
      versionDeclString = "#version 300 es";
    } else {
      usesPrecisionModifiers = false;
      versionDeclString = "#version 150";
    }
  } else {
    usesPrecisionModifiers = false;
    versionDeclString = "#version 450";
  }
  auto limits = gpu->limits();
  maxFragmentSamplers = limits->maxSamplersPerShaderStage;
  maxUBOSize = limits->maxUniformBufferBindingSize;
  uboOffsetAlignment = limits->minUniformBufferOffsetAlignment;
  if (HasExtension(info, "GL_EXT_shader_framebuffer_fetch")) {
    frameBufferFetchNeedsCustomOutput = true;
    frameBufferFetchSupport = true;
    frameBufferFetchColorName = "gl_LastFragData[0]";
    frameBufferFetchExtensionString = "GL_EXT_shader_framebuffer_fetch";
  } else if (HasExtension(info, "GL_NV_shader_framebuffer_fetch")) {
    // Actually, we haven't seen an ES3.0 device with this extension yet, so we don't know.
    frameBufferFetchNeedsCustomOutput = false;
    frameBufferFetchSupport = true;
    frameBufferFetchColorName = "gl_LastFragData[0]";
    frameBufferFetchExtensionString = "GL_NV_shader_framebuffer_fetch";
  } else if (HasExtension(info, "GL_ARM_shader_framebuffer_fetch")) {
    frameBufferFetchNeedsCustomOutput = false;
    frameBufferFetchSupport = true;
    frameBufferFetchColorName = "gl_LastFragColorARM";
    frameBufferFetchExtensionString = "GL_ARM_shader_framebuffer_fetch";
  }
}

}  // namespace tgfx