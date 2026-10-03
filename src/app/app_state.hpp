#ifndef QR_APP_APP_STATE_HPP_
#define QR_APP_APP_STATE_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "payload/builders.hpp"
#include "payload/types.hpp"
#include "qr/qr_decoder.hpp"
#include "qr/qr_encoder.hpp"
#include "render/png.hpp"
#include "render/raster.hpp"
#include "render/svg.hpp"

namespace app {

enum class OutputFormat {
  png,
  svg,
};

const char* output_format_label(OutputFormat format);

// Everything the user can type or choose. One plain struct with no behaviour, so
// the UI is a thin editor over it and the whole flow stays testable headless.
struct AppInputs {
  payload::ContentType type = payload::ContentType::text;

  payload::TextInput text;
  payload::UrlInput url;
  payload::WifiInput wifi;
  payload::ContactInput contact;
  payload::EmailInput email;
  payload::SmsInput sms;
  payload::PhoneInput phone;
  payload::GeoInput geo;
  payload::EventInput event;

  qr::EncodeOptions encode;
  render::RenderOptions render;
  OutputFormat format = OutputFormat::png;
};

// A symbol that has been built and then confirmed by the decoder. Nothing is
// ever shown or offered for export that has not been read back.
struct Generated {
  std::string payload;          // the exact string placed in the symbol
  qr::Matrix matrix;
  qr::EncodeStats encode_stats;
  qr::DecodeStats verify_stats; // from re-reading `matrix`
};

// Builds the payload for the selected type, encodes it, and verifies the result
// by decoding it back with correction disabled -- so a symbol that needed
// repairs, or whose bits do not read back as the payload, is reported as an
// error instead of being presented to the user.
//
// The error field names the offending input widget.
core::Result<Generated> generate(const AppInputs& inputs);

// The payload for the current inputs, ignoring validation failures, so the UI can
// show a live preview while the user is still typing.
std::string payload_preview(const AppInputs& inputs);

// Renders an already verified symbol with the user's current render options.
core::Result<render::Image> render_image(const AppInputs& inputs, const Generated& generated);

// File bytes ready to write. `render::encode_png` and `render::encode_svg` are
// used directly; these only exist so the caller cannot accidentally serialize a
// symbol that was never verified.
core::Result<std::vector<std::uint8_t>> export_png(const AppInputs& inputs,
                                                   const Generated& generated);
core::Result<std::string> export_svg(const AppInputs& inputs, const Generated& generated);

// Human-readable version/ECC summary shown next to the preview.
std::string describe(const Generated& generated);

// A safe default name such as "qr-v03-m.png". Never touches the filesystem.
std::string suggested_filename(const AppInputs& inputs, const Generated& generated);

}  // namespace app

#endif  // QR_APP_APP_STATE_HPP_