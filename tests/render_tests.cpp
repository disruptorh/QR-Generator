#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "qr/qr_decoder.hpp"
#include "qr/qr_encoder.hpp"
#include "render/png.hpp"
#include "render/raster.hpp"
#include "render/svg.hpp"

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

void check_eq_int(long long got, long long want, const char* what) {
  ++g_checks;
  if (got != want) {
    ++g_failures;
    std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
  }
}

std::uint8_t pixel_at(const render::Image& image, int x, int y) {
  return image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                    static_cast<std::size_t>(x)) * 4];
}

// Every pixel of the quiet zone and of a light module must be the background
// colour, and every pixel of a dark module the foreground colour: no module may
// be split across two shades.
void check_pixels_are_uniform(const render::Image& image, const qr::Matrix& matrix,
                              const render::RenderOptions& options) {
  int width = 0;
  int height = 0;
  render::raster_size(matrix, options, &width, &height);
  const int quiet_px = options.quiet_zone * options.module_px;
  bool ok = true;
  for (int y = 0; y < height && ok; ++y) {
    for (int x = 0; x < width; ++x) {
      const bool in_quiet = x < quiet_px || y < quiet_px || x >= width - quiet_px ||
                            y >= height - quiet_px;
      const int mx = (x - quiet_px) / options.module_px;
      const int my = (y - quiet_px) / options.module_px;
      const bool dark = !in_quiet && mx >= 0 && my >= 0 && mx < matrix.size && my < matrix.size &&
                        matrix.at(mx, my);
      const std::uint8_t want = dark ? options.foreground.r : options.background.r;
      if (pixel_at(image, x, y) != want) ok = false;
    }
  }
  check(ok, "every pixel matches its module colour exactly");
}

void test_rasterize() {
  const auto matrix = qr::encode("hola mundo", qr::EncodeOptions{}, nullptr);
  check(matrix.has_value(), "encode succeeds for the raster test");
  if (!matrix) return;

  render::RenderOptions options;
  const auto image = render::rasterize(*matrix, options);
  check(image.has_value(), "rasterize succeeds");
  if (!image) return;
  const int side = (matrix->size + 2 * options.quiet_zone) * options.module_px;
  check_eq_int(image.value().width, side, "image width follows the quiet zone");
  check_eq_int(image.value().height, side, "image height follows the quiet zone");
  check_eq_int(static_cast<long long>(image.value().rgba.size()),
               static_cast<long long>(side) * side * 4, "RGBA buffer is exactly width * height * 4");
  check_pixels_are_uniform(image.value(), *matrix, options);

  // The corner pixel is always quiet zone, and the centre of the top-left
  // finder pattern's outer ring is always dark.
  check(pixel_at(image.value(), 0, 0) == options.background.r, "top-left corner is quiet zone");
  const int finder_centre = options.quiet_zone * options.module_px + options.module_px * 3;
  check(pixel_at(image.value(), finder_centre, finder_centre) == options.foreground.r,
        "the top-left finder pattern centre is dark");

  // A zero quiet zone still produces a valid image, just without the margin.
  options.quiet_zone = 0;
  const auto tight = render::rasterize(*matrix, options);
  check(tight.has_value(), "rasterize accepts a zero quiet zone");
  if (tight) {
    check_eq_int(tight.value().width, matrix->size * options.module_px,
                 "a zero quiet zone drops the margin");
    check_pixels_are_uniform(tight.value(), *matrix, options);
  }
}

void test_rasterize_rejects_bad_options() {
  const auto matrix = qr::encode("x", qr::EncodeOptions{}, nullptr);
  check(matrix.has_value(), "encode succeeds for the option checks");
  if (!matrix) return;

  render::RenderOptions options;
  options.module_px = 0;
  check(!render::rasterize(*matrix, options).has_value(), "module size 0 is rejected");
  options.module_px = -4;
  check(!render::rasterize(*matrix, options).has_value(), "a negative module size is rejected");
  options.module_px = 8;
  options.quiet_zone = -1;
  check(!render::rasterize(*matrix, options).has_value(), "a negative quiet zone is rejected");

  options.quiet_zone = 4;
  options.module_px = 1 << 20;
  check(!render::rasterize(*matrix, options).has_value(), "an absurd module size is refused");

  const qr::Matrix broken;
  check(!render::rasterize(broken, options).has_value(), "an empty matrix is rejected");
}

