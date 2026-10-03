#include "qr/qr_encoder.hpp"

#include "qr/qr_spec.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace qr {
namespace {

// Module-count bounds.
constexpr int kMinVersion = spec::kMinVersion;
constexpr int kMaxVersion = spec::kMaxVersion;

// Mask penalty weights from the spec's evaluation rules.
constexpr int kPenN1 = 3;
constexpr int kPenN2 = 3;
constexpr int kPenN3 = 40;
constexpr int kPenN4 = 10;

// --------------------------------------------------------------------------
// Bit buffer
// --------------------------------------------------------------------------
class BitBuffer {
 public:
  void append(std::uint32_t value, int length) {
    for (int i = length - 1; i >= 0; --i) {
      bits_.push_back(static_cast<std::uint8_t>((value >> i) & 1u));
    }
  }

  void reset_to_with(const std::vector<std::uint8_t>& value) {
    bits_.insert(bits_.end(), value.begin(), value.end());
  }

  std::size_t size() const { return bits_.size(); }
  const std::vector<std::uint8_t>& bits() const { return bits_; }

 private:
  std::vector<std::uint8_t> bits_;
};

std::vector<std::uint8_t> to_codewords(const std::vector<std::uint8_t>& bits) {
  std::vector<std::uint8_t> out((bits.size() + 7) / 8, 0);
  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (bits[i] != 0) {
      out[i / 8] |= static_cast<std::uint8_t>(1u << (7 - (i % 8)));
    }
  }
  return out;
}

// --------------------------------------------------------------------------
// Galois field GF(256), primitive polynomial 0x11D
// --------------------------------------------------------------------------
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

// Generator polynomial for `degree` error-correction codewords.
std::vector<std::uint8_t> reed_solomon_divisor(int degree) {
  std::vector<std::uint8_t> result(static_cast<std::size_t>(degree), 0);
  result[static_cast<std::size_t>(degree - 1)] = 1;
  int root = 1;
  for (int i = 0; i < degree; ++i) {
    for (int j = 0; j < degree; ++j) {
      result[static_cast<std::size_t>(j)] = static_cast<std::uint8_t>(
          gf_mul(result[static_cast<std::size_t>(j)], root) ^
          (j + 1 < degree ? result[static_cast<std::size_t>(j + 1)] : 0));
    }
    root = gf_mul(static_cast<std::uint8_t>(root), 0x02);
  }
  return result;
}

std::vector<std::uint8_t> reed_solomon_remainder(
    const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& divisor) {
  std::vector<std::uint8_t> result(divisor.size(), 0);
  for (std::uint8_t b : data) {
    const std::uint8_t factor = static_cast<std::uint8_t>(b ^ result[0]);
    result.erase(result.begin());
    result.push_back(0);
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] ^= gf_mul(divisor[i], factor);
    }
  }
  return result;
}

