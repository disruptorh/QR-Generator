#include "app/app_state.hpp"

#include <cstdio>

namespace app {
namespace {

const char* ecc_short(qr::Ecc ecc) {
  switch (ecc) {
    case qr::Ecc::low: return "L";
    case qr::Ecc::medium: return "M";
    case qr::Ecc::quartile: return "Q";
    case qr::Ecc::high: return "H";
  }
  return "?";
}

// Runs the builder for the selected type. Each builder is pure, so this is a
// plain dispatch with no fallback that could silently pick the wrong type.
core::Result<std::string> build_payload(const AppInputs& inputs) {
  switch (inputs.type) {
    case payload::ContentType::text: return payload::build_text(inputs.text);
    case payload::ContentType::url: return payload::build_url(inputs.url);
    case payload::ContentType::wifi: return payload::build_wifi(inputs.wifi);
    case payload::ContentType::vcard: return payload::build_vcard(inputs.contact);
    case payload::ContentType::email: return payload::build_email(inputs.email);
    case payload::ContentType::sms: return payload::build_sms(inputs.sms);
    case payload::ContentType::phone: return payload::build_phone(inputs.phone);
    case payload::ContentType::geo: return payload::build_geo(inputs.geo);
    case payload::ContentType::event: return payload::build_event(inputs.event);
  }
  return core::Result<std::string>::err("type", "Tipo de contenido desconocido");
}

// A date such as 20260115T100000 comes from untrusted input, so the name is
// rebuilt from the payload instead of trusting any characters from it.
std::string slug_from_payload(const std::string& payload) {
  std::string slug;
  slug.reserve(24);
  for (const char c : payload) {
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!plain && slug.size() >= 4) break;
    slug.push_back(plain ? c : '-');
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  return slug.empty() ? std::string("contenido") : slug.substr(0, 32);
}

}  // namespace

const char* output_format_label(OutputFormat format) {
  return format == OutputFormat::svg ? "SVG" : "PNG";
}

core::Result<Generated> generate(const AppInputs& inputs) {
  const core::Result<std::string> built = build_payload(inputs);
  if (!built.has_value()) return core::Result<Generated>::err(built.error());
  const std::string& text = built.value();

  Generated generated;
  generated.payload = text;

  qr::EncodeStats stats;
  const std::optional<qr::Matrix> matrix = qr::encode(text, inputs.encode, &stats);
  if (!matrix.has_value()) {
    return core::Result<Generated>::err(
        "payload", "El contenido no cabe en un codigo QR de esa version");
  }
  generated.matrix = *matrix;
  generated.encode_stats = stats;

  // Nothing leaves this function unverified: the symbol is read back with error
  // correction disabled, so both the parity bits and the segment stream have to
  // reproduce the payload exactly.
  const core::Result<qr::DecodeResult> decoded = qr::decode_checked(generated.matrix, 0);
  if (!decoded.has_value()) {
    return core::Result<Generated>::err(
        "payload", std::string("El simbolo generado no se pudo verificar: ") +
                       decoded.error().message);
  }
  if (decoded.value().text != generated.payload) {
    return core::Result<Generated>::err(
        "payload", "El simbolo generado no reproduce el contenido al leerlo");
  }
  generated.verify_stats = decoded.value().stats;
  return core::Result<Generated>::ok(std::move(generated));
}

std::string payload_preview(const AppInputs& inputs) {
  const core::Result<std::string> built = build_payload(inputs);
  return built.has_value() ? built.value() : std::string();
}

core::Result<render::Image> render_image(const AppInputs& inputs, const Generated& generated) {
  if (generated.matrix.size <= 0) {
    return core::Result<render::Image>::err("matrix", "No hay ningun simbolo generado");
  }
  return render::rasterize(generated.matrix, inputs.render);
}

core::Result<std::vector<std::uint8_t>> export_png(const AppInputs& inputs,
                                                   const Generated& generated) {
  const core::Result<render::Image> image = render_image(inputs, generated);
  if (!image.has_value()) return core::Result<std::vector<std::uint8_t>>::err(image.error());
  return render::encode_png(image.value());
}

core::Result<std::string> export_svg(const AppInputs& inputs, const Generated& generated) {
  if (generated.matrix.size <= 0) {
    return core::Result<std::string>::err("matrix", "No hay ningun simbolo generado");
  }
  return render::encode_svg(generated.matrix, inputs.render.quiet_zone, inputs.render.foreground,
                            inputs.render.background);
}

std::string describe(const Generated& generated) {
  const qr::EncodeStats& s = generated.encode_stats;
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), "Version %d  |  %s  |  Mascara %d  |  %d x %d modulos",
                s.version, ecc_short(s.ecc), s.mask, s.size, s.size);
  return buffer;
}

std::string suggested_filename(const AppInputs& inputs, const Generated& generated) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "qr-v%02d-%s", generated.encode_stats.version,
                ecc_short(generated.encode_stats.ecc));
  const std::string extension = inputs.format == OutputFormat::svg ? ".svg" : ".png";
  return std::string(buffer) + "-" + slug_from_payload(generated.payload) + extension;
}

}  // namespace app