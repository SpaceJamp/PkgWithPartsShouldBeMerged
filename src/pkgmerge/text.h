// SPDX-License-Identifier: GPL-3.0-only
//
// Small helpers shared by the rest of the program: lossless UTF-8 rendering of
// paths (spec section 14), ASCII case handling (spec sections 4.1 and 10) and
// human-readable formatting.
//
// text.h has no platform #ifdefs apart from OsString, which differs because
// command-line arguments arrive as UTF-16 on Windows and as UTF-8 on POSIX.

#ifndef PKG_MERGE_TEXT_H
#define PKG_MERGE_TEXT_H

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace pkgmerge {

// The native character type of a path / command-line argument.
#ifdef _WIN32
using OsString = std::wstring;
#else
using OsString = std::string;
#endif

// Stringifies a narrow literal for use as an OsString constant.
#ifdef _WIN32
#define PKG_LIT(literal) L##literal
#else
#define PKG_LIT(literal) literal
#endif

/// UTF-8 rendering of a path, without loss on any target (spec section 14).
/// Never throws: a path that cannot be converted is returned byte for byte.
std::string path_to_utf8(const std::filesystem::path& p);

/// UTF-8 rendering of a path for display. Same as path_to_utf8(), but a path
/// that is not valid in its own encoding degrades to a best-effort string
/// instead of being dropped, so a diagnostic never loses the file it names.
std::string display_path(const std::filesystem::path& p);

/// The other direction: a path built from UTF-8 text, losslessly on every
/// target. Used for the names this program composes itself, such as the merged
/// output file (spec section 7).
std::filesystem::path path_from_utf8(std::string_view utf8);

/// ASCII upper/lower case and case-insensitive comparison. Only ASCII is
/// folded: piece names are matched case-insensitively (spec section 4.1), and
/// leaving non-ASCII bytes alone keeps the mapping byte-for-byte reversible.
std::string to_upper_ascii(std::string_view text);
std::string to_lower_ascii(std::string_view text);
bool iequals_ascii(std::string_view a, std::string_view b);
bool ends_with_iequals_ascii(std::string_view text, std::string_view suffix);
char ascii_upper(char c) noexcept;

/// "512 B", "4.2 MiB", "3.00 GiB", "1.5 TiB".
std::string format_bytes(std::uint64_t bytes);
/// "0s", "42s", "1m 05s", "2h 07m" - for estimated time remaining.
std::string format_duration(double seconds);
/// A short integer for counts.
std::string format_count(std::uint64_t n);

/// Concatenation helper, so messages read as one expression.
template <class... Parts>
std::string concat(Parts&&... parts) {
  std::ostringstream out;
  (out << ... << std::forward<Parts>(parts));
  return out.str();
}

/// Removes leading and trailing ASCII whitespace.
std::string_view trim_ascii(std::string_view text);

}  // namespace pkgmerge

#endif  // PKG_MERGE_TEXT_H