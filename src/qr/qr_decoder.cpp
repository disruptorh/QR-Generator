#include "qr/qr_decoder.hpp"

#include <algorithm>
#include <cstring>

#include "qr/qr_spec.hpp"

namespace qr {
namespace {

// ---------------------------------------------------------------------------
// GF(256) with primitive polynomial 0x11D, the field the spec's codes live in.
// Addition is XOR, so every subtraction below is the same operation.
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

std::uint8_t gf_inv(std::uint8_t a) {
  if (a == 0) return 0;
  return gf_exp()[255 - gf_log()[a]];
}

std::uint8_t gf_pow(std::uint8_t a, int n) {
  if (a == 0) return 0;
  n %= 255;
  if (n < 0) n += 255;
  // The exponent table only covers two periods, so the log-space product has to
  // be reduced before it is used as an index.
  return gf_exp()[static_cast<std::size_t>((gf_log()[a] * n) % 255)];
}

// Two coefficient orders are in play and mixing them up is the easiest mistake
// to make here, so each gets its own name and its own evaluator:
//   * codewords come out of the encoder's long division with the highest-degree
//     coefficient first, so they read left to right;
//   * the error locator, the error evaluator and their products are built by the
//     recurrence below with the constant term first, so they read right to left.
using Poly = std::vector<std::uint8_t>;

// Ascending: p[0] is the constant term.
std::uint8_t poly_eval(const Poly& p, std::uint8_t x) {
  std::uint8_t y = 0;
  for (std::size_t i = p.size(); i-- > 0;) y = gf_mul(y, x) ^ p[i];
  return y;
}

// Descending: cw[0] is the highest-degree coefficient.
std::uint8_t codeword_eval(const std::vector<std::uint8_t>& cw, std::uint8_t x) {
  std::uint8_t y = 0;
  for (std::uint8_t c : cw) y = gf_mul(y, x) ^ c;
  return y;
}

// ---------------------------------------------------------------------------
// Reed-Solomon decoding
// ---------------------------------------------------------------------------

// Berlekamp-Massey over the syndrome sequence, which is a sum of L exponentials
// and therefore satisfies a linear recurrence of order L. Returns the error
// locator Lambda(z) = prod(1 - X_k z), ascending, with lambda[0] == 1. Returns
// an empty vector when the syndromes do not describe this code at all.
Poly berlekamp_massey(const std::vector<std::uint8_t>& s) {
  Poly lambda{1};
  Poly previous{1};
  int length = 0;     // L, the current recurrence length
  int shift = 1;      // offset at which `previous` is re-applied
  std::uint8_t previous_scale = 1;  // 1 / previous_discrepancy

  for (int n = 0; n < static_cast<int>(s.size()); ++n) {
    // discrepancy = sum lambda[i] * s[n - i]
    std::uint8_t d = s[static_cast<std::size_t>(n)];
    for (int i = 1; i <= length; ++i) {
      d ^= gf_mul(lambda[static_cast<std::size_t>(i)], s[static_cast<std::size_t>(n - i)]);
    }
    if (d == 0) {
      ++shift;
      continue;
    }
    const Poly before = lambda;
    const std::uint8_t scale = gf_mul(d, previous_scale);
    if (lambda.size() < previous.size() + static_cast<std::size_t>(shift)) {
      lambda.resize(previous.size() + static_cast<std::size_t>(shift), 0);
    }
    for (std::size_t i = 0; i < previous.size(); ++i) {
      lambda[i + static_cast<std::size_t>(shift)] ^= gf_mul(scale, previous[i]);
    }
    if (2 * length <= n) {
      // The locator grew, so the one it replaced becomes the basis for the next
      // round, rescaled to keep its own discrepancy at 1.
      length = n + 1 - length;
      previous = before;
      previous_scale = gf_inv(d);
      shift = 1;
    } else {
      ++shift;
    }
  }
  lambda.resize(static_cast<std::size_t>(length + 1), 0);
  return length == 0 ? Poly{} : lambda;
}

// Formal derivative in characteristic 2: only odd-degree terms survive.
std::uint8_t lambda_derivative_eval(const Poly& lambda, std::uint8_t x) {
  std::uint8_t y = 0;
  std::uint8_t x_power = 1;  // x^(i-1) for i = 1, 3, 5, ...
  for (std::size_t i = 1; i < lambda.size(); i += 2) {
    y ^= gf_mul(lambda[i], x_power);
    x_power = gf_mul(x_power, gf_mul(x, x));
  }
  return y;
}

// Repairs damaged codewords in place. Returns how many changed, or -1 when the
// damage exceeds what the code can fix. A clean block must return 0 without
// touching anything.
int rs_correct(std::vector<std::uint8_t>& codewords, int ecc_count) {
  if (ecc_count <= 0) return 0;
  const int n = static_cast<int>(codewords.size());
  if (n <= ecc_count) return 0;

  // Syndromes of the received word; a clean codeword gives all zeros.
  std::vector<std::uint8_t> s(static_cast<std::size_t>(ecc_count), 0);
  int weight = 0;
  for (int j = 0; j < ecc_count; ++j) {
    s[static_cast<std::size_t>(j)] = codeword_eval(codewords, gf_pow(2, j));
    weight |= s[static_cast<std::size_t>(j)];
  }
  if (weight == 0) return 0;

  const Poly lambda = berlekamp_massey(s);
  if (lambda.empty()) return -1;
  const int length = static_cast<int>(lambda.size()) - 1;
  if (length <= 0 || length > ecc_count / 2) return -1;

  // Chien search: codeword index j sits at exponent n-1-j, so its locator is
  // X = a^(n-1-j) and the root to look for is X^-1.
  std::vector<int> positions;
  std::vector<std::uint8_t> locators;
  for (int j = 0; j < n; ++j) {
    const std::uint8_t x = gf_pow(2, (n - 1 - j) % 255);
    if (poly_eval(lambda, gf_inv(x)) == 0) {
      positions.push_back(j);
      locators.push_back(x);
    }
  }
  if (static_cast<int>(positions.size()) != length) return -1;

  // Error evaluator Omega(z) = (S(z) * Lambda(z)) mod z^L.
  Poly omega(static_cast<std::size_t>(length), 0);
  for (int i = 0; i < length; ++i) {
    std::uint8_t acc = 0;
    for (int j = 0; j <= i && j < static_cast<int>(lambda.size()); ++j) {
      acc ^= gf_mul(s[static_cast<std::size_t>(i - j)], lambda[static_cast<std::size_t>(j)]);
    }
    omega[static_cast<std::size_t>(i)] = acc;
  }

  // Forney: with the generator roots starting at a^0, the magnitude at a root X
  // is X * Omega(X^-1) / Lambda'(X^-1).
  for (std::size_t k = 0; k < positions.size(); ++k) {
    const std::uint8_t z = gf_inv(locators[k]);
    const std::uint8_t denominator = lambda_derivative_eval(lambda, z);
    if (denominator == 0) return -1;
    codewords[static_cast<std::size_t>(positions[k])] ^=
        gf_mul(locators[k], gf_mul(poly_eval(omega, z), gf_inv(denominator)));
  }

  // A correct decoding leaves no syndrome behind. This also rejects the
  // miscorrections Berlekamp-Massey can produce for heavy damage.
  for (int j = 0; j < ecc_count; ++j) {
    if (codeword_eval(codewords, gf_pow(2, j)) != 0) return -1;
  }
  return static_cast<int>(positions.size());
}

// ---------------------------------------------------------------------------
// Bit access
// ---------------------------------------------------------------------------

// Reads one bit of the data codewords, most significant bit first within a byte.
int bit_at(const std::vector<std::uint8_t>& data, std::size_t pos) {
  if (pos / 8 >= data.size()) return 0;
  return (data[pos / 8] >> (7 - static_cast<int>(pos % 8))) & 1;
}

class BitReader {
 public:
  BitReader(const std::vector<std::uint8_t>& data, std::size_t bits)
      : data_(data), bits_(bits) {}

