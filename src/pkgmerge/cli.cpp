// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/cli.h"

#include <optional>

#ifndef PKG_MERGE_VERSION
#define PKG_MERGE_VERSION "0.0.0"
#endif

// The commit this was built from, so a bug report says which code was running.
// CMake leaves this as "unknown" when git is not available at configure time.
#ifndef PKG_MERGE_GIT
#define PKG_MERGE_GIT "unknown"
#endif

namespace pkgmerge {

const char* const kProgramName = "PkgWithPartsShouldBeMerged";

namespace {

/// An argument as lower-case ASCII, or nothing when it is not plain ASCII. A
/// non-ASCII argument can therefore never be mistaken for an option, which
/// matters for paths that happen to contain dashes.
std::optional<std::string> as_ascii(const OsString& argument) {
  std::string narrowed;
  narrowed.reserve(argument.size());
  for (const OsString::value_type c : argument) {
    // One expression for both targets: a signed char sign-extends here, an
    // unsigned wchar_t simply compares as the value it already is.
    if (static_cast<unsigned long>(c) > 0x7FUL) {
      return std::nullopt;
    }
    narrowed.push_back(static_cast<char>(c));
  }
  return to_lower_ascii(narrowed);
}

ParseResult failure(const std::string& message) {
  ParseResult result;
  result.status = ParseStatus::kExitFailure;
  result.error = message;
  return result;
}

ParseResult success(const ParseStatus status) {
  ParseResult result;
  result.status = status;
  return result;
}

/// The options that stand alone. Written out rather than derived from a table so
/// that adding one cannot silently leave it out of this list.
bool is_flag(const std::string& name) {
  return name == "-r" || name == "--recursive" || name == "-f" || name == "--overwrite" ||
         name == "--no-clobber" || name == "--verify" || name == "--no-verify" ||
         name == "-n" || name == "--dry-run" || name == "-q" || name == "--quiet" ||
         name == "--no-pause" || name == "--backup" || name == "--json" || name == "-h" ||
         name == "--help" || name == "-v" || name == "--version";
}

bool is_input_option(const std::string& name) { return name == "-i" || name == "--input"; }
bool is_output_option(const std::string& name) { return name == "-o" || name == "--output"; }
bool is_value_option(const std::string& name) { return is_input_option(name) || is_output_option(name); }

bool looks_like_option(const std::optional<std::string>& name) {
  return name.has_value() && name->size() >= 2 && name->front() == '-';
}

}  // namespace

std::string usage_text() {
  return concat(kProgramName, "\n",
                "Merges the numbered pieces of a split PS4 PKG back into a single\n",
                "PKG.\n",
                "\n",
                "Usage:\n",
                "  ", kProgramName, " [OPTIONS] [INPUT_DIR [OUTPUT_DIR]]\n",
                "\n",
                "Options:\n",
                "  -i, --input DIR    Folder holding the <TITLE_ID>_<N>.pkg pieces.\n",
                "                     When omitted, a folder dialog is used if one is\n",
                "                     available.\n",
                "  -o, --output DIR   Where the merged file is written. Defaults to the\n",
                "                     input folder.\n",
                "  -r, --recursive    Also look in sub-directories of the input folder.\n",
                "  -f, --overwrite    Replace an existing output file without asking.\n",
                "      --no-clobber   Never replace an existing output file; skip those\n",
                "                     sets and report the skip.\n",
                "      --backup       When replacing, move the previous output aside\n",
                "                     instead of destroying it.\n",
                "      --verify       Re-read the merged result and compare it with the\n",
                "                     sources, byte for byte. This is the default;\n",
                "                     --no-verify skips the extra read pass.\n",
                "      --no-verify    Do not re-read and compare the result.\n",
                "  -n, --dry-run      Report what would be merged; write nothing.\n",
                "  -q, --quiet        Print only warnings and errors.\n",
                "      --json         Print the result as JSON on standard output.\n",
                "      --no-pause     Do not wait for a key press before exiting.\n",
                "  -h, --help         Print this text and exit.\n",
                "  -v, --version      Print the version and exit.\n",
                "\n",
                "The result is named <TITLE_ID>-merged.pkg, with the title identifier in\n",
                "upper case. Pieces are concatenated in ascending piece-number order, so\n",
                "piece 10 follows piece 9. Options may appear in any order, and \"--\" ends\n",
                "option parsing.\n");
}

std::string version_number() { return std::string(PKG_MERGE_VERSION); }

std::string version_text() {
  return concat(kProgramName, " ", PKG_MERGE_VERSION, " (", PKG_MERGE_GIT, ")\n",
                "Merges split PS4 PKG pieces back into a single PKG.\n");
}

ParseResult parse_arguments(const std::vector<OsString>& arguments) {
  ParseResult result;
  Options& options = result.options;

  bool options_ended = false;
  std::size_t positional = 0;

  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const OsString& argument = arguments[index];

    if (!options_ended && argument == PKG_LIT("--")) {
      options_ended = true;
      continue;
    }

    const std::optional<std::string> name =
        options_ended ? std::nullopt : as_ascii(argument);

    if (!looks_like_option(name)) {
      // A positional fills the first of input/output that is still unset, rather
      // than blindly being the first one. So "prog IN OUT" sets both, while
      // "prog -i IN OUT" sets IN as the input and OUT as the output folder, which
      // is what someone typing that means. A folder dropped on the executable
      // arrives here and behaves exactly like --input.
      if (positional == 0 && !options.input_given) {
        options.input = std::filesystem::path(argument);
        options.input_given = true;
      } else if (positional <= 1 && !options.output_given) {
        options.output = std::filesystem::path(argument);
        options.output_given = true;
      } else {
        return failure(concat("unexpected extra argument '",
                              display_path(std::filesystem::path(argument)),
                              "'; at most an input folder and an output folder are accepted"));
      }
      ++positional;
      continue;
    }

    if (is_flag(*name)) {
      const std::string& flag = *name;
      if (flag == "-r" || flag == "--recursive") {
        options.recursive = true;
      } else if (flag == "-f" || flag == "--overwrite") {
        options.overwrite = true;
      } else if (flag == "--no-clobber") {
        options.no_clobber = true;
      } else if (flag == "--verify") {
        options.verify = true;
      } else if (flag == "--no-verify") {
        options.verify = false;
      } else if (flag == "--backup") {
        options.backup = true;
      } else if (flag == "--json") {
        options.json = true;
      } else if (flag == "-n" || flag == "--dry-run") {
        options.dry_run = true;
      } else if (flag == "-q" || flag == "--quiet") {
        options.quiet = true;
      } else if (flag == "--no-pause") {
        options.no_pause = true;
      } else if (flag == "-h" || flag == "--help") {
        return success(ParseStatus::kHelp);
      } else {
        return success(ParseStatus::kVersion);  // -v / --version
      }
      continue;
    }

    // `--input=DIR` and `--output=DIR` are accepted too. The `=` is only treated
    // as a separator when the text before it is a known long option, so a
    // folder whose name contains `=` is still just a folder.
    if (name->rfind("--", 0) == 0) {
      const std::size_t equals = name->find('=');
      if (equals != std::string::npos) {
        const std::string option = name->substr(0, equals);
        if (is_value_option(option)) {
          const OsString inline_value = argument.substr(equals + 1);
          if (inline_value.empty()) {
            return failure(concat("the ", option, " option needs a folder"));
          }
          if (is_input_option(option)) {
            options.input = std::filesystem::path(inline_value);
            options.input_given = true;
          } else {
            options.output = std::filesystem::path(inline_value);
            options.output_given = true;
          }
          continue;
        }
      }
    }

    if (is_value_option(*name)) {
      // The next argument is the value, whatever it looks like: nothing is
      // guessed away, and a missing value is the only failure here.
      if (index + 1 >= arguments.size() || arguments[index + 1].empty()) {
        return failure(concat("the ", *name, " option needs a folder"));
      }
      const OsString& value = arguments[++index];
      if (is_input_option(*name)) {
        options.input = std::filesystem::path(value);
        options.input_given = true;
      } else {
        options.output = std::filesystem::path(value);
        options.output_given = true;
      }
      continue;
    }

    return failure(concat("unknown option '", *name, "'"));
  }

  if (options.overwrite && options.no_clobber) {
    return failure("--overwrite and --no-clobber contradict each other; give only one of them");
  }

  return result;
}

}  // namespace pkgmerge
