// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/scan.h"

#include <algorithm>
#include <limits>
#include <map>
#include <system_error>
#include <utility>

#include "pkgmerge/text.h"

namespace pkgmerge {
namespace {

constexpr std::string_view kExtension = ".pkg";

bool is_digit(const char c) { return c >= '0' && c <= '9'; }

/// True when the entry is a regular file, following a symbolic link but not a
/// broken one. `status` is asked with an error_code so a link whose target has
/// gone away is simply "not a piece file" rather than an exception.
bool is_usable_regular_file(const std::filesystem::directory_entry& entry) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(entry.symlink_status(ec)) && !ec) {
    return true;
  }
  ec.clear();
  const bool followed = std::filesystem::is_regular_file(entry.status(ec));
  return followed && !ec;
}

/// Reads one directory. Sub-directories are pushed onto `pending` when recursion
/// is on; `symlink_status` decides that, so a link pointing back at an ancestor
/// is never walked and the traversal always terminates.
void read_directory(const std::filesystem::path& directory, const bool recursive,
                    std::vector<std::filesystem::path>* pending,
                    std::vector<Piece>* pieces, std::vector<std::string>* warnings) {
  std::error_code ec;
  std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::none, ec);
  if (ec) {
    warnings->push_back(concat("cannot read ", display_path(directory), ", skipping it: ",
                               ec.message()));
    return;
  }

  const std::filesystem::directory_iterator end;
  while (it != end) {
    const std::filesystem::directory_entry entry = *it;

    std::error_code link_error;
    const std::filesystem::file_status link_status = entry.symlink_status(link_error);
    if (!link_error && std::filesystem::is_directory(link_status)) {
      if (recursive) {
        pending->push_back(entry.path());
      }
    } else if (!link_error && is_usable_regular_file(entry)) {
      std::string identifier;
      std::uint64_t number = 0;
      std::string name = path_to_utf8(entry.path().filename());
      if (parse_piece_name(name, &identifier, &number)) {
        Piece piece;
        piece.path = entry.path();
        piece.name = std::move(name);
        piece.number = number;
        pieces->push_back(std::move(piece));
      }
      // Anything that is not a piece file is ignored without a word
      // (spec section 4).
    }

    it.increment(ec);
    if (ec) {
      warnings->push_back(concat("cannot read the rest of ", display_path(directory),
                                 ", skipping it: ", ec.message()));
      return;
    }
  }
}

}  // namespace

bool parse_piece_name(const std::string_view file_name, std::string* identifier,
                      std::uint64_t* number) {
  if (file_name.size() <= kExtension.size() ||
      !ends_with_iequals_ascii(file_name, kExtension)) {
    return false;
  }
  const std::string_view stem = file_name.substr(0, file_name.size() - kExtension.size());

  // `^(.+)_(\d+)\.pkg$`: the identifier is everything before the last underscore
  // that is followed by digits, and it must not be empty - `_0.pkg` is not a
  // piece file (spec section 4.2). Leading zeros are accepted, because the
  // regular expression accepts them; they then collide with their own number
  // and are reported by rule 6.3.
  std::size_t digits_begin = stem.size();
  while (digits_begin > 0 && is_digit(stem[digits_begin - 1])) {
    --digits_begin;
  }
  // Three ways to fail: no digits at all (`X_.pkg`), no room for an identifier
  // (`_0.pkg`), or no underscore in front of the digits (`X0.pkg`).
  const bool has_digits = digits_begin < stem.size();
  const bool has_identifier = digits_begin >= 2;
  if (!has_digits || !has_identifier || stem[digits_begin - 1] != '_') {
    return false;
  }

  // Decimal digits only, and never wrapping: a number too large to represent
  // makes the name not a piece file at all (spec section 4.2).
  std::uint64_t value = 0;
  const std::uint64_t limit = (std::numeric_limits<std::uint64_t>::max)();
  for (std::size_t index = digits_begin; index < stem.size(); ++index) {
    const std::uint64_t digit = static_cast<std::uint64_t>(stem[index] - '0');
    if (value > (limit - digit) / 10ULL) {
      return false;
    }
    value = value * 10ULL + digit;
  }

  if (identifier != nullptr) {
    identifier->assign(stem.substr(0, digits_begin - 1));
  }
  if (number != nullptr) {
    *number = value;
  }
  return true;
}

bool total_size(const std::vector<Piece>& pieces, std::uint64_t* total) {
  const std::uint64_t limit = (std::numeric_limits<std::uint64_t>::max)();
  std::uint64_t running = 0;
  for (const Piece& piece : pieces) {
    if (piece.size > limit - running) {
      return false;
    }
    running += piece.size;
  }
  *total = running;
  return true;
}

std::vector<PieceSet> scan_for_sets(const std::filesystem::path& root, const bool recursive,
                                    std::vector<std::string>* warnings) {
  std::vector<Piece> pieces;
  std::vector<std::filesystem::path> pending;
  pending.push_back(root);
  while (!pending.empty()) {
    const std::filesystem::path directory = pending.back();
    pending.pop_back();
    read_directory(directory, recursive, &pending, &pieces, warnings);
  }

  // One sort, then the pieces of a set are already consecutive and already in
  // the order spec section 5 requires: ascending piece number, so 10 follows 9
  // and never comes before 2.
  std::sort(pieces.begin(), pieces.end(), [](const Piece& left, const Piece& right) {
    if (left.number != right.number) {
      return left.number < right.number;
    }
    return left.path < right.path;
  });

  // Grouping key is the upper-cased identifier (spec section 4.1), which is also
  // what the merged file is named from (spec section 7). std::map keeps the
  // sets in a stable order, so two runs over the same folder print the same
  // thing.
  std::map<std::string, PieceSet> grouped;
  for (Piece& piece : pieces) {
    std::string identifier;
    std::uint64_t number = 0;
    if (!parse_piece_name(piece.name, &identifier, &number)) {
      continue;  // unreachable: only piece files were collected
    }
    const std::string title_id = to_upper_ascii(identifier);
    PieceSet& set = grouped[title_id];
    set.title_id = title_id;
    if (set.pieces.empty()) {
      set.directory = piece.path.parent_path();
    }
    set.pieces.push_back(std::move(piece));
  }

  std::vector<PieceSet> sets;
  sets.reserve(grouped.size());
  for (std::map<std::string, PieceSet>::value_type& entry : grouped) {
    sets.push_back(std::move(entry.second));
  }
  return sets;
}

}  // namespace pkgmerge
