// Encoder regression tests. Self-contained: no test framework, no network, no
// external reference library. Every check is an independent re-derivation of the
// QR spec, so a wrong encoder cannot pass by agreeing with itself.
//
// Build: see tests/CMakeLists.txt (target qr_encoder_tests)

#include "qr/qr_encoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL: %s\n", what);
}

void check_eq_int(long long got, long long want, const char* what) {
  ++g_checks;
  if (got == want) return;
  ++g_failures;
  std::printf("FAIL: %s (got %lld, want %lld)\n", what, got, want);
}

// ---------------------------------------------------------------------------
// Spec tables, written out independently of the encoder's own tables.
// ---------------------------------------------------------------------------

constexpr int kMaxVersion = 40;

// Error correction codewords per block, [ecc][version], from ISO/IEC 18004
// tables 13-22. Row order matches qr::Ecc: low, medium, quartile, high.
constexpr int kSpecEccPerBlock[4][kMaxVersion + 1] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
     28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
     26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
     28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
     30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};

// Number of error correction blocks, [ecc][version].
constexpr int kSpecBlocks[4][kMaxVersion + 1] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12,
     12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18,
     20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25,
     27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34,
     30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

// Total number of codewords (data + ECC) per version, from table 1 of the spec.
constexpr int kSpecTotalCodewords[kMaxVersion + 1] = {
    -1, 26, 44, 70, 100, 134, 172, 196, 242, 292, 346, 404, 466, 532, 581, 655,
    733, 815, 901, 991, 1085, 1156, 1258, 1364, 1474, 1588, 1706, 1828, 1921,
    2051, 2185, 2323, 2465, 2611, 2761, 2876, 3034, 3196, 3362, 3532, 3706,
};

// Remainder bits (0-7) per version, given as the version ranges the spec uses.
constexpr int kSpecRemainderBits[kMaxVersion + 1] = {
    -1, 0, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0,
    3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0
};

// Remainder bits, recomputed from first principles as a cross-check on the
// table above: the symbol has size*size modules, and the function patterns plus
// the codewords account for the rest.
int spec_raw_data_modules(int version) {
  int result = (16 * version + 128) * version + 64;
  if (version >= 2) {
    const int num_align = version / 7 + 2;
    result -= (25 * num_align - 10) * num_align - 55;
    if (version >= 7) result -= 36;
  }
  return result;
}

int spec_data_codewords(int version, qr::Ecc ecc) {
  const int e = static_cast<int>(ecc);
  return kSpecTotalCodewords[version] - kSpecEccPerBlock[e][version] * kSpecBlocks[e][version];
}

int spec_char_count_bits(qr::Mode mode, int version) {
  const int tier = (version <= 9) ? 0 : (version <= 26) ? 1 : 2;
  switch (mode) {
    case qr::Mode::numeric: return 10 + tier * 2;
    case qr::Mode::alphanumeric: return 9 + tier * 2;
    case qr::Mode::byte: return 8 + (tier == 0 ? 0 : 8);
  }
  return 8;
}

// ---------------------------------------------------------------------------
// Independent GF(256) / Reed-Solomon, using the 0x11d primitive polynomial.
// ---------------------------------------------------------------------------

const std::uint8_t* gf_exp() {
  static std::uint8_t table[512];
  static bool ready = false;
  if (!ready) {
    std::uint32_t x = 1;
    for (int i = 0; i < 255; ++i) {
      table[i] = static_cast<std::uint8_t>(x);
      x <<= 1;
      if (x & 0x100) x ^= 0x11d;
    }
    for (int i = 255; i < 512; ++i) table[i] = table[i - 255];
    ready = true;
  }
  return table;
}

const std::uint8_t* gf_log() {
  static std::uint8_t table[256];
  static bool ready = false;
  if (!ready) {
    std::uint32_t x = 1;
    for (int i = 0; i < 255; ++i) {
      table[x] = static_cast<std::uint8_t>(i);
      x <<= 1;
      if (x & 0x100) x ^= 0x11d;
    }
    ready = true;
  }
  return table;
}

std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) {
  if (a == 0 || b == 0) return 0;
  return gf_exp()[gf_log()[a] + gf_log()[b]];
}

