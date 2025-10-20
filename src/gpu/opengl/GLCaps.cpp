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

#include "GLCaps.h"
#include "GLUtil.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
static GLStandard GetGLStandard(const char* versionString) {
  if (versionString == nullptr) {
    return GLStandard::None;
  }
  int major, minor;
  int n = sscanf(versionString, "%d.%d", &major, &minor);
  if (2 == n) {
    return GLStandard::GL;
  }
  int esMajor, esMinor;
  n = sscanf(versionString, "OpenGL ES %d.%d (WebGL %d.%d", &esMajor, &esMinor, &major, &minor);
  if (4 == n) {
    return GLStandard::WebGL;
  }

  // check for ES 1
  char profile[2];
  n = sscanf(versionString, "OpenGL ES-%c%c %d.%d", profile, profile + 1, &major, &minor);
  if (4 == n) {
    // we no longer support ES1.
    return GLStandard::None;
  }

  // check for ES2
  n = sscanf(versionString, "OpenGL ES %d.%d", &major, &minor);
  if (2 == n) {
    return GLStandard::GLES;
  }
  return GLStandard::None;
}

static GLVendor GetVendorFromString(const char* vendorString) {
  if (vendorString) {
    if (0 == strcmp(vendorString, "ARM")) {
      return GLVendor::ARM;
    }
    if (0 == strcmp(vendorString, "Google Inc.")) {
      return GLVendor::Google;
    }
    if (0 == strcmp(vendorString, "Imagination Technologies")) {
      return GLVendor::Imagination;
    }
    if (0 == strncmp(vendorString, "Intel ", 6) || 0 == strcmp(vendorString, "Intel")) {
      return GLVendor::Intel;
    }
    if (0 == strcmp(vendorString, "Qualcomm")) {
      return GLVendor::Qualcomm;
    }
    if (0 == strcmp(vendorString, "NVIDIA Corporation")) {
      return GLVendor::NVIDIA;
    }
    if (0 == strcmp(vendorString, "ATI Technologies Inc.")) {
      return GLVendor::ATI;
    }
  }
  return GLVendor::Other;
}

GLInfo::GLInfo(GLGetString* getString, GLGetStringi* getStringi, GLGetIntegerv* getIntegerv,
               GLGetInternalformativ* getInternalformativ,
               GLGetShaderPrecisionFormat getShaderPrecisionFormat)
    : getString(getString), getStringi(getStringi), getIntegerv(getIntegerv),
      getInternalformativ(getInternalformativ), getShaderPrecisionFormat(getShaderPrecisionFormat) {
  auto versionString = (const char*)getString(GL_VERSION);
  LOGI("OpenGL Version: %s\n", versionString);
  auto glVersion = GetGLVersion(versionString);
  version = GL_VER(glVersion.majorVersion, glVersion.minorVersion);
  standard = GetGLStandard(versionString);
  fetchExtensions();
}

void GLInfo::fetchExtensions() {
  // WebGL (1.0 or 2.0) doesn't natively support glGetStringi, but emscripten adds it in
  // https://github.com/emscripten-core/emscripten/issues/3472
  if (getStringi) {
    int extensionCount = 0;
    getIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (int i = 0; i < extensionCount; ++i) {
      const char* ext =
          reinterpret_cast<const char*>(getStringi(GL_EXTENSIONS, static_cast<unsigned>(i)));
      extensions.emplace_back(ext);
    }
  }
}

bool GLInfo::hasExtension(const std::string& extension) const {
  auto result = std::find(extensions.begin(), extensions.end(), extension);
  return result != extensions.end();
}

const GLCaps* GLCaps::Get(Context* context) {
  return context ? static_cast<const GLCaps*>(context->caps()) : nullptr;
}

