// SPDX-License-Identifier: GPL-3.0-only
//
// Finding and grouping the piece files (spec sections 4, 4.1, 4.2 and 5).
//
// A name is a piece file only when it matches, case-insensitively,
// `^(.+)_(\d+)\.pkg$`. Everything else in the scan scope is ignored silently -
// including the merged files this program produced earlier, which end in
// `-merged.pkg` and therefore do not match.

#ifndef PKG_MERGE_SCAN_H
#define PKG_MERGE_SCAN_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pkgmerge {

struct Piece {
  std::filesystem::path path;
  std::string name;       // UTF-8 file name, for messages
  std::uint64_t number = 0;
  std::uint64_t size = 0;  // filled in by validation
};

struct PieceSet {
  std::string title_id;  // ASCII upper-case identifier (spec sections 4.1 and 7)
  std::vector<Piece> pieces;  // ascending by piece number (spec section 5)
  std::filesystem::path directory;  // where the pieces were found
};

/// Splits a file name into its identifier and piece number. False when the
/// name is not a piece file at all (spec sections 4 and 4.2).
///
/// The identifier is returned as it is written; the caller upper-cases it. A
/// piece number too large for std::uint64_t makes the name not a piece file,
/// rather than wrapping or clamping it.
bool parse_piece_name(std::string_view file_name, std::string* identifier,
                      std::uint64_t* number);

/// Walks `root` (and its sub-directories when `recursive`) and returns the sets
/// found, each with its pieces in ascending numeric order and the sets
/// themselves ordered by title identifier.
///
/// Directories that cannot be listed are skipped with a warning rather than
/// aborting the scan (spec section 12).
std::vector<PieceSet> scan_for_sets(const std::filesystem::path& root, bool recursive,
                                    std::vector<std::string>* warnings);

/// Total of the piece sizes, or false when the sum would overflow 64 bits.
bool total_size(const std::vector<Piece>& pieces, std::uint64_t* total);

}  // namespace pkgmerge

#endif  // PKG_MERGE_SCAN_H
