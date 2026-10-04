// SPDX-License-Identifier: GPL-3.0-only
//
// Entry point.
//
// On Windows the entry point is wmain, so the arguments arrive as the UTF-16 the
// user actually typed. Going through main/argv instead would decode them with
// whatever the active code page happens to be, which is what mangles a folder
// name in a message (spec section 14). On POSIX the arguments are already the
// bytes the file system uses.

#include <exception>
#include <string>
#include <vector>

#include "pkgmerge/app.h"
#include "pkgmerge/term.h"
#include "pkgmerge/text.h"

namespace {

int guarded(const std::vector<pkgmerge::OsString>& arguments) noexcept {
  try {
    return pkgmerge::run(arguments);
  } catch (const std::exception& failure) {
    pkgmerge::term::write_err_line(
        pkgmerge::concat("error: an internal error stopped the program: ", failure.what()));
  } catch (...) {
    // Spec section 12: the program must never terminate through an exception
    // that reaches the runtime. Report it and leave with a failure code.
    pkgmerge::term::write_err_line("error: an internal error stopped the program");
  }
  return pkgmerge::kExitNothingMerged;
}

}  // namespace

#ifdef _WIN32

int wmain(int argc, wchar_t** argv) {
  static_cast<void>(argc);  // the arguments come from argv, which is UTF-16 here
  std::vector<pkgmerge::OsString> arguments;
  arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
  for (int index = 1; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }
  return guarded(arguments);
}

#else

int main(int argc, char** argv) {
  std::vector<pkgmerge::OsString> arguments;
  arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
  for (int index = 1; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }
  return guarded(arguments);
}

#endif
