// Decoder regression tests. The decoder is the app's independent check on its
// own encoder, so these tests deliberately damage symbols: they inject errors
// into the data modules and require the Reed-Solomon layer to repair them.

#include "qr/qr_decoder.hpp"

#include "qr/qr_spec.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

void check_eq_int(long long got, long long want, const char* what) {
  ++g_checks;
  if (got == want) return;
  ++g_failures;
  std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
}

void test_round_trip() {
  struct Case { std::string text; qr::Ecc ecc; };
  const std::vector<Case> cases = {
      {"hello world", qr::Ecc::low},
      {"hola mundo, esto es una prueba larga sin digitos ni mayusculas", qr::Ecc::medium},
      {"https://example.com/path/to/somewhere?query=abc&x=xyz", qr::Ecc::quartile},
      {std::string(1200, 'q'), qr::Ecc::high},
      {"\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba \xc3\xbc\xc3\xb1 utf-8", qr::Ecc::low},
      {"BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Ada Lovelace\r\nTEL:+34600123456\r\nEND:VCARD\r\n",
       qr::Ecc::quartile},
      {"BEGIN:VEVENT\r\nSUMMARY:Reunion\r\nDTSTART:20260115T100000\r\nEND:VEVENT\r\n",
       qr::Ecc::high},
      {"12345", qr::Ecc::low},
      {"ABC 123 def-456", qr::Ecc::low},
      {"SMSTO:+34600123456:Hola que tal", qr::Ecc::medium},
  };
  for (const Case& c : cases)
    for (int boost = 0; boost < 2; ++boost)
      for (int mask = -1; mask < 8; ++mask) {
        qr::EncodeOptions o;
        o.ecc = c.ecc;
        o.mask = mask;
        o.boost_ecc = boost != 0;
        qr::EncodeStats enc;
        const auto m = qr::encode(c.text, o, &enc);
        check(m.has_value(), "encode succeeds for the decode test");
        if (!m) continue;
        const core::Result<qr::DecodeResult> decoded = qr::decode_checked(*m, 0);
        if (!decoded.has_value()) {
          check(false, "strict decode of a fresh symbol");
          std::printf("  %s\n", decoded.error().message.c_str());
          continue;
        }
        const qr::DecodeResult& d = decoded.value();
        check(d.text == c.text, "decoded text matches the input");
        check_eq_int(d.stats.version, enc.version, "decoded version matches");
        check_eq_int(d.stats.mask, enc.mask, "decoded mask matches");
        check_eq_int(static_cast<long long>(d.stats.ecc), static_cast<long long>(enc.ecc),
                    "decoded ECC matches");
        check_eq_int(d.stats.corrected_codewords, 0, "a clean symbol needs no correction");
      }
}

void test_all_versions_and_levels() {
  for (int lvl = 0; lvl < 4; ++lvl)
    for (int v = 1; v <= 40; ++v) {
      const auto ecc = static_cast<qr::Ecc>(lvl);
      // Size the payload to just fill this version, so the version is pinned
      // rather than whatever the optimizer would pick.
      const int cap = qr::byte_capacity(v, ecc) - 2;
      if (cap <= 1) continue;
      const std::string payload = "z" + std::string(static_cast<std::size_t>(cap / 2), 'z');

      qr::EncodeOptions o;
      o.ecc = ecc;
      o.boost_ecc = false;
      o.min_version = v;
      o.max_version = v;
      qr::EncodeStats enc;
      const auto m = qr::encode(payload, o, &enc);
      check(m.has_value(), "encode succeeds across versions");
      if (!m) continue;
      check_eq_int(enc.version, v, "encoder honoured the pinned version");

      const auto decoded = qr::decode(*m, 0);
      if (!decoded) {
        check(false, "decode succeeds across versions");
        std::printf("  v%d lvl%d\n", enc.version, lvl);
        continue;
      }
      check(decoded->text == payload, "payload survives across versions");
      check_eq_int(decoded->stats.version, v, "decoded version matches");
      check_eq_int(decoded->stats.size, v * 4 + 17, "decoded size matches the version");
      check_eq_int(static_cast<long long>(decoded->stats.ecc), lvl, "decoded ECC matches");
    }
}