void test_png() {
  const auto matrix = qr::encode("PNG writer probe", qr::EncodeOptions{}, nullptr);
  check(matrix.has_value(), "encode succeeds for the PNG test");
  if (!matrix) return;

  const auto image = render::rasterize(*matrix, render::RenderOptions{});
  check(image.has_value(), "rasterize succeeds for the PNG test");
  if (!image) return;

  const auto png = render::encode_png(image.value());
  check(png.has_value(), "encode_png succeeds");
  if (!png) return;

  const std::vector<std::uint8_t>& bytes = png.value();
  const std::uint8_t signature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  check(bytes.size() > 33, "the PNG is longer than signature + IHDR + IEND");
  bool signature_ok = true;
  for (int i = 0; i < 8; ++i) {
    if (bytes[static_cast<std::size_t>(i)] != signature[i]) signature_ok = false;
  }
  check(signature_ok, "the PNG starts with the 8-byte signature");

  const std::string head(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  check(head.find("IHDR") != std::string::npos, "an IHDR chunk is present");
  check(head.find("IDAT") != std::string::npos, "an IDAT chunk is present");
  check(head.find("IEND") != std::string::npos, "an IEND chunk is present");
  check(head.find("IDAT") < head.rfind("IEND"), "IDAT precedes IEND");
  check(bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 13,
        "IHDR declares a 13-byte payload");
  check(bytes[12] == 'I' && bytes[13] == 'H' && bytes[14] == 'D' && bytes[15] == 'R',
        "the first chunk is IHDR");
  // Signature 8 + length 4 + type 4, so IHDR payload starts at byte 16:
  // width 16..19, height 20..23, then the five single-byte fields.
  const auto be32 = [&bytes](std::size_t at) {
    return (static_cast<long long>(bytes[at]) << 24) |
           (static_cast<long long>(bytes[at + 1]) << 16) |
           (static_cast<long long>(bytes[at + 2]) << 8) |
           static_cast<long long>(bytes[at + 3]);
  };
  check_eq_int(be32(16), image.value().width, "IHDR width matches the image");
  check_eq_int(be32(20), image.value().height, "IHDR height matches the image");
  check_eq_int(bytes[24], 8, "bit depth is 8");
  check_eq_int(bytes[25], 6, "colour type is RGBA");
  check_eq_int(bytes[26], 0, "compression method is deflate");
  check_eq_int(bytes[27], 0, "filter method is standard");
  check_eq_int(bytes[28], 0, "interlace is none");
  check(bytes[bytes.size() - 8] == 'I' && bytes[bytes.size() - 7] == 'E' &&
            bytes[bytes.size() - 6] == 'N' && bytes[bytes.size() - 5] == 'D',
        "the stream ends with an IEND chunk");

  // A mismatched buffer must be reported, not written out.
  render::Image broken = image.value();
  broken.rgba.pop_back();
  check(!render::encode_png(broken).has_value(), "a short RGBA buffer is rejected");
  check(!render::encode_png(render::Image{}).has_value(), "an empty image is rejected");
}

void test_svg() {
  const auto matrix = qr::encode("SVG writer probe", qr::EncodeOptions{}, nullptr);
  check(matrix.has_value(), "encode succeeds for the SVG test");
  if (!matrix) return;

  const auto svg = render::encode_svg(*matrix, 4, render::Rgb{0, 0, 0},
                                      render::Rgb{255, 255, 255});
  check(svg.has_value(), "encode_svg succeeds");
  if (!svg) return;

  const std::string& text = svg.value();
  check(text.rfind("<svg", 0) == 0, "the document starts with <svg");
  check(text.find("xmlns=\"http://www.w3.org/2000/svg\"") != std::string::npos,
        "the SVG namespace is declared");
  char expected[64];
  std::snprintf(expected, sizeof(expected), "viewBox=\"0 0 %d %d\"", matrix->size + 8,
                matrix->size + 8);
  check(text.find(expected) != std::string::npos, "the viewBox covers the quiet zone");
  check(text.find("</svg>") != std::string::npos, "the document is closed");
  check(text.find("</svg>\n") == text.size() - 7, "the document ends with a newline");
  check(text.find("<path") != std::string::npos, "dark modules are a path");

  // The run-merging must produce exactly one subpath per horizontal dark run.
  int expected_runs = 0;
  for (int y = 0; y < matrix->size; ++y) {
    bool previous = false;
    for (int x = 0; x < matrix->size; ++x) {
      if (matrix->at(x, y) && !previous) ++expected_runs;
      previous = matrix->at(x, y);
    }
  }
  // The path data uses only M, d, h, v, z and digits, so counting the 'M'
  // separators counts the merged runs exactly.
  const std::size_t data_begin = text.find(" d=\"") + 4;
  const std::size_t data_end = text.find("\"/>", data_begin);
  int emitted = 0;
  for (std::size_t i = data_begin; i < data_end; ++i) {
    if (text[i] == 'M') ++emitted;
  }
  check(expected_runs > 0, "the fixture has dark runs");
  check(emitted == expected_runs, "one subpath per horizontal dark run");

  check(!render::encode_svg(*matrix, -1, {}, {}).has_value(), "a negative quiet zone is rejected");
  check(!render::encode_svg(qr::Matrix{}, 4, {}, {}).has_value(), "an empty matrix is rejected");
}

// The decoded symbol must survive a full render round trip at every ECC level
// and for the largest versions, which is what a real export does.
void test_render_round_trip_decodes() {
  for (int lvl = 0; lvl < 4; ++lvl) {
    for (const int version : {1, 2, 7, 14, 27, 40}) {
      const auto ecc = static_cast<qr::Ecc>(lvl);
      const int cap = qr::byte_capacity(version, ecc) - 2;
      if (cap <= 1) continue;
      const std::string payload = "r" + std::string(static_cast<std::size_t>(cap / 2), 'r');

      qr::EncodeOptions options;
      options.ecc = ecc;
      options.boost_ecc = false;
      options.min_version = version;
      options.max_version = version;
      const auto matrix = qr::encode(payload, options, nullptr);
      check(matrix.has_value(), "encode succeeds for the render round trip");
      if (!matrix) continue;

      render::RenderOptions render_options;
      render_options.module_px = 3;
      const auto image = render::rasterize(*matrix, render_options);
      check(image.has_value(), "rasterize succeeds for the render round trip");
      if (!image) continue;

      const auto png = render::encode_png(image.value());
      check(png.has_value(), "encode_png succeeds for the render round trip");

      // Reading the modules back out of the pixels has to give the original
      // matrix, otherwise what we export is not the symbol we verified.
      const int quiet_px = render_options.quiet_zone * render_options.module_px;
      qr::Matrix recovered;
      recovered.size = matrix->size;
      recovered.modules.assign(static_cast<std::size_t>(matrix->size) *
                                   static_cast<std::size_t>(matrix->size),
                               false);
      bool uniform = true;
      for (int y = 0; y < matrix->size && uniform; ++y) {
        for (int x = 0; x < matrix->size; ++x) {
          const int px = quiet_px + x * render_options.module_px;
          const int py = quiet_px + y * render_options.module_px;
          const bool dark = pixel_at(image.value(), px, py) == render_options.foreground.r;
          if (dark != matrix->at(x, y)) uniform = false;
          recovered.set(x, y, dark);
        }
      }
      check(uniform, "the sampled matrix matches the encoded matrix");
      check(recovered.modules == matrix->modules, "the recovered matrix is identical");

      const auto decoded = qr::decode(recovered, 0);
      check(decoded.has_value() && decoded->text == payload,
            "the symbol recovered from pixels still decodes to the payload");
    }
  }
}

}  // namespace

int main() {
  test_rasterize();
  test_rasterize_rejects_bad_options();
  test_png();
  test_svg();
  test_render_round_trip_decodes();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}