std::vector<std::uint8_t> spec_reed_solomon_divisor(int degree) {
  std::vector<std::uint8_t> result(static_cast<std::size_t>(degree), 0);
  result[static_cast<std::size_t>(degree - 1)] = 1;
  int root = 1;
  for (int i = 0; i < degree; ++i) {
    for (int j = 0; j < degree; ++j) {
      result[static_cast<std::size_t>(j)] = static_cast<std::uint8_t>(
          gf_mul(result[static_cast<std::size_t>(j)], static_cast<std::uint8_t>(root)) ^
          (j + 1 < degree ? result[static_cast<std::size_t>(j + 1)] : 0));
    }
    root = gf_mul(static_cast<std::uint8_t>(root), 0x02);
  }
  return result;
}

std::vector<std::uint8_t> spec_reed_solomon_remainder(
    const std::vector<std::uint8_t>& data, int degree) {
  const std::vector<std::uint8_t> divisor = spec_reed_solomon_divisor(degree);
  std::vector<std::uint8_t> result(static_cast<std::size_t>(degree), 0);
  for (std::uint8_t b : data) {
    const std::uint8_t factor = static_cast<std::uint8_t>(b ^ result[0]);
    result.erase(result.begin());
    result.push_back(0);
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] = static_cast<std::uint8_t>(result[i] ^ gf_mul(divisor[i], factor));
    }
  }
  return result;
}

