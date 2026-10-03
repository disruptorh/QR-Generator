#ifndef QR_QR_SPEC_HPP_
#define QR_QR_SPEC_HPP_

// The single source of truth for everything ISO/IEC 18004 fixes by table:
// codeword counts, block layouts, pattern geometry, masks and the two BCH
// information blocks. Both the encoder and the decoder read from here, so a
// symbol can never be built with one set of rules and read back with another.
//
// Everything here is a pure function of its arguments: no globals, no I/O, no
// exceptions. Functions take the version (1..40) or a mask (0..7) and return
// values; callers are responsible for the range.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "qr/qr_encoder.hpp"

namespace qr {
namespace spec {

constexpr int kMinVersion = 1;
constexpr int kMaxVersion = 40;

// Module count along one side: 17 + 4 * version.
constexpr int symbol_size(int version) { return version * 4 + 17; }

// Version implied by a symbol size; 0 when the size is not a legal one.
int version_from_size(int size);

// --- Block layout ----------------------------------------------------------

// Error correction codewords per block. Index is [ecc][version]; [ecc][0] is
// padding and never used.
int ecc_codewords_per_block(Ecc ecc, int version);

// Number of error correction blocks. Index is [ecc][version].
int num_error_correction_blocks(Ecc ecc, int version);

// Codewords available for data (no ECC) in a version/ECC pair.
int data_codewords(Ecc ecc, int version);

// Total data + ECC codewords in a version.
int total_codewords(int version);

// Data-carrying modules, i.e. codewords * 8 + remainder bits. The remainder
// (0..7) is the number of unused modules at the end of the symbol.
int raw_data_modules(int version);

// Remainder bits: unused modules trailing the codeword stream in a version.
int remainder_bits(int version);

// Data codewords for each block, longest blocks first is *not* implied: the
// shorter blocks always come first, so the vector is ascending, with the final
// (ndata % blocks) entries one larger. Returned in block order.
std::vector<int> block_data_lengths(Ecc ecc, int version);

// --- Geometry --------------------------------------------------------------

// Row/column centres of the alignment patterns, ascending. Empty for v1.
// Version 1 and 2..6 return {6} / {6, 18}, etc.
std::vector<int> alignment_positions(int version);

// True for every module occupied by a function pattern, a format/version
// information block, or a separator -- i.e. every module that is never data.
// Row-major, size * size entries.
std::vector<bool> function_map(int version, int size);

// --- Masks and information blocks ------------------------------------------

// The 8 data masks. x is the column, y is the row.
bool mask_bit(int mask, int x, int y);

// 15-bit format information, BCH(15,5) with the 0x5412 mask, ready to draw.
int format_bits(Ecc ecc, int mask);

// 18-bit version information, BCH(18,6) with the 0x1F25 generator.
int version_bits(int version);

// Nearest valid 15-bit format word to `raw` (up to 3 bit errors), or -1.
int decode_format_bits(int raw);

// Reads the 15 format bits out of a symbol's two copies and returns the value,
// or -1 when both copies disagree beyond 3 bits.
int read_format_bits(const std::vector<bool>& modules, int size);

// Reads the 18 version bits, or -1 for versions below 7.
int read_version_bits(const std::vector<bool>& modules, int size);

// --- Character modes -------------------------------------------------------

// Width of the character count indicator for a mode in a version.
int char_count_bits(Mode mode, int version);

// The 45-character alphanumeric alphabet, in code order.
const char* alnum_table();

// Value of a byte in the alphanumeric alphabet, or -1.
int alnum_value(char c);

}  // namespace spec
}  // namespace qr

#endif  // QR_QR_SPEC_HPP_