GLCaps::GLCaps(const GLInfo& info) {
  standard = info.standard;
  version = info.version;
  vendor = GetVendorFromString((const char*)info.getString(GL_VENDOR));
  fenceSupport = true;
  switch (standard) {
    case GLStandard::GL:
      if (version < GL_VER(3, 2)) {
        ABORT("Fatal error: Desktop OpenGL versions below 3.2 are not supported!");
      }
      initGLSupport(info);
      break;
    case GLStandard::GLES:
      if (version < GL_VER(3, 0)) {
        ABORT("Fatal error: OpenGL ES versions below 3.0 are not supported!");
      }
      initGLESSupport(info);
      break;
    case GLStandard::WebGL:
      if (version < GL_VER(2, 0)) {
        ABORT("Fatal error: WebGL versions below 2.0 are not supported!");
      }
      initWebGLSupport(info);
      break;
    default:
      break;
  }
  info.getIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  info.getIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &_shaderCaps.maxFragmentSamplers);
  info.getIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &_shaderCaps.maxUBOSize);
  info.getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &_shaderCaps.uboOffsetAlignment);
  if (vendor == GLVendor::Qualcomm) {
    // https://skia-review.googlesource.com/c/skia/+/571418
    // On certain Adreno devices running WebGL, glTexSubImage2D() may not upload texels in time for
    // sampling. Similar issues have also been observed with Android OpenGL ES. To work around this,
    // call glFlush() before glTexSubImage2D().
    flushBeforeWritePixels = true;
  }
  initFormatMap(info);
}

const GLTextureFormat& GLCaps::getTextureFormat(PixelFormat pixelFormat) const {
  return pixelFormatMap.at(pixelFormat).format;
}

const Swizzle& GLCaps::getReadSwizzle(PixelFormat pixelFormat) const {
  return pixelFormatMap.at(pixelFormat).readSwizzle;
}

const Swizzle& GLCaps::getWriteSwizzle(PixelFormat pixelFormat) const {
  return pixelFormatMap.at(pixelFormat).writeSwizzle;
}

bool GLCaps::isFormatRenderable(PixelFormat pixelFormat) const {
  switch (pixelFormat) {
    case PixelFormat::RGBA_8888:
    case PixelFormat::BGRA_8888:
    case PixelFormat::ALPHA_8:
      return true;
    default:
      break;
  }
  return false;
}

int GLCaps::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  if (requestedCount <= 1) {
    return 1;
  }
  auto result = pixelFormatMap.find(pixelFormat);
  if (result == pixelFormatMap.end() || result->second.colorSampleCounts.empty()) {
    return 1;
  }
  for (auto colorSampleCount : result->second.colorSampleCounts) {
    if (colorSampleCount >= requestedCount) {
      return colorSampleCount;
    }
  }
  return 1;
}

void GLCaps::initGLSupport(const GLInfo& info) {
  pboSupport = true;
  multisampleDisableSupport = true;
  textureBarrierSupport = vendor != GLVendor::Intel &&
                          (version >= GL_VER(4, 5) || info.hasExtension("GL_ARB_texture_barrier") ||
                           info.hasExtension("GL_NV_texture_barrier"));
  clampToBorderSupport = true;
  _shaderCaps.versionDeclString = "#version 140";
  _shaderCaps.usesPrecisionModifiers = false;
  if (info.hasExtension("GL_EXT_shader_framebuffer_fetch")) {
    _shaderCaps.frameBufferFetchNeedsCustomOutput = true;
    _shaderCaps.frameBufferFetchSupport = true;
    _shaderCaps.frameBufferFetchColorName = "gl_LastFragData[0]";
    _shaderCaps.frameBufferFetchExtensionString = "GL_EXT_shader_framebuffer_fetch";
    _shaderCaps.frameBufferFetchRequiresEnablePerSample = false;
  }
}

