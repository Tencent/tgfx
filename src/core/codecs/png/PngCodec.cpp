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

#include "core/codecs/png/PngCodec.h"
#include "core/utils/ColorSpaceHelper.h"
#include "png.h"
#include "skcms.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Stream.h"

namespace tgfx {
std::shared_ptr<ImageCodec> PngCodec::MakeFrom(const std::string& filePath) {
  return MakeFromData(filePath, nullptr);
}

std::shared_ptr<ImageCodec> PngCodec::MakeFrom(std::shared_ptr<Data> imageBytes) {
  return MakeFromData("", std::move(imageBytes));
}

bool PngCodec::IsPng(const std::shared_ptr<Data>& data) {
  constexpr png_byte png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  return data->size() >= 8 && !memcmp(data->bytes(), &png_signature[0], 8);
}

typedef struct {
  const unsigned char* base;
  const unsigned char* end;
  const unsigned char* cursor;
} PngReader;

static void PngReaderReadData(png_structp png_ptr, png_bytep data, png_size_t length) {
  auto reader = static_cast<PngReader*>(png_get_io_ptr(png_ptr));
  auto avail = static_cast<png_size_t>(reader->end - reader->cursor);
  if (avail > length) {
    avail = length;
  }
  memcpy(data, reader->cursor, avail);
  reader->cursor += avail;
}

class ReadInfo {
 public:
  static std::shared_ptr<ReadInfo> Make(const std::string& filePath,
                                        const std::shared_ptr<Data>& fileData) {
    auto readInfo = std::make_shared<ReadInfo>();
    if (!filePath.empty()) {
      readInfo->infile = fopen(filePath.c_str(), "rb");
    }
    if (fileData == nullptr && readInfo->infile == nullptr) {
      return nullptr;
    }
    readInfo->p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (readInfo->p == nullptr) {
      return nullptr;
    }
    readInfo->pi = png_create_info_struct(readInfo->p);
    if (readInfo->pi == nullptr) {
      return nullptr;
    }
#ifdef PNG_SET_OPTION_SUPPORTED
    png_set_option(readInfo->p, PNG_MAXIMUM_INFLATE_WINDOW, PNG_OPTION_ON);
#endif
    if (setjmp(png_jmpbuf(readInfo->p))) {
      return nullptr;
    }
    if (readInfo->infile) {
      png_init_io(readInfo->p, readInfo->infile);
    } else {
      readInfo->reader = new PngReader();
      readInfo->reader->base = fileData->bytes();
      readInfo->reader->end = fileData->bytes() + fileData->size();
      readInfo->reader->cursor = fileData->bytes();
      png_set_read_fn(readInfo->p, readInfo->reader, PngReaderReadData);
    }
    png_read_info(readInfo->p, readInfo->pi);
    return readInfo;
  }

  ~ReadInfo() {
    if (p) {
      png_destroy_read_struct(&p, &pi, nullptr);
    }
    if (data) {
      free(data);
    }
    if (rowPtrs) {
      free(rowPtrs);
    }
    if (infile) {
      fclose(infile);
    }
    delete reader;
  }