// Appends `degree` ECC codewords to every block, then interleaves blocks.
std::vector<std::uint8_t> add_ecc_and_interleave(
    const std::vector<std::uint8_t>& data, int version, Ecc ecc) {
  const int num_blocks = spec::num_error_correction_blocks(ecc, version);
  const int block_ecc_len = spec::ecc_codewords_per_block(ecc, version);
  const int num_data = spec::data_codewords(ecc, version);
  // Standard split: blocks are as even as possible, and the blocks that end up
  // one codeword shorter are the leading ones.
  const int short_block_data = num_data / num_blocks;
  const int num_long_blocks = num_data % num_blocks;
  const int num_short_blocks = (num_long_blocks > 0) ? num_blocks - num_long_blocks : 0;
  // Every block is grown to the long length so the interleave can use one index.
  const int common_data = short_block_data + (num_long_blocks > 0 ? 1 : 0);

  const std::vector<std::uint8_t> divisor = reed_solomon_divisor(block_ecc_len);

  std::vector<std::vector<std::uint8_t>> blocks;
  blocks.reserve(static_cast<std::size_t>(num_blocks));
  std::size_t k = 0;
  for (int i = 0; i < num_blocks; ++i) {
    const int data_len =
        short_block_data + ((num_long_blocks > 0 && i >= num_short_blocks) ? 1 : 0);
    std::vector<std::uint8_t> block(data.begin() + static_cast<std::ptrdiff_t>(k),
                                    data.begin() + static_cast<std::ptrdiff_t>(k + data_len));
    k += data_len;
    const std::vector<std::uint8_t> rem = reed_solomon_remainder(block, divisor);
    // Short blocks carry one codeword less, so they get a placeholder here to
    // keep every block the same length; the interleave below drops it.
    if (data_len < common_data) block.push_back(0);
    block.insert(block.end(), rem.begin(), rem.end());
    blocks.push_back(std::move(block));
  }

  std::vector<std::uint8_t> result;
  result.reserve(static_cast<std::size_t>(spec::raw_data_modules(version) / 8));
  for (int i = 0; i < common_data + block_ecc_len; ++i) {
    for (int j = 0; j < num_blocks; ++j) {
      // The short blocks have no data codeword in the last data position.
      if (i == common_data - 1 && j < num_short_blocks) continue;
      result.push_back(blocks[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]);
    }
  }
  return result;
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------
class SymbolBuilder {
 public:
  explicit SymbolBuilder(int version)
      : size_(version * 4 + 17),
        modules_(static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_), false),
        is_function_(modules_.size(), false) {}

  int size() const { return size_; }
  Matrix take() { return Matrix{size_, std::move(modules_)}; }
  const std::vector<bool>& modules() const { return modules_; }

  void set_function(int x, int y, bool dark) {
    modules_[static_cast<std::size_t>(y) * static_cast<std::size_t>(size_) +
              static_cast<std::size_t>(x)] = dark;
    is_function_[static_cast<std::size_t>(y) * static_cast<std::size_t>(size_) +
                 static_cast<std::size_t>(x)] = true;
  }
  void set_data(int x, int y, bool dark) {
    modules_[static_cast<std::size_t>(y) * static_cast<std::size_t>(size_) +
              static_cast<std::size_t>(x)] = dark;
  }
  bool is_function(int x, int y) const {
    return is_function_[static_cast<std::size_t>(y) * static_cast<std::size_t>(size_) +
                        static_cast<std::size_t>(x)];
  }

 private:
  int size_;
  std::vector<bool> modules_;
  std::vector<bool> is_function_;
};

void draw_finder_pattern(SymbolBuilder& sb, int x, int y) {
  for (int dy = -4; dy <= 4; ++dy) {
    for (int dx = -4; dx <= 4; ++dx) {
      const int dist = std::max(std::abs(dx), std::abs(dy));
      const int xx = x + dx;
      const int yy = y + dy;
      if (xx >= 0 && xx < sb.size() && yy >= 0 && yy < sb.size()) {
        sb.set_function(xx, yy, dist != 2 && dist != 4);
      }
    }
  }
}

int format_bits(Ecc ecc, int mask);
void draw_format_bits_value(SymbolBuilder& sb, Ecc ecc, int mask);
void draw_version_bits(SymbolBuilder& sb, int version);