  std::size_t position() const { return pos_; }
  std::size_t remaining() const { return bits_ > pos_ ? bits_ - pos_ : 0; }

  bool take(int n, int* out) {
    if (remaining() < static_cast<std::size_t>(n)) return false;
    int v = 0;
    for (int i = 0; i < n; ++i) v = (v << 1) | bit_at(data_, pos_++);
    *out = v;
    return true;
  }

 private:
  const std::vector<std::uint8_t>& data_;
  std::size_t bits_;
  std::size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Symbol reading
// ---------------------------------------------------------------------------

struct Symbol {
  int version = 0;
  int size = 0;
  Ecc ecc = Ecc::medium;
  int mask = 0;
  std::vector<std::uint8_t> codewords;
};

bool read_codewords(const Matrix& matrix, Symbol* out, DecodeError* error) {
  const int size = matrix.size;
  if (size <= 0 || static_cast<std::size_t>(size) != matrix.modules.size() / static_cast<std::size_t>(size)) {
    *error = DecodeError::not_a_symbol;
    return false;
  }
  const int version = spec::version_from_size(size);
  if (version < 1) {
    *error = DecodeError::not_a_symbol;
    return false;
  }
  out->size = size;
  out->version = version;

  const int format = spec::read_format_bits(matrix.modules, size);
  if (format < 0) {
    *error = DecodeError::format_unreadable;
    return false;
  }
  out->ecc = static_cast<Ecc>(format >> 3);
  out->mask = format & 7;

  // From version 7 up the symbol carries a redundant copy of its version, which
  // must agree with the size.
  if (version >= 7) {
    const int stated = spec::read_version_bits(matrix.modules, size);
    if (stated >= 0 && stated != version) {
      *error = DecodeError::version_mismatch;
      return false;
    }
  }

  const std::vector<bool> function = spec::function_map(version, size);
  std::vector<int> bits;
  bits.reserve(static_cast<std::size_t>(spec::raw_data_modules(version)));
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;  // the vertical timing pattern is not data
    for (int vert = 0; vert < size; ++vert) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? size - 1 - vert : vert;
        if (function[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                     static_cast<std::size_t>(x)]) {
          continue;
        }
        int bit = matrix.at(x, y) ? 1 : 0;
        if (spec::mask_bit(out->mask, x, y)) bit = !bit;
        bits.push_back(bit);
      }
    }
  }
  // Every data module has to have been visited exactly once, otherwise the
  // layout and the symbol disagree and nothing below can be trusted.
  if (static_cast<int>(bits.size()) != spec::raw_data_modules(version)) {
    *error = DecodeError::not_a_symbol;
    return false;
  }

