#include "render/svg.hpp"

#include <cstdio>

namespace render {

core::Result<std::string> encode_svg(const qr::Matrix& matrix,
                                     int quiet_zone,
                                     Rgb foreground,
                                     Rgb background) {
  if (quiet_zone < 0) {
    return core::Result<std::string>::err("quiet_zone", "La zona de silencio no puede ser negativa");
  }
  if (matrix.size <= 0 ||
      matrix.modules.size() != static_cast<std::size_t>(matrix.size) *
                                   static_cast<std::size_t>(matrix.size)) {
    return core::Result<std::string>::err("matrix", "La matriz del simbolo esta incompleta");
  }

  const int side = matrix.size + 2 * quiet_zone;
  std::string out;
  out.reserve(static_cast<std::size_t>(side) * 6 + matrix.modules.size() / 4);

  char buffer[160];
  std::snprintf(buffer, sizeof(buffer),
                "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
                "width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">\n",
                side * 8, side * 8, side, side);
  out += buffer;

  std::snprintf(buffer, sizeof(buffer), "  <rect width=\"%d\" height=\"%d\" fill=\"%s\"/>\n", side,
                side, rgb_to_hex(background));
  out += buffer;

  // One path for every dark module. Adjacent modules in a row are merged into a
  // single horizontal run, which roughly halves the emitted data.
  std::snprintf(buffer, sizeof(buffer),
                "  <path fill=\"%s\" d=\"", rgb_to_hex(foreground));
  out += buffer;

  std::size_t written = 0;
  for (int y = 0; y < matrix.size; ++y) {
    int x = 0;
    while (x < matrix.size) {
      if (!matrix.at(x, y)) {
        ++x;
        continue;
      }
      const int run_start = x;
      while (x < matrix.size && matrix.at(x, y)) ++x;
      const int width = x - run_start;
      // Quiet zone shifts every module by `quiet_zone` on both axes.
      std::snprintf(buffer, sizeof(buffer), "M%d %dh%dv%dh-%dz", quiet_zone + run_start,
                    quiet_zone + y, width, 1, width);
      out += buffer;
      ++written;
      if (written % 32 == 0) out += "\n   ";
    }
  }

  out += "\"/>\n</svg>\n";
  return core::Result<std::string>::ok(std::move(out));
}

}  // namespace render