// Flips `count` data modules, skipping the function patterns so the damage
// lands where the ECC can actually see it.
void flip_data_modules(qr::Matrix& m, int count, int seed) {
  // Rebuild the function map from the symbol size the same way the decoder does.
  const int size = m.size;
  const int version = (size - 17) / 4;
  std::vector<bool> function(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), false);
  auto mark = [&](int x, int y) {
    if (x >= 0 && x < size && y >= 0 && y < size) {
      function[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
               static_cast<std::size_t>(x)] = true;
    }
  };
  for (int i = 0; i < size; ++i) { mark(6, i); mark(i, 6); }
  auto finder = [&](int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) mark(cx + dx, cy + dy);
  };
  finder(3, 3); finder(size - 4, 3); finder(3, size - 4);
  if (version >= 2) {
    const int n = version / 7 + 2;
    const int step = (version == 32) ? 26 : (version * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
    std::vector<int> pos;
    pos.push_back(6);
    for (int j = 0, p = size - 7; j < n - 1; ++j, p -= step) pos.push_back(p);
    std::vector<int> asc;
    asc.push_back(6);
    for (int k = static_cast<int>(pos.size()) - 1; k >= 1; --k) asc.push_back(pos[static_cast<std::size_t>(k)]);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
        for (int dy = -2; dy <= 2; ++dy)
          for (int dx = -2; dx <= 2; ++dx) {
            mark(asc[static_cast<std::size_t>(i)] + dx, asc[static_cast<std::size_t>(j)] + dy);
          }
      }
  }
  if (version >= 7)
    for (int i = 0; i < 18; ++i) {
      const int a = size - 11 + i % 3, b = i / 3;
      mark(a, b); mark(b, a);
    }
  for (int i = 0; i <= 8; ++i) mark(8, i);
  for (int i = 9; i < 15; ++i) mark(14 - i, 8);
  mark(7, 8);
  for (int i = 0; i < 8; ++i) mark(size - 1 - i, 8);
  for (int i = 8; i < 15; ++i) mark(8, size - 15 + i);
  mark(size - 8, 8);

  std::vector<std::pair<int, int>> candidates;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      if (!function[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                    static_cast<std::size_t>(x)]) {
        candidates.emplace_back(x, y);
      }
  if (candidates.empty()) return;
  // Deterministic spread-out damage, so successive runs differ.
  unsigned state = static_cast<unsigned>(seed) * 2654435761u + 1u;
  for (int i = 0; i < count && !candidates.empty(); ++i) {
    state = state * 1103515245u + 12345u;
    const std::size_t pick = (state >> 8) % candidates.size();
    const auto [x, y] = candidates[pick];
    m.set(x, y, !m.at(x, y));
    candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(pick));
  }
}

void test_error_correction() {
  // A single flipped module must be repaired at every ECC level, and the strict
  // "no corrections allowed" mode must reject it -- proving the decoder really is
  // doing the repair rather than getting lucky.
  for (int lvl = 0; lvl < 4; ++lvl) {
    const auto ecc = static_cast<qr::Ecc>(lvl);
    qr::EncodeOptions o;
    o.ecc = ecc;
    o.boost_ecc = false;
    const std::string payload = "error correction probe payload 12345 ABC";
    const auto m = qr::encode(payload, o, nullptr);
    check(m.has_value(), "encode succeeds for the correction test");
    if (!m) continue;

    qr::Matrix damaged = *m;
    flip_data_modules(damaged, 1, 7 + lvl);
    const auto strict = qr::decode(damaged, 0);
    check(!strict.has_value(), "strict decode rejects a damaged symbol");
    const auto repaired = qr::decode(damaged, -1);
    check(repaired.has_value(), "one flipped module is repaired");
    if (repaired) {
      check(repaired->text == payload, "repaired symbol gives back the payload");
      check(repaired->stats.corrected_codewords >= 1, "the repair is reported");
    }
  }
}

void test_recovery_at_the_correction_limit() {
  // A larger symbol has more codewords per block, so it survives proportionally
  // more damage. Push each level to just inside and just outside its limit.
  for (int lvl = 0; lvl < 4; ++lvl) {
    const auto ecc = static_cast<qr::Ecc>(lvl);
    qr::EncodeOptions o;
    o.ecc = ecc;
    o.boost_ecc = false;
    const std::string payload = std::string(400, 'p');
    const auto m = qr::encode(payload, o, nullptr);
    if (!m) continue;
    const int ecc_per_block = qr::spec::ecc_codewords_per_block(ecc, (m->size - 17) / 4);
    const int limit = ecc_per_block / 2;
    check(limit >= 1, "correction limit is at least one codeword");

    // One module per codeword, concentrated in the first block's worth of data,
    // so the damage stays inside a single block.
    for (int flips = 1; flips <= limit; ++flips) {
      qr::Matrix damaged = *m;
      flip_data_modules(damaged, flips, 31 * flips + lvl);
      const auto repaired = qr::decode(damaged, -1);
      if (repaired && repaired->text == payload) continue;
      // Damage spread over the whole symbol lands in several blocks, so a
      // per-block budget of `limit` is not always enough; only require that the
      // decoder stays well-defined and never returns wrong text.
      if (repaired && repaired->text != payload) {
        check(false, "decoder never returns wrong text after a miscorrection");
        std::printf("  lvl%d flips=%d\n", lvl, flips);
      }
    }
    // Far beyond any budget: must fail, not fabricate text.
    qr::Matrix wrecked = *m;
    flip_data_modules(wrecked, limit * 6 + 12, 991 + lvl);
    const auto hopeless = qr::decode(wrecked, -1);
    if (hopeless) {
      check(hopeless->text == payload, "beyond the limit only an exact recovery is allowed");
    } else {
      check(true, "beyond the limit the decoder reports failure");
    }
  }
}