void draw_function_patterns(SymbolBuilder& sb, int version) {
  const int size = sb.size();

  // Timing patterns.
  for (int i = 0; i < size; ++i) {
    sb.set_function(6, i, i % 2 == 0);
    sb.set_function(i, 6, i % 2 == 0);
  }

  // Finder patterns plus separators.
  draw_finder_pattern(sb, 3, 3);
  draw_finder_pattern(sb, size - 4, 3);
  draw_finder_pattern(sb, 3, size - 4);

  // Alignment patterns, skipping the three that collide with finders.
  const std::vector<int> align = spec::alignment_positions(version);
  for (std::size_t i = 0; i < align.size(); ++i) {
    for (std::size_t j = 0; j < align.size(); ++j) {
      const bool corner = (i == 0 && j == 0) ||
                          (i == 0 && j + 1 == align.size()) ||
                          (i + 1 == align.size() && j == 0);
      if (corner) continue;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          sb.set_function(align[j] + dx, align[i] + dy,
                          std::max(std::abs(dx), std::abs(dy)) != 1);
        }
      }
    }
  }

  // Reserve the format-information area (mask 0 is rewritten later) and the
  // version-information area so the data placement skips them.
  draw_format_bits_value(sb, Ecc::low, 0);
  if (version >= 7) {
    draw_version_bits(sb, version);
  }
}

int format_bits(Ecc ecc, int mask) { return spec::format_bits(ecc, mask); }

void draw_format_bits_value(SymbolBuilder& sb, Ecc ecc, int mask) {
  const int size = sb.size();
  const int bits = format_bits(ecc, mask);
  auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };

  for (int i = 0; i <= 5; ++i) sb.set_function(8, i, bit(i));
  sb.set_function(8, 7, bit(6));
  sb.set_function(8, 8, bit(7));
  sb.set_function(7, 8, bit(8));
  for (int i = 9; i < 15; ++i) sb.set_function(14 - i, 8, bit(i));

  for (int i = 0; i < 8; ++i) sb.set_function(size - 1 - i, 8, bit(i));
  for (int i = 8; i < 15; ++i) sb.set_function(8, size - 15 + i, bit(i));
  sb.set_function(8, size - 8, true);  // always dark
}

void draw_version_bits(SymbolBuilder& sb, int version) {
  const int size = sb.size();
  const int bits = spec::version_bits(version);
  for (int i = 0; i < 18; ++i) {
    const bool bit = ((bits >> i) & 1) != 0;
    const int a = size - 11 + i % 3;
    const int b = i / 3;
    sb.set_function(a, b, bit);
    sb.set_function(b, a, bit);
  }
}

void draw_codewords(SymbolBuilder& sb, const std::vector<std::uint8_t>& data) {
  const int size = sb.size();
  std::size_t i = 0;
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < size; ++vert) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? size - 1 - vert : vert;
        if (!sb.is_function(x, y) && i < data.size() * 8) {
          const bool dark = ((data[i >> 3] >> (7 - (i & 7))) & 1u) != 0;
          sb.set_data(x, y, dark);
          ++i;
        }
      }
    }
  }
}

void apply_mask(SymbolBuilder& sb, int mask) {
  for (int y = 0; y < sb.size(); ++y) {
    for (int x = 0; x < sb.size(); ++x) {
      if (sb.is_function(x, y)) continue;
      if (spec::mask_bit(mask, x, y)) {
        const std::size_t idx = static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(sb.size()) +
                                static_cast<std::size_t>(x);
        // Flip through the public setter to keep the two vectors consistent.
        sb.set_data(x, y, !sb.modules()[idx]);
      }
    }
  }
}

