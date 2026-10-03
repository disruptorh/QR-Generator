#ifndef QR_RENDER_PNG_HPP_
#define QR_RENDER_PNG_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "render/raster.hpp"

namespace render {

// Serializes `image` as a PNG byte stream: 8-bit RGBA, no interlacing.
//
// Written by hand on top of zlib rather than through an image library: the app
// only ever emits a flat RGBA buffer, and depending on libpng or stb would pull
// in a decoder we deliberately do not have. The deflate stream comes from
// zlib's compress2(), and every chunk CRC is computed with zlib's crc32().
core::Result<std::vector<std::uint8_t>> encode_png(const Image& image);

}  // namespace render

#endif  // QR_RENDER_PNG_HPP_