  out->codewords.resize(bits.size() / 8);
  for (std::size_t i = 0; i + 8 <= bits.size(); i += 8) {
    std::uint8_t v = 0;
    for (int k = 0; k < 8; ++k) v = static_cast<std::uint8_t>((v << 1) | bits[i + static_cast<std::size_t>(k)]);
    out->codewords[i / 8] = v;
  }
  return true;
}

// Undoes the block interleaving and repairs every block.
bool extract_data(const Symbol& symbol, int max_corrected, std::vector<std::uint8_t>* data,
                  DecodeStats* stats) {
  const int blocks = spec::num_error_correction_blocks(symbol.ecc, symbol.version);
  const int ecc_count = spec::ecc_codewords_per_block(symbol.ecc, symbol.version);
  const std::vector<int> data_lengths = spec::block_data_lengths(symbol.ecc, symbol.version);
  const int ndata = spec::data_codewords(symbol.ecc, symbol.version);
  if (blocks <= 0 || ecc_count <= 0 ||
      data_lengths.size() != static_cast<std::size_t>(blocks)) {
    return false;
  }

  // Data codewords are interleaved column by column. All blocks are the same
  // length unless the data does not divide evenly, in which case the surplus
  // sits in the trailing blocks as one extra codeword at that column, and only
  // those blocks have a value there.
  const int surplus = ndata % blocks;
  const int common = data_lengths.front() + (surplus > 0 ? 1 : 0);

  // Each block is rebuilt at its own true length. A short block simply has no
  // codeword at the shared column, and leaving a placeholder there would hand
  // the corrector something that is not a codeword of this code.
  std::vector<std::vector<std::uint8_t>> block(static_cast<std::size_t>(blocks));
  for (int j = 0; j < blocks; ++j) {
    block[static_cast<std::size_t>(j)].resize(
        static_cast<std::size_t>(data_lengths[static_cast<std::size_t>(j)] + ecc_count), 0);
  }

  std::size_t k = 0;
  for (int i = 0; i < common; ++i) {
    for (int j = 0; j < blocks; ++j) {
      if (i == common - 1 && data_lengths[static_cast<std::size_t>(j)] < common) continue;
      block[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] = symbol.codewords[k++];
    }
  }
  for (int i = 0; i < ecc_count; ++i) {
    for (int j = 0; j < blocks; ++j) {
      const std::size_t at = static_cast<std::size_t>(data_lengths[static_cast<std::size_t>(j)]) +
                             static_cast<std::size_t>(i);
      block[static_cast<std::size_t>(j)][at] = symbol.codewords[k++];
    }
  }
  if (k != symbol.codewords.size()) return false;

  int corrected = 0;
  int corrected_blocks = 0;
  for (int j = 0; j < blocks; ++j) {
    std::vector<std::uint8_t>& cw = block[static_cast<std::size_t>(j)];
    const int fixed = rs_correct(cw, ecc_count);
    if (fixed < 0) return false;
    if (max_corrected >= 0 && fixed > max_corrected) return false;
    if (fixed > 0) {
      corrected += fixed;
      ++corrected_blocks;
    }
  }

  data->clear();
  data->reserve(static_cast<std::size_t>(ndata));
  for (int j = 0; j < blocks; ++j) {
    const std::vector<std::uint8_t>& cw = block[static_cast<std::size_t>(j)];
    const int data_len = data_lengths[static_cast<std::size_t>(j)];
    data->insert(data->end(), cw.begin(), cw.begin() + data_len);
  }

  stats->blocks = blocks;
  stats->ecc_codewords_per_block = ecc_count;
  stats->data_codewords = ndata;
  stats->corrected_codewords = corrected;
  stats->corrected_blocks = corrected_blocks;
  return true;
}

