#ifndef QR_RENDER_SVG_HPP_
#define QR_RENDER_SVG_HPP_

#include <string>

#include "core/result.hpp"
#include "qr/qr_encoder.hpp"
#include "render/raster.hpp"

namespace render {

// Serializes `matrix` as SVG. Modules are emitted as one path of `M x y h v h
// z` rectangles, which is far smaller than one <rect> per dark module and
// renders identically everywhere.
//
// `quiet_zone` is in modules, as for the rasterizer, and the viewBox is sized so
// the document scales to any output resolution without resampling artefacts.
core::Result<std::string> encode_svg(const qr::Matrix& matrix,
                                     int quiet_zone,
                                     Rgb foreground,
                                     Rgb background);

}  // namespace render

#endif  // QR_RENDER_SVG_HPP_