// Builds the full codeword stream (data + ECC, interleaved) for a byte-mode
// payload, straight from the spec.
std::vector<std::uint8_t> spec_stream(const std::string& text, int version, qr::Ecc ecc) {
  const int e = static_cast<int>(ecc);
  const int deg = kSpecEccPerBlock[e][version];
  const int nb = kSpecBlocks[e][version];
  const int ndata = spec_data_codewords(version, ecc);

  std::vector<int> bits;
  auto put = [&](std::uint32_t value, int n) {
    for (int k = n - 1; k >= 0; --k) bits.push_back(static_cast<int>((value >> k) & 1));
  };
  put(static_cast<std::uint32_t>(qr::Mode::byte), 4);
  put(static_cast<std::uint32_t>(text.size()),
      spec_char_count_bits(qr::Mode::byte, version));
  for (char ch : text) put(static_cast<std::uint8_t>(ch), 8);
  while (bits.size() % 8 != 0) bits.push_back(0);

  std::vector<std::uint8_t> data;
  for (std::size_t i = 0; i + 8 <= bits.size(); i += 8) {
    std::uint8_t v = 0;
    for (int k = 0; k < 8; ++k) v = static_cast<std::uint8_t>((v << 1) | bits[i + k]);
    data.push_back(v);
  }
  while (data.size() < static_cast<std::size_t>(ndata)) {
    data.push_back(0xEC);
    data.push_back(0x11);
  }
  data.resize(static_cast<std::size_t>(ndata));

  const int short_len = ndata / nb;
  const int num_long = ndata % nb;
  const int num_short = num_long > 0 ? nb - num_long : 0;
  const int common = short_len + (num_long > 0 ? 1 : 0);

  std::vector<std::vector<std::uint8_t>> blocks;
  blocks.reserve(static_cast<std::size_t>(nb));
  std::size_t k = 0;
  for (int i = 0; i < nb; ++i) {
    const int len = short_len + ((num_long > 0 && i >= num_short) ? 1 : 0);
    std::vector<std::uint8_t> block(data.begin() + static_cast<std::ptrdiff_t>(k),
                                    data.begin() + static_cast<std::ptrdiff_t>(k + len));
    k += static_cast<std::size_t>(len);
    const std::vector<std::uint8_t> rem = spec_reed_solomon_remainder(block, deg);
    if (len < common) block.push_back(0);  // placeholder for the missing codeword
    block.insert(block.end(), rem.begin(), rem.end());
    blocks.push_back(std::move(block));
  }

  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(kSpecTotalCodewords[version]));
  for (int i = 0; i < common + deg; ++i) {
    for (int j = 0; j < nb; ++j) {
      if (i == common - 1 && j < num_short) continue;
      out.push_back(blocks[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Independent symbol reader: rebuilds the function-module map from the spec and
// reads the codewords back out of a finished matrix.
// ---------------------------------------------------------------------------

struct Reader {
  int size = 0;
  int version = 0;
  int mask = 0;
  int ecc = 0;  // as qr::Ecc
  std::vector<bool> func;
  std::vector<int> codewords;
  std::vector<int> data;  // de-interleaved data codewords
};

std::vector<bool> reader_map(int version, int size) {
  std::vector<bool> f(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), false);
  auto setf = [&](int x, int y) {
    if (x >= 0 && x < size && y >= 0 && y < size) {
      f[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
        static_cast<std::size_t>(x)] = true;
    }
  };
  for (int i = 0; i < size; ++i) { setf(6, i); setf(i, 6); }
  auto finder = [&](int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) setf(cx + dx, cy + dy);
  };
  finder(3, 3);
  finder(size - 4, 3);
  finder(3, size - 4);
  if (version >= 2) {
    const int n = version / 7 + 2;
    const int step = (version == 32) ? 26 : (version * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
    std::vector<int> pos;
    pos.push_back(6);
    for (int j = 0, p = size - 7; j < n - 1; ++j, p -= step) pos.push_back(p);
    std::reverse(pos.begin() + 1, pos.end());
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
        for (int dy = -2; dy <= 2; ++dy)
          for (int dx = -2; dx <= 2; ++dx) setf(pos[static_cast<std::size_t>(i)] + dx,
                                                pos[static_cast<std::size_t>(j)] + dy);
      }
  }
  if (version >= 7)
    for (int i = 0; i < 18; ++i) {
      const int a = size - 11 + i % 3, b = i / 3;
      setf(a, b);
      setf(b, a);
    }
  for (int i = 0; i <= 5; ++i) setf(8, i);
  setf(8, 7);
  setf(8, 8);
  setf(7, 8);
  for (int i = 9; i < 15; ++i) setf(14 - i, 8);
  for (int i = 0; i < 8; ++i) setf(size - 1 - i, 8);
  for (int i = 8; i < 15; ++i) setf(8, size - 15 + i);
  setf(8, size - 8);
  return f;
}

bool spec_mask_bit(int mask, int x, int y) {
  switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (y / 2 + x / 3) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
  }
  return false;
}

Reader read_symbol(const qr::Matrix& m) {
  Reader r;
  r.size = m.size;
  r.version = (r.size - 17) / 4;

  auto at = [&](int x, int y) { return m.modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(r.size) +
                                                 static_cast<std::size_t>(x)] ? 1 : 0; };
  int bits = 0;
  for (int i = 0; i <= 5; ++i) bits |= at(8, i) << i;
  bits |= at(8, 7) << 6;
  bits |= at(8, 8) << 7;
  bits |= at(7, 8) << 8;
  for (int i = 9; i < 15; ++i) bits |= at(14 - i, 8) << i;
  int best = -1, best_dist = 99;
  for (int d = 0; d < 32; ++d) {
    int rem = d;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int val = ((d << 10) | rem) ^ 0x5412;
    const int dist = __builtin_popcount(static_cast<unsigned>(val ^ bits));
    if (dist < best_dist) { best_dist = dist; best = d; }
  }
  if (best_dist > 3) return r;  // format information unreadable
  r.mask = best & 7;
  r.ecc = ((best >> 3) & 3) ^ 1;  // spec order L,M,Q,H vs. enum low..high

  r.func = reader_map(r.version, r.size);
  std::vector<int> bit;
  for (int right = r.size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < r.size; ++vert)
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const int y = (((right + 1) & 2) == 0) ? r.size - 1 - vert : vert;
        if (r.func[static_cast<std::size_t>(y) * static_cast<std::size_t>(r.size) +
                   static_cast<std::size_t>(x)]) continue;
        int d = at(x, y);
        if (spec_mask_bit(r.mask, x, y)) d = !d;
        bit.push_back(d);
      }
  }
  for (std::size_t i = 0; i + 8 <= bit.size(); i += 8) {
    int v = 0;
    for (int k = 0; k < 8; ++k) v = (v << 1) | bit[i + static_cast<std::size_t>(k)];
    r.codewords.push_back(v);
  }

  // de-interleave: the leading blocks are one codeword shorter
  const int e = r.ecc;
  const int nb = kSpecBlocks[e][r.version];
  const int deg = kSpecEccPerBlock[e][r.version];
  const int ndata = spec_data_codewords(r.version, static_cast<qr::Ecc>(r.ecc));
  const int short_len = ndata / nb;
  const int num_long = ndata % nb;
  const int num_short = num_long > 0 ? nb - num_long : 0;
  const int common = short_len + (num_long > 0 ? 1 : 0);
  std::vector<std::vector<int>> blocks(static_cast<std::size_t>(nb),
                                       std::vector<int>(static_cast<std::size_t>(common + deg), 0));
  std::size_t k = 0;
  for (int i = 0; i < common; ++i)
    for (int j = 0; j < nb; ++j) {
      if (i == common - 1 && j < num_short) continue;
      blocks[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = r.codewords[k++];
    }
  for (int i = 0; i < deg; ++i)
    for (int j = 0; j < nb; ++j)
      blocks[static_cast<std::size_t>(j)][static_cast<std::size_t>(common + i)] = r.codewords[k++];
  for (int j = 0; j < nb; ++j) {
    const int len = short_len + ((num_long > 0 && j >= num_short) ? 1 : 0);
    for (int i = 0; i < len; ++i) r.data.push_back(blocks[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]);
  }
  return r;
}

