#ifndef QR_QR_DECODER_HPP_
#define QR_QR_DECODER_HPP_

// Reads a finished symbol back into text. This is the counterpart to
// qr::encode() and exists so the application can verify what it just produced,
// without shipping or trusting an external decoding library. It shares the spec
// tables in qr_spec.hpp with the encoder, but shares none of the encoder's
// placement, masking or segmentation code -- it walks the symbol the way a
// scanner would, so agreement between the two is real evidence.
//
// Decoding is total: a value or nullopt, never an exception.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "qr/qr_encoder.hpp"

namespace qr {

// One decoded run, as it was written into the symbol.
struct DecodedSegment {
  Mode mode = Mode::byte;
  std::size_t char_count = 0;   // characters represented, not encoded bits
  std::size_t bit_length = 0;   // bits the run occupies, count field included
  std::size_t byte_offset = 0;  // where the run's text starts in the output
};

struct DecodeStats {
  int version = 0;
  int size = 0;
  Ecc ecc = Ecc::medium;
  int mask = 0;
  int blocks = 0;
  int ecc_codewords_per_block = 0;
  int data_codewords = 0;
  int corrected_codewords = 0;  // codewords repaired by Reed-Solomon
  int corrected_blocks = 0;     // blocks that needed at least one repair
  int version_bits_corrected = 0;
  int format_bits_corrected = 0;
  std::size_t bit_length = 0;   // bits the segments occupy
  int segments = 0;
};

struct DecodeResult {
  std::string text;                    // UTF-8 as the bytes were stored
  std::vector<DecodedSegment> segments;
  DecodeStats stats;
};

enum class DecodeError {
  not_a_symbol,        // size is not 21..177 in steps of 4
  format_unreadable,   // both format copies are more than 3 bits from any word
  version_mismatch,    // version block disagrees with the symbol size
  truncated,           // a segment claims more data than the symbol holds
  bad_mode,            // a mode indicator outside {1,2,4}
  bad_alphanumeric,    // a value outside the 45-character alphabet
  ecc_unrecoverable,   // too many errors in a block to correct
  inconsistent,        // parity bits that no valid padding could produce
};

// Decodes `matrix`. `max_corrected` caps how many codewords per block may be
// repaired: 0 makes the check strict (any damaged codeword fails the decode),
// which is what the application uses to confirm a freshly encoded symbol. Pass
// a negative value for the spec's own tolerance.
std::optional<DecodeResult> decode(const Matrix& matrix, int max_corrected = -1);

// Same, reporting why it failed.
core::Result<DecodeResult> decode_checked(const Matrix& matrix, int max_corrected = -1);

const char* decode_error_message(DecodeError error);

}  // namespace qr

#endif  // QR_QR_DECODER_HPP_
