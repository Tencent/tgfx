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

#include "PDFBitmap.h"
#include "core/codecs/jpeg/JpegCodec.h"
#include "core/images/CodecImage.h"
#include "core/utils/Types.h"
#include "pdf/DeflateStream.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/ColorType.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/core/YUVColorSpace.h"

namespace tgfx {

namespace {

enum class PDFStreamFormat { DCT, Flate, Uncompressed };

template <typename T>
void EmitImageStream(PDFDocumentImpl* doc, PDFIndirectReference ref, T writeStream, ISize size,
                     PDFUnion&& colorSpace, PDFIndirectReference sMask, int length,
                     PDFStreamFormat format) {
  auto pdfDict = PDFDictionary::Make("XObject");
  pdfDict->insertName("Subtype", "Image");
  pdfDict->insertInt("Width", size.width);
  pdfDict->insertInt("Height", size.height);
  pdfDict->insertUnion("ColorSpace", std::move(colorSpace));
  if (sMask) {
    pdfDict->insertRef("SMask", sMask);
  }
  pdfDict->insertInt("BitsPerComponent", 8);

  switch (format) {
    case PDFStreamFormat::DCT:
      pdfDict->insertName("Filter", "DCTDecode");
      break;
    case PDFStreamFormat::Flate:
      pdfDict->insertName("Filter", "FlateDecode");
      break;
    case PDFStreamFormat::Uncompressed:
      break;
  }

  if (format == PDFStreamFormat::DCT) {
    pdfDict->insertInt("ColorTransform", 0);
  }
  pdfDict->insertInt("Length", length);
  doc->emitStream(*pdfDict, std::move(writeStream), ref);
}

void FillStream(WriteStream* out, char value, size_t n) {
  char buffer[4096];
  memset(buffer, value, sizeof(buffer));
  for (size_t i = 0; i < n / sizeof(buffer); ++i) {
    out->write(buffer, sizeof(buffer));
  }
  out->write(buffer, n % sizeof(buffer));
}

uint32_t GetNeighborAvgColor(const Pixmap& pixmap, int xOrig, int yOrig) {
  unsigned r = 0;
  unsigned g = 0;
  unsigned b = 0;
  unsigned n = 0;
  // Clamp the range to the edge of the bitmap.
  int ymin = std::max(0, yOrig - 1);
  int ymax = std::min(yOrig + 1, pixmap.height() - 1);
  int xmin = std::max(0, xOrig - 1);
  int xmax = std::min(xOrig + 1, pixmap.width() - 1);

  const auto pixelPointer = reinterpret_cast<const uint8_t*>(pixmap.pixels());
  auto rowBytes = pixmap.rowBytes();
  for (int y = ymin; y <= ymax; ++y) {
    auto scanline =
        reinterpret_cast<const uint32_t*>(pixelPointer + (static_cast<size_t>(y) * rowBytes));
    for (int x = xmin; x <= xmax; ++x) {
      uint32_t color = *scanline++;
      if (color != 0x00000000) {
        r += (((color) >> 16) & 0xFF);
        g += (((color) >> 8) & 0xFF);
        b += (((color) >> 0) & 0xFF);
        n++;
      }
    }
  }
  if (n > 0) {
    auto avgR = static_cast<uint8_t>(r / n);
    auto avgG = static_cast<uint8_t>(g / n);
    auto avgB = static_cast<uint8_t>(b / n);
    return ((static_cast<uint32_t>(avgR) << 16) | (static_cast<uint32_t>(avgG) << 8) |
            static_cast<uint32_t>(avgB) << 0);
  }
  return 0x00000000;
}

void DoDeflatedAlpha(const Pixmap& pixmap, PDFDocumentImpl* document, PDFIndirectReference ref) {
  PDFMetadata::CompressionLevel compressionLevel = document->metadata().compressionLevel;
  PDFStreamFormat format = compressionLevel == PDFMetadata::CompressionLevel::None
                               ? PDFStreamFormat::Uncompressed
                               : PDFStreamFormat::Flate;
  auto buffer = MemoryWriteStream::Make();
  WriteStream* stream = buffer.get();
  std::shared_ptr<DeflateWriteStream> deflateWStream = nullptr;
  if (format == PDFStreamFormat::Flate) {
    deflateWStream =
        std::make_shared<DeflateWriteStream>(buffer.get(), static_cast<int>(compressionLevel));
    stream = deflateWStream.get();
  }

  if (pixmap.colorType() == ColorType::ALPHA_8) {
    const auto pixelPointer = reinterpret_cast<const uint8_t*>(pixmap.pixels());
    auto rowBytes = pixmap.rowBytes();

    uint8_t byteBuffer[4096];
    uint8_t* bufferStop = byteBuffer + std::size(byteBuffer);
    uint8_t* bufferPointer = byteBuffer;
    for (int y = 0; y < pixmap.height(); ++y) {
      auto scanline = pixelPointer + static_cast<size_t>(y) * rowBytes;
      for (int x = 0; x < pixmap.width(); ++x) {
        *bufferPointer++ = *scanline++;
        if (bufferPointer == bufferStop) {
          stream->write(byteBuffer, sizeof(byteBuffer));
          bufferPointer = byteBuffer;
        }
      }
    }
    stream->write(byteBuffer, static_cast<size_t>(bufferPointer - byteBuffer));
  } else {
    const auto pixelPointer = reinterpret_cast<const uint8_t*>(pixmap.pixels());
    auto rowBytes = pixmap.rowBytes();

    uint8_t byteBuffer[4096];
    uint8_t* bufferStop = byteBuffer + std::size(byteBuffer);
    uint8_t* bufferPointer = byteBuffer;
    for (int y = 0; y < pixmap.height(); ++y) {
      auto scanline =
          reinterpret_cast<const uint32_t*>(pixelPointer + (static_cast<size_t>(y) * rowBytes));
      for (int x = 0; x < pixmap.width(); ++x) {
        uint32_t color = *scanline++;
        *bufferPointer++ = (((color) >> 24) & 0xFF);
        if (bufferPointer == bufferStop) {
          stream->write(byteBuffer, sizeof(byteBuffer));
          bufferPointer = byteBuffer;
        }
      }
    }
    stream->write(byteBuffer, static_cast<size_t>(bufferPointer - byteBuffer));
  }
  if (deflateWStream) {
    deflateWStream->finalize();
  }

  int length = static_cast<int>(buffer->bytesWritten());
  auto imageSize = ISize::Make(pixmap.width(), pixmap.height());
  //TODO (YGaurora) optimize this data copy
  auto streamWriter = [&buffer](const std::shared_ptr<WriteStream>& stream) {
    auto data = buffer->readData();
    stream->write(data->data(), data->size());
  };
  EmitImageStream(document, ref, streamWriter, imageSize, PDFUnion::Name("DeviceGray"),
                  PDFIndirectReference(), length, format);
}

void DoDeflatedImage(const Pixmap& pixmap, PDFDocumentImpl* document, bool isOpaque,
                     PDFIndirectReference ref) {
  PDFIndirectReference sMask;
  if (!isOpaque) {
    sMask = document->reserveRef();
  }
  auto compressionLevel = document->metadata().compressionLevel;
  auto format = compressionLevel == PDFMetadata::CompressionLevel::None
                    ? PDFStreamFormat::Uncompressed
                    : PDFStreamFormat::Flate;
  auto buffer = MemoryWriteStream::Make();
  WriteStream* stream = buffer.get();
  std::shared_ptr<DeflateWriteStream> deflateWStream = nullptr;
  if (format == PDFStreamFormat::Flate) {
    deflateWStream =
        std::make_shared<DeflateWriteStream>(buffer.get(), static_cast<int>(compressionLevel));
    stream = deflateWStream.get();
  }

  auto colorSpace = PDFUnion::Name("DeviceGray");
  // int channels;
  switch (pixmap.colorType()) {
    case ColorType::ALPHA_8:
      // channels = 1;
      FillStream(stream, '\x00',
                 static_cast<size_t>(pixmap.width()) * static_cast<size_t>(pixmap.height()));
      break;
    case ColorType::Gray_8: {
      // channels = 1;
      const auto pixelPointer = reinterpret_cast<const uint8_t*>(pixmap.pixels());
      auto rowBytes = pixmap.rowBytes();

      uint8_t byteBuffer[4096];
      uint8_t* bufferStop = byteBuffer + std::size(byteBuffer);
      uint8_t* bufferPointer = byteBuffer;
      for (int y = 0; y < pixmap.height(); ++y) {
        auto scanline = pixelPointer + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < pixmap.width(); ++x) {
          *bufferPointer++ = *scanline++;
          if (bufferPointer == bufferStop) {
            stream->write(byteBuffer, sizeof(byteBuffer));
            bufferPointer = byteBuffer;
          }
        }
      }
      stream->write(byteBuffer, static_cast<size_t>(bufferPointer - byteBuffer));
      break;
    }
    default:
      auto colorSpaceRef = document->colorSpaceRef();
      colorSpace = PDFUnion::Ref(colorSpaceRef);
      const auto pixelPointer = reinterpret_cast<const uint8_t*>(pixmap.pixels());
      auto rowBytes = pixmap.rowBytes();

      uint8_t byteBuffer[3072];
      static_assert(std::size(byteBuffer) % 3 == 0);
      uint8_t* bufferStop = byteBuffer + std::size(byteBuffer);
      uint8_t* bufferPointer = byteBuffer;
      for (int y = 0; y < pixmap.height(); ++y) {
        auto scanline =
            reinterpret_cast<const uint32_t*>(pixelPointer + (static_cast<size_t>(y) * rowBytes));
        for (int x = 0; x < pixmap.width(); ++x) {
          uint32_t color = *scanline++;
          if ((((color) >> 24) & 0xFF) == 0x00) {
            color = GetNeighborAvgColor(pixmap, x, y);
          }
          *bufferPointer++ = (((color) >> 16) & 0xFF);
          *bufferPointer++ = (((color) >> 8) & 0xFF);
          *bufferPointer++ = (((color) >> 0) & 0xFF);
          if (bufferPointer == bufferStop) {
            stream->write(byteBuffer, sizeof(byteBuffer));
            bufferPointer = byteBuffer;
          }
        }
      }
      stream->write(byteBuffer, static_cast<size_t>(bufferPointer - byteBuffer));
  }
  if (deflateWStream) {
    deflateWStream->finalize();
  }