// Parses the segment stream out of de-interleaved data codewords.
std::string parse_segments(const Reader& r, bool* ok) {
  *ok = true;
  std::string out;
  std::size_t pos = 0;
  auto take = [&](int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
      const std::size_t cw = pos / 8;
      const int bit_index = static_cast<int>(pos % 8);
      if (cw >= r.data.size()) { *ok = false; return 0; }
      v = (v << 1) | ((r.data[cw] >> (7 - bit_index)) & 1);
      ++pos;
    }
    return v;
  };
  static const char kAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
  while (static_cast<int>(r.data.size()) * 8 - static_cast<int>(pos) >= 4) {
    const int mode = take(4);
    if (mode == 0) break;
    int cc = 0;
    if (mode == static_cast<int>(qr::Mode::numeric)) cc = spec_char_count_bits(qr::Mode::numeric, r.version);
    else if (mode == static_cast<int>(qr::Mode::alphanumeric)) cc = spec_char_count_bits(qr::Mode::alphanumeric, r.version);
    else if (mode == static_cast<int>(qr::Mode::byte)) cc = spec_char_count_bits(qr::Mode::byte, r.version);
    else { *ok = false; return std::string(); }
    const int count = take(cc);
    if (mode == static_cast<int>(qr::Mode::numeric)) {
      int i = 0;
      for (; i + 3 <= count; i += 3) {
        const int v = take(10);
        out += static_cast<char>('0' + v / 100);
        out += static_cast<char>('0' + (v / 10) % 10);
        out += static_cast<char>('0' + v % 10);
      }
      if (count - i == 2) {
        const int v = take(7);
        out += static_cast<char>('0' + v / 10);
        out += static_cast<char>('0' + v % 10);
      } else if (count - i == 1) {
        out += static_cast<char>('0' + take(4));
      }
    } else if (mode == static_cast<int>(qr::Mode::alphanumeric)) {
      int i = 0;
      for (; i + 1 < count; i += 2) {
        const int v = take(11);
        out += kAlnum[v / 45];
        out += kAlnum[v % 45];
      }
      if (count - i == 1) out += kAlnum[take(6)];
    } else {
      for (int i = 0; i < count; ++i) out += static_cast<char>(take(8));
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_spec_tables() {
  for (int v = 1; v <= kMaxVersion; ++v) {
    const int raw = spec_raw_data_modules(v);
    check_eq_int(raw / 8, kSpecTotalCodewords[v], "total codewords match the spec formula");
    check_eq_int(raw % 8, kSpecRemainderBits[v], "remainder bits match the spec table");
    for (int e = 0; e < 4; ++e) {
      const auto ecc = static_cast<qr::Ecc>(e);
      const int data = spec_data_codewords(v, ecc);
      check(data > 0 && data < kSpecTotalCodewords[v], "data codewords in range");
    }
  }
}

void test_generator_polynomial() {
  // (x^7 + 0x7f x^6 + ... + 0x75) is the degree-7 generator of the QR spec.
  const std::vector<std::uint8_t> d = spec_reed_solomon_divisor(7);
  const std::uint8_t want[7] = {0x7f, 0x7a, 0x9a, 0xa4, 0x0b, 0x44, 0x75};
  bool ok = d.size() == 7;
  for (std::size_t i = 0; ok && i < 7; ++i) ok = d[i] == want[i];
  check(ok, "degree-7 Reed-Solomon generator polynomial");
}

void test_known_vector() {
  // "hello world", version 1-L: the reference codeword stream from ISO/IEC 18004
  // (th jumper / the canonical worked example used throughout the literature).
  qr::EncodeOptions o;
  o.ecc = qr::Ecc::low;
  o.min_version = 1;
  o.max_version = 1;
  o.mask = 7;
  o.boost_ecc = false;
  qr::EncodeStats st;
  const auto m = qr::encode("hello world", o, &st);
  check(m.has_value(), "known-vector encode succeeds");
  if (!m) return;
  const Reader r = read_symbol(*m);
  static const std::uint8_t kWant[26] = {0x40, 0xb6, 0x86, 0x56, 0xc6, 0xc6, 0xf2, 0x07,
                                         0x76, 0xf7, 0x26, 0xc6, 0x40, 0xec, 0x11, 0xec,
                                         0x11, 0xec, 0x11, 0xb8, 0x68, 0xbf, 0xce, 0x2c,
                                         0x20, 0xa2};
  const std::size_t kWantLen = sizeof(kWant) / sizeof(kWant[0]);
  bool ok = r.codewords.size() == kWantLen && r.version == 1 && r.ecc == 0 && r.mask == 7;
  for (std::size_t i = 0; ok && i < kWantLen; ++i) {
    ok = r.codewords[i] == kWant[i];
    if (!ok) std::printf("  cw %zu: got %02x want %02x\n", i, r.codewords[i], kWant[i]);
  }
  check(ok, "\"hello world\" v1-L matches the reference codeword stream");
}

void test_stream_matches_spec() {
  // Byte-mode payloads, one per ECC level, sized to force a spread of versions.
  struct Case { std::string text; qr::Ecc ecc; };
  const std::vector<Case> cases = {
      {"hello world", qr::Ecc::low},
      {"hola mundo, esto es una prueba larga sin digitos ni mayusculas", qr::Ecc::medium},
      {std::string(300, 'x'), qr::Ecc::quartile},
      {std::string(1000, 'z'), qr::Ecc::high},
      {"https://example.com/path/to/somewhere?query=abc&x=xyz", qr::Ecc::low},
      {"\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba \xc3\xbc\xc3\xb1 utf-8", qr::Ecc::medium},
  };
  for (const Case& c : cases) {
    // force a version large enough that the payload is pure byte mode
    int version = 0;
    for (int v = 1; v <= kMaxVersion; ++v) {
      const int cap = spec_data_codewords(v, c.ecc) * 8 / 8;
      if (static_cast<int>(c.text.size()) <= cap) { version = v; break; }
    }
    if (version == 0) continue;
    qr::EncodeOptions o;
    o.ecc = c.ecc;
    o.min_version = version;
    o.max_version = version;
    o.boost_ecc = false;
    qr::EncodeStats st;
    const auto m = qr::encode(c.text, o, &st);
    check(m.has_value(), "byte-mode encode succeeds");
    if (!m) continue;
    const Reader r = read_symbol(*m);
    const std::vector<std::uint8_t> want = spec_stream(c.text, version, c.ecc);
    bool ok = r.codewords.size() == want.size();
    if (!ok) std::printf("  v%d: got %zu codewords, want %zu\n", version, r.codewords.size(), want.size());
    for (std::size_t i = 0; ok && i < want.size(); ++i) {
      if (r.codewords[i] != want[i]) {
        std::printf("  v%d cw %zu: got %02x want %02x\n", version, i, r.codewords[i], want[i]);
        ok = false;
      }
    }
    check(ok, "codeword stream matches the independent spec implementation");
  }
}

void test_all_versions_round_trip() {
  // Every version, every ECC level: the symbol must decode back to the payload
  // with a reader that knows nothing about the encoder's internals.
  for (int lvl = 0; lvl < 4; ++lvl)
    for (int v = 1; v <= kMaxVersion; ++v) {
      const auto ecc = static_cast<qr::Ecc>(lvl);
      const int cap = spec_data_codewords(v, ecc) - 4;
      if (cap <= 0) continue;
      // lowercase filler, so the payload is pure byte mode and the expected
      // codeword stream is predictable
      const std::string payload = "z" + std::string(static_cast<std::size_t>(cap / 2), 'z');
      qr::EncodeOptions o;
      o.ecc = ecc;
      o.min_version = v;
      o.max_version = v;
      o.boost_ecc = false;
      qr::EncodeStats st;
      const auto m = qr::encode(payload, o, &st);
      check(m.has_value(), "forced-version encode succeeds");
      if (!m) continue;
      check_eq_int(st.version, v, "encoder honours min_version/max_version");
      const Reader r = read_symbol(*m);
      check_eq_int(r.version, v, "version recovered from the symbol size");
      check_eq_int(r.ecc, lvl, "ECC level recovered from the format bits");
      check_eq_int(r.mask, st.mask, "mask recovered from the format bits");
      bool parsed = false;
      const std::string got = parse_segments(r, &parsed);
      check(parsed && got == payload, "payload recovered from the symbol");
      if (parsed && got != payload && got.size() < 80)
        std::printf("  v%d lvl%d: got \"%s\"\n", v, lvl, got.c_str());
    }
}

void test_mixed_mode_round_trip() {
  // Payloads that force the segmenter to use more than one mode.
  const std::vector<std::string> payloads = {
      "BEGIN:VCARD\nVERSION:3.0\nFN:Ada Lovelace\nEMAIL:ada@example.com\nEND:VCARD",
      "BEGIN:VEVENT\nSUMMARY:Reunion\nLOCATION:Madrid\nEND:VEVENT",
      "mailto:ada@example.com?subject=hola&body=que%20tal%20estas",
      "Tel: +34 600 123 456 / 911 22 33 44",
      std::string(70, '7'),
      "ABC 123 def-456 ghi/789",
      "WIFI:T:WPA;S:My Network;P:s3cret;;",
  };
  for (const std::string& text : payloads)
    for (int lvl = 0; lvl < 4; ++lvl) {
      for (int boost = 0; boost < 2; ++boost) {
        qr::EncodeOptions o;
        o.ecc = static_cast<qr::Ecc>(lvl);
        o.boost_ecc = boost != 0;
        qr::EncodeStats st;
        const auto m = qr::encode(text, o, &st);
        check(m.has_value(), "mixed-mode encode succeeds");
        if (!m) continue;
        const Reader r = read_symbol(*m);
        check_eq_int(r.version, st.version, "round-trip version matches");
        check_eq_int(r.ecc, static_cast<int>(st.ecc), "round-trip ECC matches");
        check_eq_int(r.mask, st.mask, "round-trip mask matches");
        bool parsed = false;
        const std::string got = parse_segments(r, &parsed);
        check(parsed && got == text, "mixed-mode payload recovered");
      }
    }
}

void test_segment_optimality() {
  // The segmenter must never cost more bits than encoding the whole string in
  // byte mode, and must pick the mode that actually fits.
  struct Case { std::string text; qr::Mode want; };
  const std::vector<Case> cases = {
      {"1234567890", qr::Mode::numeric},
      {"HELLO WORLD", qr::Mode::alphanumeric},
      {"hello world", qr::Mode::byte},
      {"ABC-123", qr::Mode::alphanumeric},
      {"\xc3\xa1\xc3\xa9", qr::Mode::byte},
  };
  for (const Case& c : cases) {
    const std::vector<qr::Segment> segs = qr::make_segments(c.text, 1);
    bool single = segs.size() == 1;
    if (single && segs[0].mode != c.want) {
      std::printf("  \"%s\": got mode %d, want %d\n", c.text.c_str(), static_cast<int>(segs[0].mode),
                  static_cast<int>(c.want));
      single = false;
    }
    check(single, "single-mode payload uses the right mode");
    // byte mode is always available as a fallback
    const std::size_t byte_cost = 4 + spec_char_count_bits(qr::Mode::byte, 1) + c.text.size() * 8;
    check(qr::optimal_bit_cost(c.text, 1) <= byte_cost, "segment cost never exceeds byte mode");
  }
}

void test_overflow_and_limits() {
  // Too much data for version 40-H must fail rather than truncate.
  qr::EncodeOptions o;
  o.ecc = qr::Ecc::high;
  o.min_version = 1;
  o.max_version = kMaxVersion;
  o.boost_ecc = false;
  const auto m = qr::encode(std::string(5000, 'z'), o, nullptr);
  check(!m.has_value(), "oversized payload is rejected");

  // The largest payload that fits at the requested ECC must still encode.
  const int cap = spec_data_codewords(kMaxVersion, qr::Ecc::high) - 4;
  const auto ok = qr::encode(std::string(static_cast<std::size_t>(cap), 'z'), o, nullptr);
  check(ok.has_value() && cap > 0, "maximum payload encodes");
  if (ok) check_eq_int(ok->size, kMaxVersion * 4 + 17, "maximum payload uses version 40");

  // Empty string encodes to a valid (empty) symbol.
  const auto empty = qr::encode("", o, nullptr);
  check(empty.has_value(), "empty payload encodes");
  if (empty) check_eq_int(empty->size, 21, "empty payload gives a version 1 symbol");

  // Every version is reachable exactly.
  for (int v = 1; v <= kMaxVersion; ++v) {
    qr::EncodeOptions forced;
    forced.ecc = qr::Ecc::low;
    forced.min_version = v;
    forced.max_version = v;
    forced.boost_ecc = false;
    const auto sym = qr::encode("A", forced, nullptr);
    check(sym.has_value(), "every version 1-40 is encodable");
    if (sym) check_eq_int(sym->size, v * 4 + 17, "symbol size follows the version");
  }
}

void test_determinism() {
  const std::string text = "deterministic payload 12345 ABC xyz";
  qr::EncodeOptions o;
  const auto a = qr::encode(text, o, nullptr);
  const auto b = qr::encode(text, o, nullptr);
  check(a.has_value() && b.has_value(), "encode is repeatable");
  if (!a || !b) return;
  check(a->size == b->size, "repeat encode has the same size");
  bool same = true;
  for (int y = 0; y < a->size && same; ++y)
    for (int x = 0; x < a->size; ++x)
      if (a->modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(a->size) +
                      static_cast<std::size_t>(x)] !=
          b->modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(b->size) +
                      static_cast<std::size_t>(x)]) { same = false; break; }
  check(same, "repeat encode produces an identical matrix");
}