void GLCaps::initGLESSupport(const GLInfo& info) {
  pboSupport = true;
  multisampleDisableSupport = info.hasExtension("GL_EXT_multisample_compatibility");
  textureBarrierSupport = info.hasExtension("GL_NV_texture_barrier");
  clampToBorderSupport = version > GL_VER(3, 2) ||
                         info.hasExtension("GL_EXT_texture_border_clamp") ||
                         info.hasExtension("GL_NV_texture_border_clamp") ||
                         info.hasExtension("GL_OES_texture_border_clamp");
  _shaderCaps.versionDeclString = "#version 300 es";
  if (info.hasExtension("GL_EXT_shader_framebuffer_fetch")) {
    _shaderCaps.frameBufferFetchNeedsCustomOutput = true;
    _shaderCaps.frameBufferFetchSupport = true;
    _shaderCaps.frameBufferFetchColorName = "gl_LastFragData[0]";
    _shaderCaps.frameBufferFetchExtensionString = "GL_EXT_shader_framebuffer_fetch";
    _shaderCaps.frameBufferFetchRequiresEnablePerSample = false;
  } else if (info.hasExtension("GL_NV_shader_framebuffer_fetch")) {
    // Actually, we haven't seen an ES3.0 device with this extension yet, so we don't know.
    _shaderCaps.frameBufferFetchNeedsCustomOutput = false;
    _shaderCaps.frameBufferFetchSupport = true;
    _shaderCaps.frameBufferFetchColorName = "gl_LastFragData[0]";
    _shaderCaps.frameBufferFetchExtensionString = "GL_NV_shader_framebuffer_fetch";
    _shaderCaps.frameBufferFetchRequiresEnablePerSample = false;
  } else if (info.hasExtension("GL_ARM_shader_framebuffer_fetch")) {
    _shaderCaps.frameBufferFetchNeedsCustomOutput = false;
    _shaderCaps.frameBufferFetchSupport = true;
    _shaderCaps.frameBufferFetchColorName = "gl_LastFragColorARM";
    _shaderCaps.frameBufferFetchExtensionString = "GL_ARM_shader_framebuffer_fetch";
    // The arm extension requires specifically enabling MSAA fetching per sample.
    // On some devices this may have a perf hit. Also multiple render targets are disabled
    _shaderCaps.frameBufferFetchRequiresEnablePerSample = true;
  }
  _shaderCaps.usesPrecisionModifiers = true;
}

void GLCaps::initWebGLSupport(const GLInfo&) {
  pboSupport = false;
  multisampleDisableSupport = false;
  textureBarrierSupport = false;
  clampToBorderSupport = false;
  _shaderCaps.versionDeclString = "#version 300 es";
  _shaderCaps.frameBufferFetchSupport = false;
  _shaderCaps.usesPrecisionModifiers = true;
}

void GLCaps::initFormatMap(const GLInfo& info) {
  auto& RGBAFormat = pixelFormatMap[PixelFormat::RGBA_8888];
  RGBAFormat.format.sizedFormat = GL_RGBA8;
  RGBAFormat.format.externalFormat = GL_RGBA;
  RGBAFormat.format.externalType = GL_UNSIGNED_BYTE;
  RGBAFormat.readSwizzle = Swizzle::RGBA();
  auto& BGRAFormat = pixelFormatMap[PixelFormat::BGRA_8888];
  BGRAFormat.format.sizedFormat = GL_RGBA8;
  BGRAFormat.format.externalFormat = GL_BGRA;
  BGRAFormat.format.externalType = GL_UNSIGNED_BYTE;
  BGRAFormat.readSwizzle = Swizzle::RGBA();
  auto& DEPTHSTENCILFormat = pixelFormatMap[PixelFormat::DEPTH24_STENCIL8];
  DEPTHSTENCILFormat.format.sizedFormat = GL_DEPTH24_STENCIL8;
  DEPTHSTENCILFormat.format.externalFormat = GL_DEPTH_STENCIL;
  DEPTHSTENCILFormat.format.externalType = GL_UNSIGNED_INT_24_8;
  auto& ALPHAFormat = pixelFormatMap[PixelFormat::ALPHA_8];
  ALPHAFormat.format.sizedFormat = GL_R8;
  ALPHAFormat.format.externalFormat = GL_RED;
  ALPHAFormat.format.externalType = GL_UNSIGNED_BYTE;
  ALPHAFormat.readSwizzle = Swizzle::RRRR();
  // Shader output swizzles will default to RGBA. When we've use GL_RED instead of GL_ALPHA to
  // implement PixelFormat::ALPHA_8 we need to swizzle the shader outputs so the alpha channel
  // gets written to the single component.
  ALPHAFormat.writeSwizzle = Swizzle::AAAA();
  auto& GrayFormat = pixelFormatMap[PixelFormat::GRAY_8];
  GrayFormat.format.sizedFormat = GL_R8;
  GrayFormat.format.externalFormat = GL_RED;
  GrayFormat.format.externalType = GL_UNSIGNED_BYTE;
  GrayFormat.readSwizzle = Swizzle::RRRA();
  auto& RGFormat = pixelFormatMap[PixelFormat::RG_88];
  RGFormat.format.sizedFormat = GL_RG8;
  RGFormat.format.externalFormat = GL_RG;
  RGFormat.format.externalType = GL_UNSIGNED_BYTE;
  RGFormat.readSwizzle = Swizzle::RGRG();

  bool useSizedRbFormats = standard == GLStandard::GLES || standard == GLStandard::WebGL;
  for (auto& item : pixelFormatMap) {
    auto& format = item.second.format;
    format.internalFormatTexImage = format.sizedFormat;
    format.internalFormatRenderBuffer =
        useSizedRbFormats ? format.sizedFormat : format.externalFormat;
  }
  if (info.hasExtension("GL_APPLE_texture_format_BGRA8888") ||
      info.hasExtension("GL_EXT_texture_format_BGRA8888")) {
    pixelFormatMap[PixelFormat::BGRA_8888].format.internalFormatTexImage = GL_RGBA;
  }
  initColorSampleCount(info);
}