core::Result<DecodeResult> fail(DecodeError error) {
  return core::Result<DecodeResult>::err("symbol", decode_error_message(error));
}

}  // namespace

std::optional<DecodeResult> decode(const Matrix& matrix, int max_corrected) {
  const core::Result<DecodeResult> result = decode_checked(matrix, max_corrected);
  if (!result.has_value()) return std::nullopt;
  return result.value();
}

core::Result<DecodeResult> decode_checked(const Matrix& matrix, int max_corrected) {
  Symbol symbol;
  DecodeError error = DecodeError::not_a_symbol;
  if (!read_codewords(matrix, &symbol, &error)) return fail(error);

  DecodeResult out;
  out.stats.version = symbol.version;
  out.stats.size = symbol.size;
  out.stats.ecc = symbol.ecc;
  out.stats.mask = symbol.mask;

  std::vector<std::uint8_t> data;
  if (!extract_data(symbol, max_corrected, &data, &out.stats)) {
    return fail(DecodeError::ecc_unrecoverable);
  }

  const std::size_t bits = data.size() * 8;
  BitReader reader(data, bits);
  const char* alnum = spec::alnum_table();
  std::size_t stream_end = bits;

  for (;;) {
    if (reader.remaining() < 4) break;
    int mode_value = 0;
    const std::size_t before_mode = reader.position();
    reader.take(4, &mode_value);
    if (mode_value == 0) {
      // Terminator: the padding starts at these four zero bits, not after them.
      stream_end = before_mode;
      break;
    }

    Mode mode;
    switch (mode_value) {
      case 0x1: mode = Mode::numeric; break;
      case 0x2: mode = Mode::alphanumeric; break;
      case 0x4: mode = Mode::byte; break;
      default: return fail(DecodeError::bad_mode);
    }

    int count = 0;
    if (!reader.take(spec::char_count_bits(mode, symbol.version), &count)) {
      return fail(DecodeError::truncated);
    }

    DecodedSegment segment;
    segment.mode = mode;
    segment.char_count = static_cast<std::size_t>(count);
    segment.byte_offset = out.text.size();
    const std::size_t start = reader.position();

    if (mode == Mode::numeric) {
      for (int i = 0; i < count / 3; ++i) {
        int v = 0;
        if (!reader.take(10, &v) || v > 999) return fail(DecodeError::truncated);
        out.text += static_cast<char>('0' + v / 100);
        out.text += static_cast<char>('0' + (v / 10) % 10);
        out.text += static_cast<char>('0' + v % 10);
      }
      const int rest = count % 3;
      if (rest == 1) {
        int v = 0;
        if (!reader.take(4, &v) || v > 9) return fail(DecodeError::truncated);
        out.text += static_cast<char>('0' + v);
      } else if (rest == 2) {
        int v = 0;
        if (!reader.take(7, &v) || v > 99) return fail(DecodeError::truncated);
        out.text += static_cast<char>('0' + v / 10);
        out.text += static_cast<char>('0' + v % 10);
      }
    } else if (mode == Mode::alphanumeric) {
      for (int i = 0; i < count / 2; ++i) {
        int v = 0;
        if (!reader.take(11, &v) || v >= 45 * 45) return fail(DecodeError::bad_alphanumeric);
        out.text += alnum[v / 45];
        out.text += alnum[v % 45];
      }
      if (count % 2 == 1) {
        int v = 0;
        if (!reader.take(6, &v) || v > 44) return fail(DecodeError::bad_alphanumeric);
        out.text += alnum[v];
      }
    } else {
      for (int i = 0; i < count; ++i) {
        int v = 0;
        if (!reader.take(8, &v)) return fail(DecodeError::truncated);
        out.text += static_cast<char>(v);
      }
    }

    segment.bit_length = reader.position() - start;
    out.segments.push_back(segment);
  }

  // The tail of the data area has to be what the spec prescribes: a terminator
  // of up to four zero bits, zero fill to the next byte boundary, then
  // alternating 0xEC / 0x11 pad codewords. The terminator is shortened when the
  // data area has no room for it, so the zero run is either the full terminator
  // plus alignment, or simply whatever is left. Anything else means the symbol
  // did not come from a conforming encoder and is reported rather than trusted.
  {
    const std::size_t terminator = std::min<std::size_t>(4, bits - stream_end);
    const std::size_t after_terminator = stream_end + terminator;
    const std::size_t aligned = (after_terminator + 7) & ~static_cast<std::size_t>(7);
    if (aligned > bits) return fail(DecodeError::inconsistent);
    for (std::size_t p = stream_end; p < aligned; ++p) {
      if (bit_at(data, p) != 0) return fail(DecodeError::inconsistent);
    }
    bool expect_ec = true;
    for (std::size_t p = aligned; p + 8 <= bits; p += 8) {
      std::uint8_t byte = 0;
      for (int i = 0; i < 8; ++i) {
        byte = static_cast<std::uint8_t>((byte << 1) | bit_at(data, p + static_cast<std::size_t>(i)));
      }
      if (byte != (expect_ec ? 0xEC : 0x11)) return fail(DecodeError::inconsistent);
      expect_ec = !expect_ec;
    }
  }

  out.stats.bit_length = stream_end;
  out.stats.segments = static_cast<int>(out.segments.size());
  return core::Result<DecodeResult>::ok(std::move(out));
}

const char* decode_error_message(DecodeError error) {
  switch (error) {
    case DecodeError::not_a_symbol: return "La matriz no es un simbolo QR valido";
    case DecodeError::format_unreadable: return "No se pudo leer la informacion de formato";
    case DecodeError::version_mismatch: return "El bloque de version no coincide con el tamano";
    case DecodeError::truncated: return "Un segmento declara mas datos de los que caben";
    case DecodeError::bad_mode: return "Indicador de modo no valido";
    case DecodeError::bad_alphanumeric: return "Valor alfanumerico fuera del alfabeto";
    case DecodeError::ecc_unrecoverable: return "Demasiados errores de correccion";
    case DecodeError::inconsistent: return "Bits de relleno inconsistentes";
  }
  return "Error desconocido";
}

}  // namespace qr
