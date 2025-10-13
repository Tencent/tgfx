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

static void eatSpaceSepStrings(std::vector<std::string>* out, const char text[]) {
  if (!text) {
    return;
  }
  while (true) {
    while (' ' == *text) {
      ++text;
    }
    if ('\0' == *text) {
      break;
    }
    size_t length = strcspn(text, " ");
    out->emplace_back(text, length);
    text += length;
  }
}

void GLInfo::fetchExtensions() {
  bool indexed = false;
  if (standard == GLStandard::GL || standard == GLStandard::GLES) {
    // glGetStringi and indexed extensions were added in version 3.0 of desktop GL and ES.
    indexed = version >= GL_VER(3, 0);
  } else if (standard == GLStandard::WebGL) {
    // WebGL (1.0 or 2.0) doesn't natively support glGetStringi, but emscripten adds it in
    // https://github.com/emscripten-core/emscripten/issues/3472
    indexed = version >= GL_VER(2, 0);
  }
  if (indexed) {
    if (getStringi) {
      int extensionCount = 0;
      getIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
      for (int i = 0; i < extensionCount; ++i) {
        const char* ext =
            reinterpret_cast<const char*>(getStringi(GL_EXTENSIONS, static_cast<unsigned>(i)));
        extensions.emplace_back(ext);
      }
    }
  } else {
    auto text = reinterpret_cast<const char*>(getString(GL_EXTENSIONS));
    eatSpaceSepStrings(&extensions, text);
  }
}

bool GLInfo::hasExtension(const std::string& extension) const {
  auto result = std::find(extensions.begin(), extensions.end(), extension);
  return result != extensions.end();
}

static bool IsMediumFloatFp32(const GLInfo& ctxInfo) {
  if (ctxInfo.standard == GLStandard::GL && ctxInfo.version < GL_VER(4, 1) &&
      !ctxInfo.hasExtension("GL_ARB_ES2_compatibility")) {
    // We're on a desktop GL that doesn't have precision info. Assume they're all 32bit float.
    return true;
  }
  // glGetShaderPrecisionFormat doesn't accept GL_GEOMETRY_SHADER as a shader type. Hopefully the
  // geometry shaders don't have lower precision than vertex and fragment.
  for (auto shader : {GL_FRAGMENT_SHADER, GL_VERTEX_SHADER}) {
    int range[2];
    int bits;
    ctxInfo.getShaderPrecisionFormat(static_cast<unsigned>(shader), GL_MEDIUM_FLOAT, range, &bits);
    if (range[0] < 127 || range[1] < 127 || bits < 23) {
      return false;
    }
  }
  return true;
}

const GLCaps* GLCaps::Get(Context* context) {
  return context ? static_cast<const GLCaps*>(context->caps()) : nullptr;
}

GLCaps::GLCaps(const GLInfo& info) {
  standard = info.standard;
  version = info.version;
  vendor = GetVendorFromString((const char*)info.getString(GL_VENDOR));
  _shaderCaps.floatIs32Bits = IsMediumFloatFp32(info);
  switch (standard) {
    case GLStandard::GL:
      initGLSupport(info);
      break;
    case GLStandard::GLES:
      initGLESSupport(info);
      break;
    case GLStandard::WebGL:
      initWebGLSupport(info);
      break;
    default:
      break;
  }
  info.getIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  info.getIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &_shaderCaps.maxFragmentSamplers);
  if (vendor == GLVendor::Qualcomm) {
    // https://skia-review.googlesource.com/c/skia/+/571418
    // On certain Adreno devices running WebGL, glTexSubImage2D() may not upload texels in time for
    // sampling. Similar issues have also been observed with Android OpenGL ES. To work around this,
    // call glFlush() before glTexSubImage2D().
    flushBeforeWritePixels = true;
  }
  initMSAASupport(info);
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
      return true;
    case PixelFormat::ALPHA_8:
      if (textureRedSupport) {
        return true;
      }
      break;
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
  packRowLengthSupport = true;
  unpackRowLengthSupport = true;
  vertexArrayObjectSupport = version >= GL_VER(3, 0) ||
                             info.hasExtension("GL_ARB_vertex_array_object") ||
                             info.hasExtension("GL_APPLE_vertex_array_object");
  textureRedSupport = version >= GL_VER(3, 0) || info.hasExtension("GL_ARB_texture_rg");
  multisampleDisableSupport = true;
  if (vendor != GLVendor::Intel) {
    textureBarrierSupport = version >= GL_VER(4, 5) ||
                            info.hasExtension("GL_ARB_texture_barrier") ||
                            info.hasExtension("GL_NV_texture_barrier");
  }
  semaphoreSupport = version >= GL_VER(3, 2) || info.hasExtension("GL_ARB_sync");
  if (version < GL_VER(1, 3) && !info.hasExtension("GL_ARB_texture_border_clamp")) {
    clampToBorderSupport = false;
  }
  _shaderCaps.versionDeclString = "#version 140";
  _shaderCaps.usesCustomColorOutputName = true;
  _shaderCaps.varyingIsInOut = true;
  _shaderCaps.textureFuncName = "texture";
  if (info.hasExtension("GL_EXT_shader_framebuffer_fetch")) {
    _shaderCaps.frameBufferFetchNeedsCustomOutput = version >= GL_VER(3, 0);
    _shaderCaps.frameBufferFetchSupport = true;
    _shaderCaps.frameBufferFetchColorName = "gl_LastFragData[0]";
    _shaderCaps.frameBufferFetchExtensionString = "GL_EXT_shader_framebuffer_fetch";
    _shaderCaps.frameBufferFetchRequiresEnablePerSample = false;
  }

  _shaderCaps.uboSupport = version >= GL_VER(3, 1);
  if (_shaderCaps.uboSupport) {
    info.getIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &_shaderCaps.maxUBOSize);
    info.getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &_shaderCaps.uboOffsetAlignment);
  }
}