void test_all_masks_valid() {
  // Every mask must produce a symbol whose format bits and modules agree.
  const std::string text = "mask coverage payload 0123456789";
  for (int mask = 0; mask < 8; ++mask) {
    qr::EncodeOptions o;
    o.ecc = qr::Ecc::medium;
    o.mask = mask;
    o.boost_ecc = false;
    qr::EncodeStats st;
    const auto m = qr::encode(text, o, &st);
    check(m.has_value(), "forced-mask encode succeeds");
    if (!m) continue;
    check_eq_int(st.mask, mask, "forced mask is honoured");
    const Reader r = read_symbol(*m);
    check_eq_int(r.mask, mask, "forced mask is recorded in the format bits");
    bool parsed = false;
    check(parse_segments(r, &parsed) == text && parsed, "forced-mask symbol decodes");
  }
}

void test_every_mask_decodes_across_versions() {
  // Catches mask formulas that are wrong for a specific pattern, which a
  // single-mask sweep would miss.
  for (int v : {1, 2, 6, 7, 15, 21, 27, 32, 40}) {
    for (int lvl = 0; lvl < 4; ++lvl) {
      for (int mask = 0; mask < 8; ++mask) {
        const auto ecc = static_cast<qr::Ecc>(lvl);
        const int cap = spec_data_codewords(v, ecc) - 4;
        if (cap <= 0) continue;
        qr::EncodeOptions o;
        o.ecc = ecc;
        o.mask = mask;
        o.boost_ecc = false;
        o.min_version = v;
        o.max_version = v;
        qr::EncodeStats st;
        const auto m = qr::encode("z" + std::string(static_cast<std::size_t>(cap / 2), 'z'), o, &st);
        check(m.has_value(), "masked forced-version encode succeeds");
        if (!m) continue;
        const Reader r = read_symbol(*m);
        check_eq_int(r.mask, mask, "mask recorded correctly");
        bool parsed = false;
        check(parse_segments(r, &parsed) == "z" + std::string(static_cast<std::size_t>(cap / 2), 'z') && parsed,
              "masked symbol decodes");
      }
    }
  }
}

}  // namespace

int main() {
  test_spec_tables();
  test_generator_polynomial();
  test_known_vector();
  test_stream_matches_spec();
  test_all_versions_round_trip();
  test_mixed_mode_round_trip();
  test_segment_optimality();
  test_overflow_and_limits();
  test_determinism();
  test_all_masks_valid();
  test_every_mask_decodes_across_versions();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