  png_structp p = nullptr;
  png_infop pi = nullptr;
  FILE* infile = nullptr;
  PngReader* reader = nullptr;
  unsigned char** rowPtrs = nullptr;
  unsigned char* data = nullptr;
};

#if (PNG_LIBPNG_VER_MAJOR > 1) || (PNG_LIBPNG_VER_MAJOR == 1 && PNG_LIBPNG_VER_MINOR >= 6)

static float PngFixedPointToFloat(png_fixed_point x) {
  return ((float)x) * 0.00001f;
}

static float PngInvertedFixedPointToFloat(png_fixed_point x) {
  /**
   * This is necessary because the gAMA chunk actually stores 1/gamma.
   */
  return 1.0f / PngFixedPointToFloat(x);
}

#endif  // LIBPNG >= 1.6

/**
 * If there is no color profile information, it will use sRGB.
 */
static std::shared_ptr<ColorSpace> ReadColorProfile(png_structp pngPtr, png_infop infoPtr) {
#if (PNG_LIBPNG_VER_MAJOR > 1) || (PNG_LIBPNG_VER_MAJOR == 1 && PNG_LIBPNG_VER_MINOR >= 6)
  /**
   * First check for an ICC profile
   */
  png_bytep profile;
  png_uint_32 length;
  /**
   * The below variables are unused, however, we need to pass them in anyway or png_get_iCCP() will
   * return nothing.
   */
  png_charp name;
  /**
   * The |compression| is uninteresting since:
   *  (1) libpng has already decompressed the profile for us.
   *  (2) "deflate" is the only mode of decompression that libpng supports.
   */
  int compression;
  if (PNG_INFO_iCCP == png_get_iCCP(pngPtr, infoPtr, &name, &compression, &profile, &length)) {
    return ColorSpace::MakeFromICC(profile, length);
  }

  /**
   * Second, check for sRGB. Note that Blink does this first. This code checks ICC first, with the
   * thinking that an image has both truly wants the potentially more specific ICC chunk, with sRGB
   * as a backup in case the decoder does not support full color management.
   */
  if (png_get_valid(pngPtr, infoPtr, PNG_INFO_sRGB)) {
    return ColorSpace::MakeSRGB();
  }

  /**
   * Default to SRGB gamut.
   */
  gfx::skcms_Matrix3x3 toXYZD50 = gfx::skcms_sRGB_profile()->toXYZD50;
  png_fixed_point chrm[8];
  png_fixed_point gamma;
  if (png_get_cHRM_fixed(pngPtr, infoPtr, &chrm[0], &chrm[1], &chrm[2], &chrm[3], &chrm[4],
                         &chrm[5], &chrm[6], &chrm[7])) {
    float rx = PngFixedPointToFloat(chrm[2]);
    float ry = PngFixedPointToFloat(chrm[3]);
    float gx = PngFixedPointToFloat(chrm[4]);
    float gy = PngFixedPointToFloat(chrm[5]);
    float bx = PngFixedPointToFloat(chrm[6]);
    float by = PngFixedPointToFloat(chrm[7]);
    float wx = PngFixedPointToFloat(chrm[0]);
    float wy = PngFixedPointToFloat(chrm[1]);

    gfx::skcms_Matrix3x3 tmp;
    if (skcms_PrimariesToXYZD50(rx, ry, gx, gy, bx, by, wx, wy, &tmp)) {
      toXYZD50 = tmp;
    }
  }

  gfx::skcms_TransferFunction fn;
  if (PNG_INFO_gAMA == png_get_gAMA_fixed(pngPtr, infoPtr, &gamma)) {
    fn.a = 1.0f;
    fn.b = fn.c = fn.d = fn.e = fn.f = 0.0f;
    fn.g = PngInvertedFixedPointToFloat(gamma);
  } else {
    /**
     * Default to sRGB gamma if the image has color space information, but does not specify gamma.
     * Note that Blink would again return nullptr in this case.
     */
    fn = *gfx::skcms_sRGB_TransferFunction();
  }
  return ColorSpace::MakeRGB(*reinterpret_cast<TransferFunction*>(&fn),
                             *reinterpret_cast<ColorMatrix33*>(&toXYZD50));
#else   // LIBPNG < 1.6
  return ColorSpace::MakeSRGB();
#endif  // LIBPNG >= 1.6
}

std::shared_ptr<ImageCodec> PngCodec::MakeFromData(const std::string& filePath,
                                                   std::shared_ptr<Data> byteData) {
  auto readInfo = ReadInfo::Make(filePath, byteData);
  if (readInfo == nullptr) {
    return nullptr;
  }
  png_uint_32 w = 0, h = 0;
  int colorType = -1;
  png_get_IHDR(readInfo->p, readInfo->pi, &w, &h, nullptr, &colorType, nullptr, nullptr, nullptr);
  auto cs = ReadColorProfile(readInfo->p, readInfo->pi);
  if (!cs) {
    cs = ColorSpace::MakeSRGB();
  }
  if (w == 0 || h == 0) {
    return nullptr;
  }
  bool isAlphaOnly = false;
  if (colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_color_8p sigBits;
    if (png_get_sBIT(readInfo->p, readInfo->pi, &sigBits)) {
      if (8 == sigBits->alpha && 1 == sigBits->gray) {
        isAlphaOnly = true;
      }
    }
  }
  return std::shared_ptr<ImageCodec>(new PngCodec(static_cast<int>(w), static_cast<int>(h),
                                                  Orientation::TopLeft, isAlphaOnly, filePath,
                                                  std::move(byteData), std::move(cs)));
}

static void UpdateReadInfo(png_structp p, png_infop pi) {
  int originalColorType = png_get_color_type(p, pi);
  int bitDepth = png_get_bit_depth(p, pi);
  if (bitDepth == 16) {
    png_set_strip_16(p);
  }
  if (originalColorType == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(p);
  }
  if (originalColorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
    png_set_expand_gray_1_2_4_to_8(p);
  }
  if (png_get_valid(p, pi, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(p);
  }
  if (originalColorType == PNG_COLOR_TYPE_RGB || originalColorType == PNG_COLOR_TYPE_GRAY ||
      originalColorType == PNG_COLOR_TYPE_PALETTE) {
    png_set_filler(p, 0xFF, PNG_FILLER_AFTER);
  }
  if (originalColorType == PNG_COLOR_TYPE_GRAY || originalColorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(p);
  }
  png_read_update_info(p, pi);
}

bool PngCodec::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                            std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const {
  auto readInfo = ReadInfo::Make(filePath, fileData);
  if (readInfo == nullptr) {
    return false;
  }
  int w = width();
  int h = height();
  if (h == 0 || w == 0) {
    return false;
  }
  UpdateReadInfo(readInfo->p, readInfo->pi);
  readInfo->rowPtrs = (unsigned char**)malloc(sizeof(unsigned char*) * (size_t)h);
  if (readInfo->rowPtrs == nullptr) {
    return false;
  }
  if (setjmp(png_jmpbuf(readInfo->p))) {
    return false;
  }
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
  if (colorType == ColorType::RGBA_8888 && alphaType == AlphaType::Unpremultiplied) {
    for (size_t i = 0; i < static_cast<size_t>(h); i++) {
      readInfo->rowPtrs[i] = static_cast<unsigned char*>(dstPixels) + (dstRowBytes * i);
    }
    png_read_image(readInfo->p, readInfo->rowPtrs);
    if (!ColorSpaceIsEqual(colorSpace(), dstColorSpace)) {
      ConvertColorSpaceInPlace(w, h, colorType, alphaType, dstRowBytes, colorSpace(), dstColorSpace,
                               dstPixels);
    }
    return true;
  }
  ImageInfo info =
      ImageInfo::Make(w, h, ColorType::RGBA_8888, AlphaType::Unpremultiplied, 0, colorSpace());
  readInfo->data = (unsigned char*)malloc(info.byteSize());
  if (readInfo->data == nullptr) {
    return false;
  }
  for (size_t i = 0; i < static_cast<size_t>(h); i++) {
    readInfo->rowPtrs[i] = readInfo->data + (info.rowBytes() * i);
  }
  png_read_image(readInfo->p, readInfo->rowPtrs);
  Pixmap pixmap(info, readInfo->data);
  return pixmap.readPixels(dstInfo, dstPixels);
}

bool PngCodec::isAlphaOnly() const {
  return _isAlphaOnly;
}

std::shared_ptr<Data> PngCodec::getEncodedData() const {
  if (fileData) {
    return fileData;
  }
  if (!filePath.empty()) {
    return Data::MakeFromFile(filePath);
  }
  return nullptr;
}

#ifdef TGFX_USE_PNG_ENCODE
struct PngWriter {
  unsigned char* data = nullptr;
  size_t length = 0;
};

static void png_reader_write_data(png_structp png_ptr, png_bytep data, png_size_t length) {
  auto writer = static_cast<PngWriter*>(png_get_io_ptr(png_ptr));
  size_t nsize = writer->length + length;
  if (writer->data) {
    writer->data = static_cast<unsigned char*>(realloc(writer->data, nsize));
  } else {
    writer->data = static_cast<unsigned char*>(malloc(nsize));
  }
  memcpy(writer->data + writer->length, data, length);
  writer->length += length;
}

std::shared_ptr<Data> PngCodec::Encode(const Pixmap& pixmap, int) {
  auto srcPixels = static_cast<png_bytep>(const_cast<void*>((pixmap.pixels())));
  auto srcRowBytes = static_cast<int>(pixmap.rowBytes());
  uint8_t* alphaPixels = nullptr;
  Buffer tempBuffer;
  if (pixmap.colorType() == ColorType::ALPHA_8) {
    tempBuffer.alloc(static_cast<size_t>(pixmap.width()) * 2);
    alphaPixels = tempBuffer.bytes();
    srcRowBytes = static_cast<int>(tempBuffer.size());
  } else if (pixmap.colorType() != ColorType::RGBA_8888 ||
             pixmap.alphaType() == AlphaType::Premultiplied) {
    auto dstInfo = ImageInfo::Make(pixmap.width(), pixmap.height(), ColorType::RGBA_8888,
                                   AlphaType::Unpremultiplied, 0, pixmap.colorSpace());
    tempBuffer.alloc(dstInfo.byteSize());
    if (!pixmap.readPixels(dstInfo, tempBuffer.data())) {
      return nullptr;
    }
    srcPixels = tempBuffer.bytes();
    srcRowBytes = static_cast<int>(dstInfo.rowBytes());
  }
  PngWriter pngWriter;
  png_structp png_ptr = nullptr;
  png_infop info_ptr = nullptr;
  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  bool encodeSuccess = false;
  do {
    if (png_ptr == nullptr) {
      break;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr) {
      break;
    }
    if (setjmp(png_jmpbuf(png_ptr))) {
      break;
    }
    png_color_8 sigBit = {8, 8, 8, 0, 8};
    int colorType = PNG_COLOR_TYPE_RGB_ALPHA;
    if (pixmap.colorType() == ColorType::ALPHA_8) {
      // We store ALPHA_8 images as GrayAlpha in png. If the gray channel is set to 1, we assume the
      // gray channel can be ignored, and we output just alpha. We tried 0 at first, but png doesn't
      // like a 0 sigBit for a channel it expects, hence we chose 1.
      sigBit = {0, 0, 0, 1, 8};
      colorType = PNG_COLOR_TYPE_GRAY_ALPHA;
    }
    png_set_IHDR(png_ptr, info_ptr, static_cast<png_uint_32>(pixmap.width()),
                 static_cast<png_uint_32>(pixmap.height()), 8, colorType, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    if (colorType != PNG_COLOR_TYPE_GRAY_ALPHA && pixmap.colorSpace()) {
      auto iccData = pixmap.colorSpace()->toICCProfile();
      png_set_iCCP(png_ptr, info_ptr, "TGFX", 0, iccData->bytes(),
                   static_cast<uint32_t>(iccData->size()));
    }
    png_set_sBIT(png_ptr, info_ptr, &sigBit);
    png_set_write_fn(png_ptr, &pngWriter, png_reader_write_data, nullptr);
    png_write_info(png_ptr, info_ptr);
    for (int i = 0; i < pixmap.height(); ++i) {
      if (pixmap.colorType() == ColorType::ALPHA_8) {
        // convert alpha8 to gray
        for (int j = 0; j < pixmap.width(); j++) {
          *(alphaPixels++) = 0;
          *(alphaPixels++) = *(srcPixels++);
        }
        alphaPixels -= srcRowBytes;
        srcPixels += (pixmap.rowBytes() - static_cast<size_t>(pixmap.width()));
        png_write_row(png_ptr, reinterpret_cast<png_const_bytep>(alphaPixels));
      } else {
        png_write_row(png_ptr, reinterpret_cast<png_const_bytep>(srcPixels));
        srcPixels += srcRowBytes;
      }
    }
    png_write_end(png_ptr, info_ptr);
    encodeSuccess = true;
  } while (false);
  if (png_ptr) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
  }
  if (!encodeSuccess) {
    if (pngWriter.data) {
      free(pngWriter.data);
    }
    return nullptr;
  }
  return Data::MakeAdopted(pngWriter.data, pngWriter.length, Data::FreeProc);
}
#endif

}  // namespace tgfx