void GLCaps::initGLESSupport(const GLInfo& info) {
  packRowLengthSupport = version >= GL_VER(3, 0) || info.hasExtension("GL_NV_pack_subimage");
  unpackRowLengthSupport = version >= GL_VER(3, 0) || info.hasExtension("GL_EXT_unpack_subimage");
  vertexArrayObjectSupport =
      version >= GL_VER(3, 0) || info.hasExtension("GL_OES_vertex_array_object");
  textureRedSupport = version >= GL_VER(3, 0) || info.hasExtension("GL_EXT_texture_rg");
  multisampleDisableSupport = info.hasExtension("GL_EXT_multisample_compatibility");
  textureBarrierSupport = info.hasExtension("GL_NV_texture_barrier");
  _shaderCaps.versionDeclString = version >= GL_VER(3, 0) ? "#version 300 es" : "#version 100";
  _shaderCaps.usesCustomColorOutputName = version >= GL_VER(3, 0);
  _shaderCaps.varyingIsInOut = version >= GL_VER(3, 0);
  _shaderCaps.textureFuncName = version >= GL_VER(3, 0) ? "texture" : "texture2D";
  _shaderCaps.oesTextureExtension =
      version >= GL_VER(3, 0) ? "GL_OES_EGL_image_external_essl3" : "GL_OES_EGL_image_external";
  if (info.hasExtension("GL_EXT_shader_framebuffer_fetch")) {
    _shaderCaps.frameBufferFetchNeedsCustomOutput = version >= GL_VER(3, 0);
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
  semaphoreSupport = version >= GL_VER(3, 0) || info.hasExtension("GL_APPLE_sync");
  if (version < GL_VER(3, 2) && !info.hasExtension("GL_EXT_texture_border_clamp") &&
      !info.hasExtension("GL_NV_texture_border_clamp") &&
      !info.hasExtension("GL_OES_texture_border_clamp")) {
    clampToBorderSupport = false;
  }
  npotTextureTileSupport = version >= GL_VER(3, 0) || info.hasExtension("GL_OES_texture_npot");
  mipmapSupport = npotTextureTileSupport || info.hasExtension("GL_IMG_texture_npot");
  _shaderCaps.usesPrecisionModifiers = true;

  _shaderCaps.uboSupport = version >= GL_VER(3, 0);
  if (_shaderCaps.uboSupport) {
    info.getIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &_shaderCaps.maxUBOSize);
    info.getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &_shaderCaps.uboOffsetAlignment);
  }
}