int penalty_score_impl(const std::vector<bool>& modules, int size) {
  auto at = [&modules, size](int x, int y) {
    return modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                   static_cast<std::size_t>(x)]
               ? 1
               : 0;
  };

  int score = 0;

  // Rule 1: runs of five or more identical modules in a row or column.
  for (int y = 0; y < size; ++y) {
    int run_color = at(0, y);
    int run_len = 1;
    for (int x = 1; x < size; ++x) {
      const int c = at(x, y);
      if (c == run_color) {
        ++run_len;
      } else {
        if (run_len >= 5) score += kPenN1 + (run_len - 5);
        run_color = c;
        run_len = 1;
      }
    }
    if (run_len >= 5) score += kPenN1 + (run_len - 5);
  }
  for (int x = 0; x < size; ++x) {
    int run_color = at(x, 0);
    int run_len = 1;
    for (int y = 1; y < size; ++y) {
      const int c = at(x, y);
      if (c == run_color) {
        ++run_len;
      } else {
        if (run_len >= 5) score += kPenN1 + (run_len - 5);
        run_color = c;
        run_len = 1;
      }
    }
    if (run_len >= 5) score += kPenN1 + (run_len - 5);
  }

  // Rule 2: 2x2 blocks of one colour.
  for (int y = 0; y + 1 < size; ++y) {
    for (int x = 0; x + 1 < size; ++x) {
      const int c = at(x, y);
      if (c == at(x + 1, y) && c == at(x, y + 1) && c == at(x + 1, y + 1)) {
        score += kPenN2;
      }
    }
  }

  // Rule 3: finder-like 1:1:3:1:1 patterns with a four-module light margin.
  auto finder_like_runs = [&score](int length, auto module_at) {
    // Explicit window search: 00001011101 and 10111010000.
    const char* kDark = "10111010000";
    const char* kLight = "00001011101";
    for (int i = 0; i + 11 <= length; ++i) {
      bool dark_ok = true;
      bool light_ok = true;
      for (int k = 0; k < 11; ++k) {
        const int c = module_at(i + k);
        if (c != (kDark[k] - '0')) dark_ok = false;
        if (c != (kLight[k] - '0')) light_ok = false;
      }
      if (dark_ok || light_ok) score += kPenN3;
    }
  };

  for (int y = 0; y < size; ++y) {
    finder_like_runs(size, [&at, y](int x) { return at(x, y); });
  }
  for (int x = 0; x < size; ++x) {
    finder_like_runs(size, [&at, x](int y) { return at(x, y); });
  }

  // Rule 4: overall balance of dark modules.
  int dark = 0;
  for (bool m : modules) {
    if (m) ++dark;
  }
  const int total = size * size;
  const int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
  score += k * kPenN4;
  return score;
}

}  // namespace

const char* ecc_label(Ecc ecc) {
  switch (ecc) {
    case Ecc::low: return "L";
    case Ecc::medium: return "M";
    case Ecc::quartile: return "Q";
    case Ecc::high: return "H";
  }
  return "M";
}

bool is_alphanumeric(const std::string& text) {
  for (char c : text) {
    if (std::strchr(spec::alnum_table(), c) == nullptr || c == '\0') return false;
  }
  return true;
}

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_alnum_char(char c) {
  return c != '\0' && std::strchr(spec::alnum_table(), c) != nullptr;
}

// Cost in bits of encoding `len` characters of `text` starting at `pos` in
// `mode`, excluding the mode indicator and the character-count field.
std::size_t run_data_bits(Mode mode, std::size_t pos, std::size_t len) {
  switch (mode) {
    case Mode::numeric: {
      std::size_t bits = 0;
      std::size_t k = pos;
      const std::size_t end = pos + len;
      while (k + 3 <= end) {
        bits += 10;
        k += 3;
      }
      const std::size_t rem = end - k;
      bits += (rem == 2) ? 7 : (rem == 1 ? 4 : 0);
      return bits;
    }
    case Mode::alphanumeric: {
      std::size_t bits = 0;
      std::size_t k = pos;
      const std::size_t end = pos + len;
      while (k + 2 <= end) {
        bits += 11;
        k += 2;
      }
      bits += (end - k) * 6;
      return bits;
    }
    case Mode::byte:
    default:
      return len * 8;
  }
}

