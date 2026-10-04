// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/text.h"

#include <array>

namespace pkgmerge {
namespace {

constexpr std::array<const char*, 5> kByteUnits{"B", "KiB", "MiB", "GiB", "TiB"};

}  // namespace

std::string path_to_utf8(const std::filesystem::path& p) {
#ifdef _WIN32
  // std::filesystem::path::u8string() converts losslessly with the native
  // (UTF-16) encoding and is independent of the active code page.
  const std::u8string utf8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
  // POSIX paths are byte strings already in UTF-8 on every system this ships
  // to; converting them again could only corrupt them.
  return p.string();
#endif
}

std::string display_path(const std::filesystem::path& p) {
  // display_path() is called from diagnostics, so it must not throw. A path
  // that cannot be rendered (only ever a bad_alloc, or an invalid native
  // encoding) still has to be nameable in the message.
  try {
    return path_to_utf8(p);
  } catch (...) {
    return "<path unavailable>";
  }
}

std::filesystem::path path_from_utf8(const std::string_view utf8) {
#ifdef _WIN32
  // The native encoding is UTF-16, so the bytes are widened as UTF-8 rather
  // than reinterpreted through whatever code page happens to be active.
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
  // POSIX paths are byte strings; the bytes are already what the file system
  // expects.
  return std::filesystem::path(std::string(utf8));
#endif
}

char ascii_upper(char c) noexcept {
  if (c >= 'a' && c <= 'z') {
    return static_cast<char>(c - ('a' - 'A'));
  }
  return c;
}

std::string to_upper_ascii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = ascii_upper(c);
  }
  return out;
}

std::string to_lower_ascii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - ('A' - 'a'));
    }
  }
  return out;
}

bool iequals_ascii(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (ascii_upper(a[i]) != ascii_upper(b[i])) {
      return false;
    }
  }
  return true;
}

bool ends_with_iequals_ascii(std::string_view text, std::string_view suffix) {
  if (suffix.size() > text.size()) {
    return false;
  }
  return iequals_ascii(text.substr(text.size() - suffix.size()), suffix);
}

std::string format_bytes(std::uint64_t bytes) {
  if (bytes < 1024ULL) {
    return concat(bytes, " B");
  }
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < kByteUnits.size()) {
    value /= 1024.0;
    ++unit;
  }
  std::ostringstream out;
  out.precision(2);
  out.flags(std::ios::fixed);
  out << value << ' ' << kByteUnits[unit];
  return out.str();
}

std::string format_duration(double seconds) {
  if (!(seconds >= 0.0) || seconds > 359999.0) {  // also catches NaN
    return "unknown";
  }
  const std::uint64_t total = static_cast<std::uint64_t>(seconds);
  const std::uint64_t hours = total / 3600ULL;
  const std::uint64_t minutes = (total % 3600ULL) / 60ULL;
  const std::uint64_t secs = total % 60ULL;
  if (hours > 0ULL) {
    return concat(hours, "h ", (minutes < 10ULL ? "0" : ""), minutes, "m");
  }
  if (minutes > 0ULL) {
    return concat(minutes, "m ", (secs < 10ULL ? "0" : ""), secs, "s");
  }
  return concat(secs, "s");
}

std::string format_count(std::uint64_t n) { return concat(n); }

std::string_view trim_ascii(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
    --last;
  }
  return text.substr(first, last - first);
}

}  // namespace pkgmerge