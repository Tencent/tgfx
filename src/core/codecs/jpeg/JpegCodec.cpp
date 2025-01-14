/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/codecs/jpeg/JpegCodec.h"
#include <csetjmp>
#include "core/utils/OrientationHelper.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Pixmap.h"

extern "C" {
#include "jerror.h"
#include "jpeglib.h"
}

namespace tgfx {

bool JpegCodec::IsJpeg(const std::shared_ptr<Data>& data) {
  constexpr uint8_t jpegSig[] = {0xFF, 0xD8, 0xFF};
  return data->size() >= 3 && !memcmp(data->bytes(), jpegSig, sizeof(jpegSig));
}

const uint32_t kExifHeaderSize = 14;
const uint32_t kExifMarker = JPEG_APP0 + 1;

static bool is_orientation_marker(jpeg_marker_struct* marker, Orientation* orientation) {
  if (kExifMarker != marker->marker || marker->data_length < kExifHeaderSize) {
    return false;
  }

  constexpr uint8_t kExifSig[]{'E', 'x', 'i', 'f', '\0'};
  if (memcmp(marker->data, kExifSig, sizeof(kExifSig)) != 0) {
    return false;
  }

  // Account for 'E', 'x', 'i', 'f', '\0', '<fill byte>'.
  constexpr size_t kOffset = 6;
  return is_orientation_marker(marker->data + kOffset, marker->data_length - kOffset, orientation);
}

static Orientation get_exif_orientation(jpeg_decompress_struct* dinfo) {
  Orientation orientation;
  for (jpeg_marker_struct* marker = dinfo->marker_list; marker; marker = marker->next) {
    if (is_orientation_marker(marker, &orientation)) {
      return orientation;
    }
  }
  return Orientation::TopLeft;
}

struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

std::shared_ptr<ImageCodec> JpegCodec::MakeFrom(const std::string& filePath) {
  return MakeFromData(filePath, nullptr);
}

std::shared_ptr<ImageCodec> JpegCodec::MakeFrom(std::shared_ptr<Data> imageBytes) {
  return MakeFromData("", std::move(imageBytes));
}

std::shared_ptr<ImageCodec> JpegCodec::MakeFromData(const std::string& filePath,
                                                    std::shared_ptr<Data> byteData) {
  FILE* infile = nullptr;
  if (byteData == nullptr && (infile = fopen(filePath.c_str(), "rb")) == nullptr) {
    return nullptr;
  }
  jpeg_decompress_struct cinfo = {};
  my_error_mgr jerr = {};
  cinfo.err = jpeg_std_error(&jerr.pub);
  Orientation orientation = Orientation::TopLeft;
  do {
    if (setjmp(jerr.setjmp_buffer)) break;
    jpeg_create_decompress(&cinfo);
    if (infile) {
      jpeg_stdio_src(&cinfo, infile);
    } else {
      jpeg_mem_src(&cinfo, byteData->bytes(), byteData->size());
    }
    jpeg_save_markers(&cinfo, kExifMarker, 0xFFFF);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) break;
    orientation = get_exif_orientation(&cinfo);
  } while (false);
  jpeg_destroy_decompress(&cinfo);
  if (infile) fclose(infile);
  if (cinfo.image_width == 0 || cinfo.image_height == 0) {
    return nullptr;
  }
  return std::shared_ptr<ImageCodec>(new JpegCodec(static_cast<int>(cinfo.image_width),
                                                   static_cast<int>(cinfo.image_height),
                                                   orientation, filePath, std::move(byteData)));
}

bool CMYKToColorType(uint32_t* dst, const uint32_t* src, int count, ColorType type) {
  if (type != ColorType::BGRA_8888 && type != ColorType::RGBA_8888) {
    return false;
  }
  for (int i = 0; i < count; i++) {
    uint8_t k = (src[i] >> 24) & 0xFF;
    uint8_t y = (src[i] >> 16) & 0xFF;
    uint8_t m = (src[i] >> 8) & 0xFF;
    uint8_t c = (src[i] >> 0) & 0xFF;
    uint8_t r = (c * k + 127) / 255;
    uint8_t g = (m * k + 127) / 255;
    uint8_t b = (y * k + 127) / 255;

    if (type == ColorType::BGRA_8888) {
      dst[i] = (uint32_t)0xFF << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b << 0;
    } else {
      dst[i] = (uint32_t)0xFF << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r << 0;
    }
  }
  return true;
}

