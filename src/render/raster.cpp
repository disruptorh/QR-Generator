#include "render/raster.hpp"

#include <cstdio>
#include <new>

namespace render {
namespace {

// Bounds on a generated image. A version 40 symbol plus its quiet zone is 185
// modules, so a 4096 px side still leaves 22 px per module; anything past that
// is a mistyped value rather than a request. Refusing early keeps a hostile
// module_px from turning into an out-of-memory abort.
constexpr long long kMaxSide = 4096;
constexpr long long kMaxPixels = 4096LL * 4096LL;

}  // namespace

const char* rgb_to_hex(Rgb color) {
  static thread_local char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r, color.g, color.b);
  return buffer;
}

bool raster_size(const qr::Matrix& matrix, const RenderOptions& options, int* width, int* height) {
  if (matrix.size <= 0) return false;
  const long long side = static_cast<long long>(matrix.size) + 2LL * options.quiet_zone;
  const long long px = side * options.module_px;
  if (px <= 0 || px > kMaxSide || px * px > kMaxPixels) return false;
  *width = static_cast<int>(px);
  *height = static_cast<int>(px);
  return true;
}

core::Result<Image> rasterize(const qr::Matrix& matrix, const RenderOptions& options) {
  if (options.module_px < 1) {
    return core::Result<Image>::err("module_px", "El tamano de modulo debe ser al menos 1 px");
  }
  if (options.quiet_zone < 0) {
    return core::Result<Image>::err("quiet_zone", "La zona de silencio no puede ser negativa");
  }
  if (matrix.size <= 0 ||
      matrix.modules.size() != static_cast<std::size_t>(matrix.size) *
                                   static_cast<std::size_t>(matrix.size)) {
    return core::Result<Image>::err("matrix", "La matriz del simbolo esta incompleta");
  }

  int width = 0;
  int height = 0;
  if (!raster_size(matrix, options, &width, &height)) {
    return core::Result<Image>::err("module_px", "El tamano solicitado es demasiado grande");
  }

  Image image;
  image.width = width;
  image.height = height;
  // The budget above is a pure function of the request, so this only fails if
  // the machine is already out of memory. That must not escape into the render
  // loop, so it is reported like any other failure.
  try {
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4,
                      static_cast<std::uint8_t>(0));
  } catch (const std::bad_alloc&) {
    return core::Result<Image>::err("module_px", "No hay memoria suficiente para esa imagen");
  }
  for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
    image.rgba[i + 0] = options.background.r;
    image.rgba[i + 1] = options.background.g;
    image.rgba[i + 2] = options.background.b;
    image.rgba[i + 3] = 255;
  }

  const int module_px = options.module_px;
  const int quiet_px = options.quiet_zone * module_px;
  for (int y = 0; y < matrix.size; ++y) {
    for (int x = 0; x < matrix.size; ++x) {
      if (!matrix.at(x, y)) continue;
      const int x0 = quiet_px + x * module_px;
      const int y0 = quiet_px + y * module_px;
      for (int py = y0; py < y0 + module_px; ++py) {
        std::uint8_t* row = image.rgba.data() +
                            (static_cast<std::size_t>(py) * static_cast<std::size_t>(width)) * 4;
        for (int px = x0; px < x0 + module_px; ++px) {
          row[static_cast<std::size_t>(px) * 4 + 0] = options.foreground.r;
          row[static_cast<std::size_t>(px) * 4 + 1] = options.foreground.g;
          row[static_cast<std::size_t>(px) * 4 + 2] = options.foreground.b;
        }
      }
    }
  }
  return core::Result<Image>::ok(std::move(image));
}

}  // namespace render