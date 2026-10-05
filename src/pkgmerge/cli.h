// SPDX-License-Identifier: GPL-3.0-only
//
// Command line of spec section 10.
//
// Options may appear in any order, `--` ends option parsing, and the first two
// positional arguments are the input and output directories - which is what
// makes drag-and-drop work: a dropped folder path behaves exactly like
// `--input`.

#ifndef PKG_MERGE_CLI_H
#define PKG_MERGE_CLI_H

#include <filesystem>
#include <string>
#include <vector>

#include "pkgmerge/text.h"

namespace pkgmerge {

/// The name the program calls itself. Spec 10 fixes this for the usage line, so
/// --help, --version, --json and the Windows version resource all use it rather
/// than each inventing one.
extern const char* const kProgramName;

struct Options {
  std::filesystem::path input;
  std::filesystem::path output;
  bool input_given = false;
  bool output_given = false;
  bool recursive = false;
  bool overwrite = false;
  bool no_clobber = false;
  // Verification is on unless --no-verify is given. This tool exists because a
  // silently corrupt output is the failure worth fearing, and the check costs
  // one extra read pass over what was just written.
  bool verify = true;
  bool dry_run = false;
  bool quiet = false;
  bool no_pause = false;
  // Move an existing output aside instead of destroying it when replacing.
  bool backup = false;
  // Emit the result as JSON on stdout instead of prose.
  bool json = false;
};

enum class ParseStatus {
  kRun,           // carry on and merge
  kHelp,          // print usage_text() and exit successfully
  kVersion,       // print version_text() and exit successfully
  kExitFailure,   // bad arguments: report `error` plus the usage text
};

struct ParseResult {
  ParseStatus status = ParseStatus::kRun;
  Options options;
  std::string error;
};

/// `arguments` excludes the program name.
ParseResult parse_arguments(const std::vector<OsString>& arguments);

std::string usage_text();
std::string version_text();
/// Just the version number, e.g. "1.1.0", for structured output.
std::string version_number();

}  // namespace pkgmerge

#endif  // PKG_MERGE_CLI_H
