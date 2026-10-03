#ifndef QR_RENDER_RASTER_HPP_
#define QR_RENDER_RASTER_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "qr/qr_encoder.hpp"

namespace render {

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

const char* rgb_to_hex(Rgb color);

struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // width * height * 4, non-premultiplied

  bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
};

struct RenderOptions {
  int module_px = 8;    // pixel size of one module; must be >= 1
  int quiet_zone = 4;   // quiet zone width in modules; must be >= 0
  Rgb foreground{0, 0, 0};
  Rgb background{255, 255, 255};
};

// Scales `matrix` into straight RGBA pixels. Every module is a solid
// `module_px` square and the whole image is (size + 2 * quiet_zone) modules per
// side, so no module can ever land on a pixel boundary by accident.
//
// Returns an error for a malformed matrix or an out-of-range option rather than
// throwing, so a bad user setting cannot unwind the render loop.
core::Result<Image> rasterize(const qr::Matrix& matrix, const RenderOptions& options);

// Total pixel size the options ask for, without allocating. Used by the UI to
// show the result size before generating.
bool raster_size(const qr::Matrix& matrix, const RenderOptions& options, int* width, int* height);

}  // namespace render

#endif  // QR_RENDER_RASTER_HPP_