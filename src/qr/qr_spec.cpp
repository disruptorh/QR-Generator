#include "qr/qr_spec.hpp"

#include <algorithm>
#include <cstdlib>

namespace qr {
namespace spec {
namespace {

constexpr char kAlnumChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// [ecc][version] tables straight out of ISO/IEC 18004 tables 13-22. The [ecc][0]
// entries are padding so that the version index starts at 1.
constexpr int kEccCodewordsPerBlock[4][kMaxVersion + 1] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30,
     28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30, 30},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28,
     26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
     28, 28, 28, 28, 28},
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28,
     28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30, 30, 30, 30},
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28,
     28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30, 30, 30, 30},
};

constexpr int kNumErrorCorrectionBlocks[4][kMaxVersion + 1] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9,
     10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17,
     17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47,
     49},
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
     23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62,
     65, 68},
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
     25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74,
     77, 81},
};

// Remainder bits per version, written as the ranges the spec groups them in.
int remainder_bits_for(int version) {
  if (version <= 0) return -1;
  if (version == 1) return 0;
  if (version <= 6) return 7;
  if (version <= 13) return 0;
  if (version <= 20) return 3;
  if (version <= 27) return 4;
  if (version <= 34) return 3;
  return 0;
}

bool in_range(int version) { return version >= kMinVersion && version <= kMaxVersion; }

int popcount15(int v) {
  int n = 0;
  for (int i = 0; i < 15; ++i) n += (v >> i) & 1;
  return n;
}

}  // namespace

int version_from_size(int size) {
  if (size < 21 || size > 177 || (size - 17) % 4 != 0) return 0;
  return (size - 17) / 4;
}

int ecc_codewords_per_block(Ecc ecc, int version) {
  if (!in_range(version)) return 0;
  return kEccCodewordsPerBlock[static_cast<int>(ecc)][version];
}

int num_error_correction_blocks(Ecc ecc, int version) {
  if (!in_range(version)) return 0;
  return kNumErrorCorrectionBlocks[static_cast<int>(ecc)][version];
}

int raw_data_modules(int version) {
  int result = (16 * version + 128) * version + 64;
  if (version >= 2) {
    const int num_align = version / 7 + 2;
    result -= (25 * num_align - 10) * num_align - 55;
    if (version >= 7) result -= 36;
  }
  return result;
}

int total_codewords(int version) { return raw_data_modules(version) / 8; }

int remainder_bits(int version) { return remainder_bits_for(version); }

int data_codewords(Ecc ecc, int version) {
  if (!in_range(version)) return 0;
  return total_codewords(version) - ecc_codewords_per_block(ecc, version) *
                                       num_error_correction_blocks(ecc, version);
}

std::vector<int> block_data_lengths(Ecc ecc, int version) {
  std::vector<int> lengths;
  const int blocks = num_error_correction_blocks(ecc, version);
  const int ndata = data_codewords(ecc, version);
  if (blocks <= 0 || ndata <= 0) return lengths;
  const int short_len = ndata / blocks;
  const int num_long = ndata % blocks;
  const int num_short = num_long > 0 ? blocks - num_long : 0;
  lengths.reserve(static_cast<std::size_t>(blocks));
  for (int i = 0; i < blocks; ++i) {
    lengths.push_back(short_len + ((num_long > 0 && i >= num_short) ? 1 : 0));
  }
  return lengths;
}

std::vector<int> alignment_positions(int version) {
  if (version <= 1) return {};
  const int num_align = version / 7 + 2;
  const int step =
      (version == 32) ? 26 : (version * 4 + num_align * 2 + 1) / (num_align * 2 - 2) * 2;
  std::vector<int> result;
  result.reserve(static_cast<std::size_t>(num_align));
  // The spec derives these by walking down from the last position; reversing
  // that walk gives ascending order, which the corner-collision checks assume.
  for (int pos = version * 4 + 17 - 7; static_cast<int>(result.size()) < num_align - 1;
       pos -= step) {
    result.push_back(pos);
  }
  std::reverse(result.begin(), result.end());
  result.insert(result.begin(), 6);
  return result;
}