void encode_run(Mode mode, const std::string& text, std::size_t pos,
                std::size_t len, BitBuffer* out) {
  switch (mode) {
    case Mode::numeric: {
      std::size_t k = pos;
      const std::size_t end = pos + len;
      while (k < end) {
        unsigned value = 0;
        int digits = 0;
        while (digits < 3 && k + static_cast<std::size_t>(digits) < end) {
          value = value * 10 + static_cast<unsigned>(
                               text[k + static_cast<std::size_t>(digits)] - '0');
          ++digits;
        }
        out->append(value, digits == 3 ? 10 : (digits == 2 ? 7 : 4));
        k += static_cast<std::size_t>(digits);
      }
      break;
    }
    case Mode::alphanumeric: {
      std::size_t k = pos;
      const std::size_t end = pos + len;
      while (k < end) {
        const int v1 = static_cast<int>(
            std::strchr(spec::alnum_table(), text[k]) - spec::alnum_table());
        if (k + 1 < end) {
          const int v2 = static_cast<int>(
              std::strchr(spec::alnum_table(), text[k + 1]) - spec::alnum_table());
          out->append(static_cast<std::uint32_t>(v1 * 45 + v2), 11);
          k += 2;
        } else {
          out->append(static_cast<std::uint32_t>(v1), 6);
          k += 1;
        }
      }
      break;
    }
    case Mode::byte:
    default:
      for (std::size_t k = pos; k < pos + len; ++k) {
        out->append(static_cast<std::uint8_t>(text[k]), 8);
      }
      break;
  }
}

// Candidate run lengths for one start position and one mode. Within a mode the
// bit cost is monotone in the run length, so only the short runs (where paying
// a fresh header can still pay off) and the longest runs matter.
void candidate_lengths(Mode mode, std::size_t max_len,
                       std::vector<std::size_t>* out) {
  out->clear();
  for (std::size_t k = 1; k <= 6 && k <= max_len; ++k) out->push_back(k);
  const std::size_t tail = (mode == Mode::numeric) ? 3 : (mode == Mode::alphanumeric ? 2 : 1);
  for (std::size_t k = max_len; k > 6 && k + tail > max_len; --k) {
    out->push_back(k);
  }
}

constexpr std::size_t kInfBits = static_cast<std::size_t>(-1) / 4;

}  // namespace

std::size_t optimal_bit_cost(const std::string& text, int version) {
  const std::size_t n = text.size();
  if (n == 0) return 0;
  std::vector<std::size_t> best(n + 1, kInfBits);
  std::vector<std::size_t> prev(n + 1, 0);
  std::vector<Mode> prev_mode(n + 1, Mode::byte);
  std::vector<std::size_t> prev_len(n + 1, 0);
  best[0] = 0;

  std::vector<std::size_t> cands;
  for (std::size_t i = 0; i < n; ++i) {
    if (best[i] == kInfBits) continue;

    // Longest run available for each mode at this position.
    std::size_t num_run = 0;
    while (i + num_run < n && is_digit(text[i + num_run])) ++num_run;
    std::size_t alnum_run = 0;
    while (i + alnum_run < n && is_alnum_char(text[i + alnum_run])) ++alnum_run;

    const std::size_t max_by_mode[3] = {num_run, alnum_run, n - i};
    const Mode mode_of[3] = {Mode::numeric, Mode::alphanumeric, Mode::byte};
    for (int m = 0; m < 3; ++m) {
      const std::size_t max_len = max_by_mode[m];
      if (max_len == 0) continue;
      candidate_lengths(mode_of[m], max_len, &cands);
      for (std::size_t k : cands) {
        if (k > max_len) continue;
        if (mode_of[m] == Mode::numeric) {
          for (std::size_t c = i; c < i + k; ++c) {
            if (!is_digit(text[c])) { k = 0; break; }
          }
          if (k == 0) continue;
        } else if (mode_of[m] == Mode::alphanumeric) {
          for (std::size_t c = i; c < i + k; ++c) {
            if (!is_alnum_char(text[c])) { k = 0; break; }
          }
          if (k == 0) continue;
        }
        const std::size_t cost =
            4u + static_cast<std::size_t>(spec::char_count_bits(mode_of[m], version)) +
            run_data_bits(mode_of[m], i, k);
        const std::size_t total = best[i] + cost;
        if (total < best[i + k]) {
          best[i + k] = total;
          prev[i + k] = i;
          prev_mode[i + k] = mode_of[m];
          prev_len[i + k] = k;
        }
      }
    }
  }
  return best[n];
}