  int length = static_cast<int>(buffer->bytesWritten());
  auto imageSize = ISize::Make(pixmap.width(), pixmap.height());
  //TODO (YGaurora) optimize this data copy
  auto streamWriter = [&buffer](const std::shared_ptr<WriteStream>& stream) {
    auto data = buffer->readData();
    stream->write(data->data(), data->size());
  };
  EmitImageStream(document, ref, streamWriter, imageSize, std::move(colorSpace), sMask, length,
                  format);

  if (!isOpaque) {
    DoDeflatedAlpha(pixmap, document, sMask);
  }
}

}  // namespace

void PDFBitmap::SerializeImage(const std::shared_ptr<Image>& image, int /*encodingQuality*/,
                               PDFDocumentImpl* doc, PDFIndirectReference ref) {
  //TODO (YGaurora): is image opaque,encode as jpeg
  auto image2bitmap = [doc](Context* context, const std::shared_ptr<Image>& image) {
    auto surface = Surface::Make(context, image->width(), image->height(), false, 1, false, 0,
                                 doc->colorSpace());
    auto canvas = surface->getCanvas();
    canvas->drawImage(image);

    Bitmap bitmap(surface->width(), surface->height(), false, true, surface->colorSpace());
    auto pixels = bitmap.lockPixels();
    //bitmap in pdf must be unpremultiplied
    if (surface->readPixels(bitmap.info().makeAlphaType(AlphaType::Unpremultiplied), pixels)) {
      bitmap.unlockPixels();
      return bitmap;
    }
    return Bitmap();
  };

  auto bitmap = image2bitmap(doc->context(), image);
  if (bitmap.isEmpty()) {
    return;
  }
  auto pixmap = Pixmap(bitmap);
  DoDeflatedImage(pixmap, doc, bitmap.isOpaque(), ref);
}

PDFIndirectReference PDFBitmap::Serialize(const std::shared_ptr<Image>& image,
                                          PDFDocumentImpl* document, int encodingQuality) {
  DEBUG_ASSERT(image);
  DEBUG_ASSERT(document);
  auto ref = document->reserveRef();
  SerializeImage(image, encodingQuality, document, ref);
  return ref;
}
}  // namespace tgfx