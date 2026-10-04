// SPDX-License-Identifier: GPL-3.0-only
//
// Checking a set (spec section 6) and merging it (spec sections 7, 8 and 9).
//
// Two properties are load bearing here:
//
//  * Nothing is published until the whole set has been written and its length
//    checked. The result goes to a temporary file in the destination directory
//    and is renamed into place afterwards, so no failure, cancellation or
//    interruption can leave a partial file under the output name
//    (spec section 7.1).
//  * Sizes and offsets are std::uint64_t everywhere, and no piece is ever held
//    in memory: a fixed block is moved at a time, so a 32-bit build merges a set
//    larger than its own address space (spec sections 8 and 17.6).

#ifndef PKG_MERGE_MERGE_H
#define PKG_MERGE_MERGE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pkgmerge/scan.h"

namespace pkgmerge {

/// The size of one streaming block. Fixed, so memory use does not depend on the
/// size of the pieces (spec section 8).
inline constexpr std::size_t kCopyBlockSize = 1024U * 1024U;

struct ValidationReport {
  bool ok = false;
  std::uint64_t total = 0;             // sum of the piece sizes
  std::vector<std::string> problems;   // each one names the file or number
  std::vector<std::string> warnings;   // rule 6.5, which never fails a merge
};

/// Applies every rule of spec section 6 and measures each piece, which is where
/// `Piece::size` comes from. Nothing is written. A problem aborts this set and
/// no other.
ValidationReport validate_set(PieceSet* set);

enum class MergeStatus {
  kMerged,     // the output is in place
  kSkipped,    // an existing output file was kept (spec section 9)
  kFailed,     // nothing was published; `note` says why
  kCancelled,  // cancelled on request (spec section 11)
};

struct MergeRequest {
  std::filesystem::path output_directory;
  bool overwrite = false;
  bool no_clobber = false;
  bool verify = false;
  bool dry_run = false;
  bool quiet = false;
};

struct MergeOutcome {
  MergeStatus status = MergeStatus::kFailed;
  std::string note;
  std::uint64_t bytes = 0;
  std::filesystem::path output;
};

/// `<output directory>/<TITLE_ID>-merged.pkg` (spec section 7).
std::filesystem::path output_path_for(const PieceSet& set,
                                      const std::filesystem::path& output_directory);

/// Merges one already-validated set. `set` must be the same object that
/// validate_set() measured.
MergeOutcome merge_set(const PieceSet& set, const MergeRequest& request);

}  // namespace pkgmerge

#endif  // PKG_MERGE_MERGE_H