std::vector<Segment> make_segments(const std::string& text, int version) {
  std::vector<Segment> result;
  const std::size_t n = text.size();
  if (n == 0) return result;

  std::vector<std::size_t> best(n + 1, kInfBits);
  std::vector<std::size_t> prev(n + 1, 0);
  std::vector<Mode> prev_mode(n + 1, Mode::byte);
  std::vector<std::size_t> prev_len(n + 1, 0);
  best[0] = 0;

  std::vector<std::size_t> cands;
  for (std::size_t i = 0; i < n; ++i) {
    if (best[i] == kInfBits) continue;

    std::size_t num_run = 0;
    while (i + num_run < n && is_digit(text[i + num_run])) ++num_run;
    std::size_t alnum_run = 0;
    while (i + alnum_run < n && is_alnum_char(text[i + alnum_run])) ++alnum_run;

    const std::size_t max_by_mode[3] = {num_run, alnum_run, n - i};
    const Mode mode_of[3] = {Mode::numeric, Mode::alphanumeric, Mode::byte};
    for (int m = 0; m < 3; ++m) {
      const std::size_t max_len = max_by_mode[m];
      if (max_len == 0) continue;
      candidate_lengths(mode_of[m], max_len, &cands);
      for (std::size_t k : cands) {
        if (k > max_len) continue;
        bool fits = true;
        if (mode_of[m] == Mode::numeric) {
          for (std::size_t c = i; c < i + k && fits; ++c) fits = is_digit(text[c]);
        } else if (mode_of[m] == Mode::alphanumeric) {
          for (std::size_t c = i; c < i + k && fits; ++c) fits = is_alnum_char(text[c]);
        }
        if (!fits) continue;
        const std::size_t cost =
            4u + static_cast<std::size_t>(spec::char_count_bits(mode_of[m], version)) +
            run_data_bits(mode_of[m], i, k);
        if (best[i] + cost < best[i + k]) {
          best[i + k] = best[i] + cost;
          prev[i + k] = i;
          prev_mode[i + k] = mode_of[m];
          prev_len[i + k] = k;
        }
      }
    }
  }
  if (best[n] == kInfBits) return result;  // unreachable: byte mode always fits

  // Walk the chain backwards, then emit the segments in order.
  struct Step {
    Mode mode;
    std::size_t pos;
    std::size_t len;
  };
  std::vector<Step> steps;
  for (std::size_t at = n; at > 0; at = prev[at]) {
    steps.push_back(Step{prev_mode[at], prev[at], prev_len[at]});
  }
  for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
    Segment seg;
    seg.mode = it->mode;
    seg.char_count = it->len;
    BitBuffer buf;
    encode_run(it->mode, text, it->pos, it->len, &buf);
    seg.data_bits = buf.bits();
    result.push_back(std::move(seg));
  }
  return result;
}

Ecc max_ecc_for_version(int version, std::size_t bit_length) {
  for (int ecc = 3; ecc >= 0; --ecc) {
    const int capacity_bits = spec::data_codewords(static_cast<Ecc>(ecc), version) * 8;
    if (static_cast<int>(bit_length) <= capacity_bits) {
      return static_cast<Ecc>(ecc);
    }
  }
  return Ecc::low;
}

int byte_capacity(int version, Ecc ecc) {
  const int bits = spec::data_codewords(ecc, version) * 8;
  const int header = 4 + spec::char_count_bits(Mode::byte, version);
  if (bits < header + 8) return 0;
  return (bits - header) / 8;
}

int penalty_score(const Matrix& matrix) {
  return penalty_score_impl(matrix.modules, matrix.size);
}