void GLCaps::initWebGLSupport(const GLInfo& info) {
  packRowLengthSupport = version >= GL_VER(2, 0);
  unpackRowLengthSupport = version >= GL_VER(2, 0);
  vertexArrayObjectSupport = version >= GL_VER(2, 0) ||
                             info.hasExtension("GL_OES_vertex_array_object") ||
                             info.hasExtension("OES_vertex_array_object");
  textureRedSupport = version >= GL_VER(2, 0);
  multisampleDisableSupport = false;
  textureBarrierSupport = false;
  semaphoreSupport = version >= GL_VER(2, 0);
  clampToBorderSupport = false;
  npotTextureTileSupport = version >= GL_VER(2, 0);
  mipmapSupport = npotTextureTileSupport;
  _shaderCaps.usesCustomColorOutputName = version >= GL_VER(2, 0);
  _shaderCaps.varyingIsInOut = version >= GL_VER(2, 0);
  _shaderCaps.versionDeclString = version >= GL_VER(2, 0) ? "#version 300 es" : "#version 100";
  _shaderCaps.textureFuncName = version >= GL_VER(2, 0) ? "texture" : "texture2D";
  _shaderCaps.frameBufferFetchSupport = false;
  _shaderCaps.usesPrecisionModifiers = true;

  _shaderCaps.uboSupport = version >= GL_VER(2, 0);
  if (_shaderCaps.uboSupport) {
    info.getIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &_shaderCaps.maxUBOSize);
    info.getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &_shaderCaps.uboOffsetAlignment);
  }
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
  if (textureRedSupport) {
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
  } else {
    auto& ALPHAFormat = pixelFormatMap[PixelFormat::ALPHA_8];
    ALPHAFormat.format.sizedFormat = GL_ALPHA8;
    ALPHAFormat.format.externalFormat = GL_ALPHA;
    ALPHAFormat.format.externalType = GL_UNSIGNED_BYTE;
    ALPHAFormat.readSwizzle = Swizzle::AAAA();
    auto& GrayFormat = pixelFormatMap[PixelFormat::GRAY_8];
    GrayFormat.format.sizedFormat = GL_LUMINANCE8;
    GrayFormat.format.externalFormat = GL_LUMINANCE;
    GrayFormat.format.externalType = GL_UNSIGNED_BYTE;
    GrayFormat.readSwizzle = Swizzle::RGBA();
    auto& RGFormat = pixelFormatMap[PixelFormat::RG_88];
    RGFormat.format.sizedFormat = GL_LUMINANCE8_ALPHA8;
    RGFormat.format.externalFormat = GL_LUMINANCE_ALPHA;
    RGFormat.format.externalType = GL_UNSIGNED_BYTE;
    RGFormat.readSwizzle = Swizzle::RARA();
  }

  // ES 2.0 requires that the internal/external formats match.
  bool useSizedTexFormats =
      (standard == GLStandard::GL || (standard == GLStandard::GLES && version >= GL_VER(3, 0)) ||
       (standard == GLStandard::WebGL && version >= GL_VER(2, 0)));
  bool useSizedRbFormats = standard == GLStandard::GLES || standard == GLStandard::WebGL;

  for (auto& item : pixelFormatMap) {
    auto& format = item.second.format;
    format.internalFormatTexImage = useSizedTexFormats ? format.sizedFormat : format.externalFormat;
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
          ((version >= GL_VER(4, 2)) || glInterface.hasExtension("GL_ARB_internalformat_query"))) ||
         (standard == GLStandard::GLES && version >= GL_VER(3, 0));
}

void GLCaps::initColorSampleCount(const GLInfo& info) {
  std::vector<PixelFormat> pixelFormats = {PixelFormat::RGBA_8888};
  for (auto pixelFormat : pixelFormats) {
    if (UsesInternalformatQuery(standard, info, version)) {
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
      if (MSFBOType::None != msFBOType) {
        info.getIntegerv(GL_MAX_SAMPLES, &maxSampleCnt);
      }
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

void GLCaps::initMSAASupport(const GLInfo& info) {
  if (standard == GLStandard::GL) {
    if (version >= GL_VER(3, 0) || info.hasExtension("GL_ARB_framebuffer_object") ||
        (info.hasExtension("GL_EXT_framebuffer_multisample") &&
         info.hasExtension("GL_EXT_framebuffer_blit"))) {
      msFBOType = MSFBOType::Standard;
    }
  } else if (standard == GLStandard::GLES) {
    if (version >= GL_VER(3, 0) || info.hasExtension("GL_CHROMIUM_framebuffer_multisample") ||
        info.hasExtension("GL_ANGLE_framebuffer_multisample")) {
      msFBOType = MSFBOType::Standard;
    } else if (info.hasExtension("GL_APPLE_framebuffer_multisample")) {
      msFBOType = MSFBOType::ES_Apple;
    }
  } else if (standard == GLStandard::WebGL) {
    // No support in WebGL 1, but there is for 2.0
    if (version >= GL_VER(2, 0)) {
      msFBOType = MSFBOType::Standard;
    }
  }

  // We disable MSAA across the board for Intel GPUs for performance reasons.
  if (GLVendor::Intel == vendor) {
    msFBOType = MSFBOType::None;
  }
}
}  // namespace tgfx