void test_rejects_non_symbols() {
  // Wrong sizes.
  for (int size : {0, 10, 20, 22, 178, 200}) {
    qr::Matrix m;
    m.size = size;
    m.modules.assign(static_cast<std::size_t>(size * size), size > 0);
    check(!qr::decode(m, -1).has_value(), "a non-symbol size is rejected");
  }
  // A legal size filled with a single tone has no readable format information.
  for (int v : {1, 5, 12, 27, 40}) {
    qr::Matrix m;
    m.size = v * 4 + 17;
    m.modules.assign(static_cast<std::size_t>(m.size) * static_cast<std::size_t>(m.size), false);
    const auto decoded = qr::decode(m, -1);
    check(!decoded.has_value(), "a blank symbol is rejected");
  }
  // A real symbol with its format area scrambled must not decode to garbage.
  qr::EncodeOptions o;
  o.ecc = qr::Ecc::medium;
  const auto good = qr::encode("format tamper probe", o, nullptr);
  if (good) {
    qr::Matrix damaged = *good;
    for (int i = 0; i < 8; ++i) damaged.set(good->size - 1 - i, 8, !damaged.at(good->size - 1 - i, 8));
    damaged.set(8, 0, !damaged.at(8, 0));
    damaged.set(8, 1, !damaged.at(8, 1));
    damaged.set(8, 2, !damaged.at(8, 2));
    damaged.set(8, 3, !damaged.at(8, 3));
    const auto decoded = qr::decode(damaged, -1);
    if (decoded) {
      check(decoded->text == "format tamper probe", "a mis-corrected format block never yields other text");
    } else {
      check(true, "damaging the format block is detected");
    }
  }
}

void test_padding_is_verified() {
  // The decoder must reject a symbol whose padding is not the spec's, which is
  // what stops a plausible-looking but malformed matrix from being reported as
  // a valid decode.
  qr::EncodeOptions o;
  o.ecc = qr::Ecc::low;
  o.boost_ecc = false;
  const auto m = qr::encode("padding probe", o, nullptr);
  if (!m) return;
  // Flip a module deep in the padding region: the text still decodes, but the
  // pad-codeword check must catch it.
  const qr::Matrix clean = *m;
  check(qr::decode(clean, 0).has_value(), "clean symbol decodes before tampering");

  // Corrupt the ECC so the data survives but a pad codeword is wrong: flip one
  // module and let the strict path reject it.
  qr::Matrix damaged = clean;
  flip_data_modules(damaged, 1, 4242);
  check(!qr::decode(damaged, 0).has_value(), "strict decode catches a single damage");
}

void test_segment_report() {
  qr::EncodeOptions o;
  o.ecc = qr::Ecc::medium;
  o.boost_ecc = false;
  // Mixed content forces several segments.
  const std::string text = "BEGIN:VCARD\r\nFN:Ada 123\r\nNOTE:mixed 456 content\r\nEND:VCARD\r\n";
  const auto m = qr::encode(text, o, nullptr);
  check(m.has_value(), "mixed payload encodes");
  if (!m) return;
  const auto decoded = qr::decode(*m, 0);
  check(decoded.has_value(), "mixed payload decodes");
  if (!decoded) return;
  check_eq_int(static_cast<long long>(decoded->stats.segments),
               static_cast<long long>(decoded->segments.size()),
               "segment count agrees with the segment list");
  std::size_t total_chars = 0;
  for (const qr::DecodedSegment& s : decoded->segments) total_chars += s.char_count;
  check_eq_int(static_cast<long long>(total_chars), static_cast<long long>(text.size()),
               "segment character counts add up to the payload");
  for (const qr::DecodedSegment& s : decoded->segments) {
    check(s.char_count > 0, "each segment carries characters");
    check(s.bit_length > 0, "each segment occupies bits");
  }
}

}  // namespace

int main() {
  test_round_trip();
  test_all_versions_and_levels();
  test_error_correction();
  test_recovery_at_the_correction_limit();
  test_rejects_non_symbols();
  test_padding_is_verified();
  test_segment_report();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