static bool UsesInternalformatQuery(GLStandard standard, const GLInfo& glInterface,
                                    uint32_t version) {
  return (standard == GLStandard::GL &&
          (version >= GL_VER(4, 2) || glInterface.hasExtension("GL_ARB_internalformat_query"))) ||
         standard == GLStandard::GLES;
}

void GLCaps::initColorSampleCount(const GLInfo& info) {
  std::vector<PixelFormat> pixelFormats = {PixelFormat::RGBA_8888, PixelFormat::ALPHA_8};
  for (auto pixelFormat : pixelFormats) {
    if (vendor == GLVendor::Intel) {
      // We disable MSAA across the board for Intel GPUs for performance reasons.
      pixelFormatMap[pixelFormat].colorSampleCounts.push_back(1);
    } else if (UsesInternalformatQuery(standard, info, version)) {
      int count = 0;
      unsigned format = pixelFormatMap[pixelFormat].format.internalFormatRenderBuffer;
      info.getInternalformativ(GL_RENDERBUFFER, format, GL_NUM_SAMPLE_COUNTS, 1, &count);
      if (count) {
        int* temp = new int[static_cast<size_t>(count)];
        info.getInternalformativ(GL_RENDERBUFFER, format, GL_SAMPLES, count, temp);
        // GL has a concept of MSAA rasterization with a single sample but we do not.
        if (temp[count - 1] == 1) {
          --count;
        }
        // We initialize our supported values with 1 (no msaa) and reverse the order returned by GL
        // so that the array is ascending.
        pixelFormatMap[pixelFormat].colorSampleCounts.push_back(1);
        for (int j = 0; j < count; ++j) {
          pixelFormatMap[pixelFormat].colorSampleCounts.push_back(temp[count - j - 1]);
        }
        delete[] temp;
      }
    } else {
      // Fake out the table using some semi-standard counts up to the max allowed sample count.
      int maxSampleCnt = 1;
      info.getIntegerv(GL_MAX_SAMPLES, &maxSampleCnt);
      // Chrome has a mock GL implementation that returns 0.
      maxSampleCnt = std::max(1, maxSampleCnt);

      std::vector<int> defaultSamples{1, 2, 4, 8};
      for (auto samples : defaultSamples) {
        if (samples > maxSampleCnt) {
          break;
        }
        pixelFormatMap[pixelFormat].colorSampleCounts.push_back(samples);
      }
    }
  }
}
}  // namespace tgfx