std::vector<bool> function_map(int version, int size) {
  std::vector<bool> f(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), false);
  if (size <= 0) return f;
  auto set = [&](int x, int y) {
    if (x >= 0 && x < size && y >= 0 && y < size) {
      f[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
        static_cast<std::size_t>(x)] = true;
    }
  };

  // Timing patterns.
  for (int i = 0; i < size; ++i) {
    set(6, i);
    set(i, 6);
  }
  // Finder patterns with their separators: the 8x8 corner blocks.
  auto finder = [&](int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) set(cx + dx, cy + dy);
  };
  finder(3, 3);
  finder(size - 4, 3);
  finder(3, size - 4);

  const std::vector<int> align = alignment_positions(version);
  for (std::size_t i = 0; i < align.size(); ++i) {
    for (std::size_t j = 0; j < align.size(); ++j) {
      const bool corner = (i == 0 && j == 0) ||
                          (i == 0 && j + 1 == align.size()) ||
                          (i + 1 == align.size() && j == 0);
      if (corner) continue;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) set(align[j] + dx, align[i] + dy);
    }
  }

  // Format information: both copies, plus the always-dark module.
  for (int i = 0; i <= 8; ++i) set(8, i);
  for (int i = 9; i < 15; ++i) set(14 - i, 8);  // x = 5..0
  set(7, 8);
  for (int i = 0; i < 8; ++i) set(size - 1 - i, 8);
  for (int i = 8; i < 15; ++i) set(8, size - 15 + i);
  set(8, size - 8);  // the always-dark module

  // Version information.
  if (version >= 7) {
    for (int i = 0; i < 18; ++i) {
      const int a = size - 11 + i % 3;
      const int b = i / 3;
      set(a, b);
      set(b, a);
    }
  }
  return f;
}

bool mask_bit(int mask, int x, int y) {
  switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (y / 2 + x / 3) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    default: return false;
  }
}

int format_bits(Ecc ecc, int mask) {
  static const int kEccBits[4] = {1, 0, 3, 2};  // L, M, Q, H as the spec numbers them
  const int data = (kEccBits[static_cast<int>(ecc)] << 3) | (mask & 7);
  int rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  return ((data << 10) | rem) ^ 0x5412;
}

int version_bits(int version) {
  int rem = version;
  for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1f25);
  return (version << 12) | rem;
}

// Brute force over the 32 real (ecc, mask) words rather than recomputing the
// BCH parity for an arbitrary input: the code and the 0x5412 mask both change
// the bit pattern, so there is nothing to invert.
int decode_format_bits(int raw) {
  raw &= 0x7fff;
  int best = -1;
  int best_dist = 99;
  for (int ecc_index = 0; ecc_index < 4; ++ecc_index) {
    for (int mask = 0; mask < 8; ++mask) {
      const int dist = popcount15(format_bits(static_cast<Ecc>(ecc_index), mask) ^ raw);
      if (dist < best_dist) {
        best_dist = dist;
        best = (ecc_index << 3) | mask;
      }
    }
  }
  return best_dist <= 3 ? best : -1;
}

int read_format_bits(const std::vector<bool>& modules, int size) {
  auto at = [&](int x, int y) {
    return modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                   static_cast<std::size_t>(x)]
               ? 1
               : 0;
  };
  // The dark module at (8, size-8) carries no information.
  int a = 0;
  for (int i = 0; i <= 5; ++i) a |= at(8, i) << i;
  a |= at(8, 7) << 6;
  a |= at(8, 8) << 7;
  a |= at(7, 8) << 8;
  for (int i = 9; i < 15; ++i) a |= at(14 - i, 8) << i;
  int b = 0;
  for (int i = 0; i < 8; ++i) b |= at(size - 1 - i, 8) << i;
  for (int i = 8; i < 15; ++i) b |= at(8, size - 15 + i) << i;

  const int da = decode_format_bits(a);
  const int db = decode_format_bits(b);
  if (da < 0 && db < 0) return -1;
  if (da < 0) return db;
  if (db < 0) return da;
  return da == db ? da : -1;  // the two copies must agree
}

int read_version_bits(const std::vector<bool>& modules, int size) {
  if (size < 45) return -1;  // no version block below version 7
  auto at = [&](int x, int y) {
    return modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                   static_cast<std::size_t>(x)]
               ? 1
               : 0;
  };
  int raw = 0;
  for (int i = 0; i < 18; ++i) {
    const int a = size - 11 + i % 3;
    const int b = i / 3;
    if (at(a, b) != at(b, a)) return -1;
    raw |= at(a, b) << i;
  }
  int best = 0;
  int best_dist = 99;
  for (int candidate = 7; candidate <= kMaxVersion; ++candidate) {
    int dist = 0;
    const int word = version_bits(candidate);
    for (int i = 0; i < 18; ++i) dist += ((word >> i) & 1) ^ ((raw >> i) & 1);
    if (dist < best_dist) {
      best_dist = dist;
      best = candidate;
    }
  }
  return best_dist <= 3 ? best : -1;
}

int char_count_bits(Mode mode, int version) {
  const int tier = (version <= 9) ? 0 : (version <= 26) ? 1 : 2;
  switch (mode) {
    case Mode::numeric: return 10 + tier * 2;  // 10, 12, 14
    case Mode::alphanumeric: return 9 + tier * 2;  // 9, 11, 13
    case Mode::byte: return 8 + (tier == 0 ? 0 : 8);  // 8, 16, 16
  }
  return 8;
}

const char* alnum_table() { return kAlnumChars; }

int alnum_value(char c) {
  for (int i = 0; i < 45; ++i) {
    if (kAlnumChars[i] == c) return i;
  }
  return -1;
}

}  // namespace spec
}  // namespace qr