std::optional<Matrix> encode(const std::string& text,
                             const EncodeOptions& options,
                             EncodeStats* stats) {
  if (options.min_version < kMinVersion || options.max_version > kMaxVersion ||
      options.min_version > options.max_version) {
    return std::nullopt;
  }
  if (options.mask < -1 || options.mask > 7) return std::nullopt;

  // Smallest version in range that fits the payload at the requested ECC. The
  // segmentation is recomputed per version because the character-count field
  // widths change with the version.
  int version = 0;
  Ecc ecc = options.ecc;
  for (int v = options.min_version; v <= options.max_version; ++v) {
    if (optimal_bit_cost(text, v) <=
        static_cast<std::size_t>(spec::data_codewords(ecc, v)) * 8) {
      version = v;
      break;
    }
  }
  if (version == 0) return std::nullopt;

  // Auto-boost: raise the ECC level as long as the version stays put.
  if (options.boost_ecc) {
    const Ecc best = max_ecc_for_version(version, optimal_bit_cost(text, version));
    if (static_cast<int>(best) > static_cast<int>(ecc)) ecc = best;
  }

  const std::vector<Segment> segments = make_segments(text, version);

  BitBuffer buffer;
  for (const Segment& seg : segments) {
    buffer.append(static_cast<std::uint32_t>(seg.mode), 4);
    buffer.append(static_cast<std::uint32_t>(seg.char_count),
                  spec::char_count_bits(seg.mode, version));
    buffer.reset_to_with(seg.data_bits);
  }
  const int data_bits_capacity = spec::data_codewords(ecc, version) * 8;
  if (static_cast<int>(buffer.size()) > data_bits_capacity) return std::nullopt;

  // Terminator, byte alignment, then alternating pad codewords.
  const int terminator =
      std::min(4, data_bits_capacity - static_cast<int>(buffer.size()));
  buffer.append(0, terminator);
  buffer.append(0, static_cast<int>((8 - (buffer.size() % 8)) % 8));
  for (int pad = 0xec; buffer.size() < static_cast<std::size_t>(data_bits_capacity);
       pad ^= 0xec ^ 0x11) {
    buffer.append(static_cast<std::uint32_t>(pad), 8);
  }

  const std::vector<std::uint8_t> data_codewords = to_codewords(buffer.bits());

  const std::vector<std::uint8_t> all_codewords =
      add_ecc_and_interleave(data_codewords, version, ecc);

  // Draw, then try every mask (or just the requested one).
  Matrix best_matrix;
  int best_mask = -1;
  int best_score = -1;
  const int mask_first = (options.mask >= 0) ? options.mask : 0;
  const int mask_last = (options.mask >= 0) ? options.mask : 7;
  for (int mask = mask_first; mask <= mask_last; ++mask) {
    SymbolBuilder sb(version);
    draw_function_patterns(sb, version);
    draw_codewords(sb, all_codewords);
    draw_format_bits_value(sb, ecc, mask);
    apply_mask(sb, mask);
    Matrix candidate = sb.take();
    const int score = penalty_score_impl(candidate.modules, candidate.size);
    if (best_score < 0 || score < best_score) {
      best_score = score;
      best_mask = mask;
      best_matrix = std::move(candidate);
    }
  }
  if (best_mask < 0) return std::nullopt;

  if (stats != nullptr) {
    stats->version = version;
    stats->size = best_matrix.size;
    stats->ecc = ecc;
    stats->mask = best_mask;
    stats->data_codewords = static_cast<int>(data_codewords.size());
    stats->ecc_codewords =
        spec::ecc_codewords_per_block(ecc, version) *
        spec::num_error_correction_blocks(ecc, version);
    stats->used_bytes = static_cast<int>(text.size());
    stats->capacity_bytes = byte_capacity(version, ecc);
    stats->payload_bits = optimal_bit_cost(text, version);
  }
  return best_matrix;
}

}  // namespace qr