bool JpegCodec::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstPixels == nullptr || dstInfo.isEmpty()) {
    return false;
  }
  if (dstInfo.colorType() == ColorType::ALPHA_8) {
    memset(dstPixels, 255, dstInfo.rowBytes() * static_cast<size_t>(height()));
    return true;
  }
  Bitmap bitmap = {};
  auto outPixels = dstPixels;
  auto outRowBytes = dstInfo.rowBytes();
  J_COLOR_SPACE out_color_space;
  switch (dstInfo.colorType()) {
    case ColorType::RGBA_8888:
      out_color_space = JCS_EXT_RGBA;
      break;
    case ColorType::BGRA_8888:
      out_color_space = JCS_EXT_BGRA;
      break;
    case ColorType::Gray_8:
      out_color_space = JCS_GRAYSCALE;
      break;
    case ColorType::RGB_565:
      out_color_space = JCS_RGB565;
      break;
    default:
      auto success = bitmap.allocPixels(dstInfo.width(), dstInfo.height(), false, false);
      if (!success) {
        return false;
      }
      out_color_space = JCS_EXT_RGBA;
      break;
  }
  Pixmap pixmap(bitmap);
  if (!pixmap.isEmpty()) {
    outPixels = pixmap.writablePixels();
    outRowBytes = pixmap.rowBytes();
  }
  FILE* infile = nullptr;
  if (fileData == nullptr && (infile = fopen(filePath.c_str(), "rb")) == nullptr) {
    return false;
  }
  jpeg_decompress_struct cinfo = {};
  my_error_mgr jerr = {};
  cinfo.err = jpeg_std_error(&jerr.pub);
  bool result = false;
  do {
    if (setjmp(jerr.setjmp_buffer)) break;
    jpeg_create_decompress(&cinfo);
    if (infile) {
      jpeg_stdio_src(&cinfo, infile);
    } else {
      jpeg_mem_src(&cinfo, fileData->bytes(), fileData->size());
    }
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
      break;
    }
    cinfo.out_color_space = out_color_space;
    if (cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK) {
      cinfo.out_color_space = JCS_CMYK;
    }
    if (!jpeg_start_decompress(&cinfo)) {
      break;
    }
    JSAMPROW pRow[1];
    int line = 0;
    JDIMENSION h = static_cast<JDIMENSION>(height());
    while (cinfo.output_scanline < h) {
      pRow[0] = (JSAMPROW)(static_cast<unsigned char*>(outPixels) +
                           outRowBytes * static_cast<size_t>(line));
      jpeg_read_scanlines(&cinfo, pRow, 1);
      line++;
    }
    result = jpeg_finish_decompress(&cinfo);
  } while (false);
  if (cinfo.out_color_space == JCS_CMYK) {
    int count = dstInfo.width() * dstInfo.height();
    if (!CMYKToColorType(static_cast<uint32_t*>(dstPixels), static_cast<uint32_t*>(outPixels),
                         count, dstInfo.colorType())) {
      result = false;
    }
  }
  jpeg_destroy_decompress(&cinfo);
  if (infile) {
    fclose(infile);
  }
  if (result && !pixmap.isEmpty()) {
    pixmap.readPixels(dstInfo, dstPixels);
  }
  return result;
}

std::shared_ptr<Data> JpegCodec::getEncodedData() const {
  if (fileData) {
    return fileData;
  }
  if (!filePath.empty()) {
    return Data::MakeFromFile(filePath);
  }
  return nullptr;
}

#ifdef TGFX_USE_JPEG_ENCODE
std::shared_ptr<Data> JpegCodec::Encode(const Pixmap& pixmap, int quality) {
  auto srcPixels = static_cast<uint8_t*>(const_cast<void*>(pixmap.pixels()));
  int srcRowBytes = static_cast<int>(pixmap.rowBytes());
  jpeg_compress_struct cinfo = {};
  jpeg_error_mgr jerr = {};
  JSAMPROW row_pointer[1];
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  uint8_t* dstBuffer = nullptr;
  unsigned long dstBufferSize = 0;  // NOLINT
  jpeg_mem_dest(&cinfo, &dstBuffer, &dstBufferSize);
  cinfo.image_width = static_cast<JDIMENSION>(pixmap.width());
  cinfo.image_height = static_cast<JDIMENSION>(pixmap.height());
  Buffer buffer = {};
  switch (pixmap.colorType()) {
    case ColorType::RGBA_8888:
      cinfo.in_color_space = JCS_EXT_RGBA;
      cinfo.input_components = 4;
      break;
    case ColorType::BGRA_8888:
      cinfo.in_color_space = JCS_EXT_BGRA;
      cinfo.input_components = 4;
      break;
    case ColorType::Gray_8:
      cinfo.in_color_space = JCS_GRAYSCALE;
      cinfo.input_components = 1;
      break;
    default:
      auto info = ImageInfo::Make(pixmap.width(), pixmap.height(), ColorType::RGBA_8888);
      buffer.alloc(info.byteSize());
      if (buffer.isEmpty()) {
        return nullptr;
      }
      srcPixels = buffer.bytes();
      srcRowBytes = static_cast<int>(info.rowBytes());
      Pixmap(info, srcPixels).writePixels(pixmap.info(), pixmap.pixels());
      cinfo.in_color_space = JCS_EXT_RGBA;
      cinfo.input_components = 4;
      break;
  }
  jpeg_set_defaults(&cinfo);
  cinfo.optimize_coding = TRUE;
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = &(srcPixels)[cinfo.next_scanline * static_cast<JDIMENSION>(srcRowBytes)];
    (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  /* similar to read file, clean up after we're done compressing */
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  return Data::MakeAdopted(dstBuffer, dstBufferSize, Data::FreeProc);
}
#endif

}  // namespace tgfx