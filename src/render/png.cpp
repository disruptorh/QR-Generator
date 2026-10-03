#include "render/png.hpp"

#include <zlib.h>

namespace render {
namespace {

const std::uint8_t kSignature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

void push_be32(std::vector<std::uint8_t>* out, std::uint32_t v) {
  out->push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
  out->push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
  out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
  out->push_back(static_cast<std::uint8_t>(v & 0xff));
}

// A chunk is length, type, payload, then a CRC over type + payload.
void push_chunk(std::vector<std::uint8_t>* out, const char type[4],
                const std::uint8_t* data, std::size_t size) {
  push_be32(out, static_cast<std::uint32_t>(size));
  const std::size_t crc_start = out->size();
  out->insert(out->end(), type, type + 4);
  if (size > 0) out->insert(out->end(), data, data + size);
  const uLong crc = crc32(crc32(0L, Z_NULL, 0), out->data() + crc_start,
                          static_cast<uInt>(out->size() - crc_start));
  push_be32(out, static_cast<std::uint32_t>(crc));
}

}  // namespace

core::Result<std::vector<std::uint8_t>> encode_png(const Image& image) {
  if (image.width <= 0 || image.height <= 0) {
    return core::Result<std::vector<std::uint8_t>>::err("image", "La imagen no tiene tamano");
  }
  const std::size_t stride = static_cast<std::size_t>(image.width) * 4;
  if (image.rgba.size() != stride * static_cast<std::size_t>(image.height)) {
    return core::Result<std::vector<std::uint8_t>>::err(
        "image", "El tamano del buffer no coincide con la imagen");
  }

  // Scanlines are prefixed with a filter byte. Filter 0 (none) keeps the writer
  // trivial and still compresses well, because QR output is large flat areas.
  std::vector<std::uint8_t> raw;
  raw.reserve((stride + 1) * static_cast<std::size_t>(image.height));
  for (int y = 0; y < image.height; ++y) {
    raw.push_back(0);
    const std::uint8_t* row = image.rgba.data() + static_cast<std::size_t>(y) * stride;
    raw.insert(raw.end(), row, row + stride);
  }

  uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> deflated(bound);
  const int rc = compress2(deflated.data(), &bound, raw.data(), static_cast<uLong>(raw.size()),
                           Z_BEST_COMPRESSION);
  if (rc != Z_OK) {
    return core::Result<std::vector<std::uint8_t>>::err("png", "zlib no pudo comprimir la imagen");
  }
  deflated.resize(bound);

  std::vector<std::uint8_t> png;
  png.reserve(deflated.size() + 128);
  png.insert(png.end(), kSignature, kSignature + 8);

  std::uint8_t ihdr[13];
  const std::uint32_t width = static_cast<std::uint32_t>(image.width);
  const std::uint32_t height = static_cast<std::uint32_t>(image.height);
  ihdr[0] = static_cast<std::uint8_t>((width >> 24) & 0xff);
  ihdr[1] = static_cast<std::uint8_t>((width >> 16) & 0xff);
  ihdr[2] = static_cast<std::uint8_t>((width >> 8) & 0xff);
  ihdr[3] = static_cast<std::uint8_t>(width & 0xff);
  ihdr[4] = static_cast<std::uint8_t>((height >> 24) & 0xff);
  ihdr[5] = static_cast<std::uint8_t>((height >> 16) & 0xff);
  ihdr[6] = static_cast<std::uint8_t>((height >> 8) & 0xff);
  ihdr[7] = static_cast<std::uint8_t>(height & 0xff);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 6;   // colour type: truecolour with alpha
  ihdr[10] = 0;  // deflate
  ihdr[11] = 0;  // adaptive filtering
  ihdr[12] = 0;  // no interlace
  push_chunk(&png, "IHDR", ihdr, sizeof(ihdr));
  push_chunk(&png, "IDAT", deflated.data(), deflated.size());
  push_chunk(&png, "IEND", nullptr, 0);

  return core::Result<std::vector<std::uint8_t>>::ok(std::move(png));
}

}  // namespace render