#ifndef QR_QR_ENCODER_HPP_
#define QR_QR_ENCODER_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qr {

// Error correction level, ordered from weakest to strongest.
enum class Ecc {
  low = 0,
  medium = 1,
  quartile = 2,
  high = 3,
};

const char* ecc_label(Ecc ecc);

// ---------------------------------------------------------------------------
// Segments
// ---------------------------------------------------------------------------

enum class Mode {
  numeric = 0x1,
  alphanumeric = 0x2,
  byte = 0x4,
};

// One run of data encoded in a single mode. `data_bits` holds the *encoded* bit
// stream of the run (not the raw characters), and `char_count` is the number of
// source characters it represents -- that is what goes into the character count
// indicator, which is unrelated to the encoded bit length.
struct Segment {
  Mode mode = Mode::byte;
  std::vector<std::uint8_t> data_bits;
  std::size_t char_count = 0;

  std::size_t bit_length() const { return data_bits.size(); }
};

// Splits `text` into runs of numeric / alphanumeric / byte segments, choosing
// the split with the fewest total bits for `version` (character-count field
// widths depend on the version). Uses a shortest-path over the string, so a
// short digit run inside a URL never inflates the payload. Never fails: any
// byte string is representable in byte mode.
//
// Byte cost of the optimal split at `version`; exposed for tests and for the
// version search.
std::size_t optimal_bit_cost(const std::string& text, int version);

// Never fails: returns an empty list for an empty string.
std::vector<Segment> make_segments(const std::string& text, int version);

// True when the whole string fits in alphanumeric mode.
bool is_alphanumeric(const std::string& text);

// ---------------------------------------------------------------------------
// Symbol
// ---------------------------------------------------------------------------

// A finished symbol: one bool per module, row-major.
struct Matrix {
  int size = 0;
  std::vector<bool> modules;

  bool at(int x, int y) const {
    return modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                   static_cast<std::size_t>(x)];
  }
  void set(int x, int y, bool dark) {
    modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
            static_cast<std::size_t>(x)] = dark;
  }
};

struct EncodeOptions {
  Ecc ecc = Ecc::medium;
  bool boost_ecc = true;             // raise ECC while the version stays put
  int mask = -1;                     // -1 = automatic, else 0..7
  int min_version = 1;               // 1..40
  int max_version = 40;              // 1..40
};

struct EncodeStats {
  int version = 0;
  int size = 0;              // modules per side
  Ecc ecc = Ecc::medium;
  int mask = 0;
  int data_codewords = 0;    // total data codewords in the symbol
  int used_bytes = 0;        // payload bytes actually encoded
  int capacity_bytes = 0;    // bytes the selected version+ECC could carry
  int ecc_codewords = 0;     // error correction codewords in the symbol
  int payload_bits = 0;      // bits the segmented payload actually occupies
};

// Highest ECC level that still fits in `version` for the given bit count.
Ecc max_ecc_for_version(int version, std::size_t bit_length);

// Byte-mode capacity (UTF-8 bytes) of a version/ECC pair.
int byte_capacity(int version, Ecc ecc);

// Encodes `text`, optimizing the segmentation for the smallest symbol. Returns
// nullopt when the data does not fit in any version within the requested range,
// or when the options are self-contradictory (bad version range or mask). This
// is the only failure mode: a value, never an exception.
std::optional<Matrix> encode(const std::string& text,
                             const EncodeOptions& options,
                             EncodeStats* stats);

// Exposed for the mask-choice heuristic and for tests.
int penalty_score(const Matrix& matrix);

}  // namespace qr

#endif  // QR_QR_ENCODER_HPP_