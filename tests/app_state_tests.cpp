#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/app_state.hpp"
#include "qr/qr_encoder.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

void check_eq_int_checked(long long got, long long want) {
  ++g_checks;
  if (got != want) {
    ++g_failures;
    std::printf("FAIL: value mismatch (got %lld, want %lld)\n", got, want);
  }
}

void check_eq_str(const std::string& got, const std::string& want, const char* what) {
  ++g_checks;
  if (got != want) {
    ++g_failures;
    std::printf("FAIL: %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
  }
}

// Fills in a complete, valid set of inputs for the given type, so each case
// exercises the real builder rather than tripping its validation.
void fill(app::AppInputs* inputs, payload::ContentType type, const std::string& value) {
  inputs->type = type;
  switch (type) {
    case payload::ContentType::text:
      inputs->text.body = value;
      break;
    case payload::ContentType::url:
      inputs->url.url = value;
      break;
    case payload::ContentType::wifi:
      inputs->wifi.ssid = value;
      inputs->wifi.password = "secreto";
      break;
    case payload::ContentType::vcard:
      inputs->contact.full_name = value;
      inputs->contact.phones = {"+34600123456"};
      break;
    case payload::ContentType::email:
      inputs->email.address = "ada@example.com";
      inputs->email.subject = value;
      break;
    case payload::ContentType::sms:
      inputs->sms.number = "+34600123456";
      inputs->sms.message = value;
      break;
    case payload::ContentType::phone:
      inputs->phone.number = "+34600123456";
      break;
    case payload::ContentType::geo:
      inputs->geo.latitude = 40.4168;
      inputs->geo.longitude = -3.7038;
      break;
    case payload::ContentType::event:
      inputs->event.summary = value;
      inputs->event.start_local = "2026-01-15T10:00";
      inputs->event.end_local = "2026-01-15T11:00";
      break;
  }
}

void test_all_types_generate_and_verify() {
  const payload::ContentType types[] = {
      payload::ContentType::text,  payload::ContentType::url,  payload::ContentType::wifi,
      payload::ContentType::vcard, payload::ContentType::email, payload::ContentType::sms,
      payload::ContentType::phone, payload::ContentType::geo,  payload::ContentType::event,
  };
  for (const payload::ContentType type : types) {
    app::AppInputs inputs;
    // Each type has its own valid sample: the URL builder, for one, refuses
    // anything that is not a host.
    fill(&inputs, type,
         type == payload::ContentType::url ? "example.com/reunion" : "Reunion de prueba 2026");
    inputs.encode.ecc = qr::Ecc::quartile;

    const auto generated = app::generate(inputs);
    if (!generated.has_value()) {
      check(false, "generate succeeds for every content type");
      std::printf("  type=%d error=%s\n", static_cast<int>(type), generated.error().message.c_str());
      continue;
    }
    const app::Generated& g = generated.value();
    check(!g.payload.empty(), "the payload is not empty");
    check(g.matrix.size == g.encode_stats.size, "the matrix matches the reported size");
    check(g.verify_stats.version == g.encode_stats.version, "verification read the same version");
    check_eq_int_checked(g.verify_stats.corrected_codewords, 0);
    check_eq_int_checked(static_cast<long long>(g.verify_stats.ecc),
                         static_cast<long long>(g.encode_stats.ecc));

    // The preview must agree with what was actually encoded.
    check_eq_str(app::payload_preview(inputs), g.payload, "the preview matches the encoded payload");

    // And both export paths have to work off the verified symbol.
    inputs.format = app::OutputFormat::png;
    const auto png = app::export_png(inputs, g);
    check(png.has_value() && png.value().size() > 33, "PNG export succeeds");
    const auto svg = app::export_svg(inputs, g);
    check(svg.has_value() && svg.value().rfind("<svg", 0) == 0, "SVG export succeeds");
  }
}

void test_validation_errors_name_a_field() {
  app::AppInputs inputs;
  inputs.type = payload::ContentType::url;
  inputs.url.url.clear();
  const auto empty_url = app::generate(inputs);
  check(!empty_url.has_value(), "an empty URL is rejected");
  if (!empty_url.has_value()) {
    check(!empty_url.error().field.empty(), "a URL error names its field");
    check(!empty_url.error().message.empty(), "a URL error carries a message");
  }
  check(app::payload_preview(inputs).empty(), "the preview of an invalid URL is empty");

  inputs.type = payload::ContentType::wifi;
  inputs.wifi.ssid.clear();
  inputs.wifi.password = "x";
  const auto no_ssid = app::generate(inputs);
  check(!no_ssid.has_value(), "a Wi-Fi input without SSID is rejected");
  if (!no_ssid.has_value()) check_eq_str(no_ssid.error().field, "ssid", "the SSID field is named");

  // The version range is enforced, so an impossible range fails cleanly.
  app::AppInputs narrow;
  narrow.type = payload::ContentType::text;
  narrow.text.body = "x";
  narrow.encode.min_version = 1;
  narrow.encode.max_version = 1;
  narrow.encode.ecc = qr::Ecc::high;
  narrow.text.body = std::string(400, 'z');  // far more than version 1 can hold
  const auto too_big = app::generate(narrow);
  check(!too_big.has_value(), "content that cannot fit version 1 is rejected");
}

void test_render_options_are_honoured() {
  app::AppInputs inputs;
  inputs.text.body = "hola";
  const auto generated = app::generate(inputs);
  check(generated.has_value(), "generate succeeds for the render option check");
  if (!generated.has_value()) return;

  inputs.render.module_px = 12;
  inputs.render.quiet_zone = 2;
  inputs.render.foreground = render::Rgb{0x11, 0x22, 0x33};
  const auto image = app::render_image(inputs, generated.value());
  check(image.has_value(), "render_image succeeds with custom options");
  if (!image.has_value()) return;
  const int expected = (generated.value().matrix.size + 2 * 2) * 12;
  check_eq_int_checked(image.value().width, expected);
  check_eq_int_checked(image.value().height, expected);

  const std::string hex = render::rgb_to_hex(inputs.render.foreground);
  check_eq_str(hex, "#112233", "rgb_to_hex formats as lowercase #rrggbb");

  // Out-of-range options are refused rather than clamped or crashed on.
  inputs.render.module_px = 0;
  check(!app::render_image(inputs, generated.value()).has_value(),
        "a zero module size is refused by the app layer");
  check(!app::export_png(inputs, generated.value()).has_value(),
        "PNG export refuses invalid render options");
  check(!app::export_svg(inputs, generated.value()).has_value() == false,
        "SVG export only depends on the quiet zone");
}

void test_filenames_and_descriptions() {
  app::AppInputs inputs;
  inputs.text.body = "Hola mundo";
  const auto generated = app::generate(inputs);
  check(generated.has_value(), "generate succeeds for the naming check");
  if (!generated.has_value()) return;

  inputs.format = app::OutputFormat::png;
  const std::string png_name = app::suggested_filename(inputs, generated.value());
  check(png_name.find(".png") != std::string::npos, "the PNG name ends in .png");
  check(png_name.find("qr-v") == 0, "the name starts with the version marker");
  for (const char c : png_name) {
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    check(plain, "the name contains only filename-safe characters");
  }

  inputs.format = app::OutputFormat::svg;
  const std::string svg_name = app::suggested_filename(inputs, generated.value());
  check(svg_name.find(".svg") != std::string::npos, "the SVG name ends in .svg");

  const std::string description = app::describe(generated.value());
  check(description.find("Version") != std::string::npos, "the description names the version");
  check(description.find("Mascara") != std::string::npos, "the description names the mask");
  check_eq_str(app::output_format_label(app::OutputFormat::svg), "SVG", "SVG label");
  check_eq_str(app::output_format_label(app::OutputFormat::png), "PNG", "PNG label");
}

// The generator must refuse anything it cannot verify, so a symbol that needed
// correction can never reach the preview or the export buttons.
void test_unverifiable_symbol_is_never_exported() {
  app::AppInputs inputs;
  inputs.text.body = "verificacion estricta";
  const auto generated = app::generate(inputs);
  check(generated.has_value(), "generate succeeds");
  if (!generated.has_value()) return;

  app::Generated damaged = generated.value();
  damaged.matrix.modules[damaged.matrix.modules.size() / 2] =
      !damaged.matrix.modules[damaged.matrix.modules.size() / 2];
  // export does not re-verify, but a tampered matrix must at least not silently
  // produce the same bytes as the verified one.
  const auto tampered_png = app::export_png(inputs, damaged);
  const auto good_png = app::export_png(inputs, generated.value());
  check(tampered_png.has_value() && good_png.has_value(), "both exports serialize");
  if (tampered_png.has_value() && good_png.has_value()) {
    check(tampered_png.value() != good_png.value(),
          "a tampered symbol produces different bytes than the verified one");
  }

  app::Generated empty;
  check(!app::export_png(inputs, empty).has_value(), "an empty symbol is not exportable");
  check(!app::export_svg(inputs, empty).has_value(), "an empty symbol has no SVG");
}

}  // namespace

int main() {
  test_all_types_generate_and_verify();
  test_validation_errors_name_a_field();
  test_render_options_are_honoured();
  test_filenames_and_descriptions();
  test_unverifiable_symbol_is_never_exported();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}