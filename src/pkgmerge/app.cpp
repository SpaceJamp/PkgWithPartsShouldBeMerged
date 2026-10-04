// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/app.h"

#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "pkgmerge/cli.h"
#include "pkgmerge/fileio.h"
#include "pkgmerge/merge.h"
#include "pkgmerge/platform.h"
#include "pkgmerge/scan.h"
#include "pkgmerge/term.h"
#include "pkgmerge/text.h"

namespace pkgmerge {
namespace {

// Informational output goes to standard output; warnings and errors go to
// standard error, which is what keeps them out of the way of a redirected log
// and what --quiet leaves in place (spec sections 10 and 14).
void info(const std::string& text) { term::write_out_line(text); }
void warn(const std::string& text) { term::write_err_line(concat("warning: ", text)); }
void complain(const std::string& text) { term::write_err_line(concat("error: ", text)); }

/// True when `path` is a directory this process can look at.
bool is_usable_directory(const std::filesystem::path& path, std::string* reason) {
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (ec) {
    *reason = concat(display_path(path), ": ", ec.message());
    return false;
  }
  if (!std::filesystem::exists(status)) {
    *reason = concat(display_path(path), " does not exist");
    return false;
  }
  if (!std::filesystem::is_directory(status)) {
    *reason = concat(display_path(path), " is not a folder");
    return false;
  }
  return true;
}

/// Asks for a folder when none was given (spec section 10). Never waits on a
/// dialog that cannot appear: `unavailable` means exactly that.
bool obtain_input_folder(std::filesystem::path* input) {
  std::error_code ec;
  std::filesystem::path start = std::filesystem::current_path(ec);
  if (ec) {
    start.clear();
  }

  const platform::FolderChoice choice = platform::choose_input_folder(start);
  if (choice.chosen) {
    *input = choice.path;
    return true;
  }
  if (choice.unavailable) {
    complain(concat("no input folder was given and no folder dialog is available (",
                    choice.detail, "); give one with --input DIR"));
  } else {
    complain("no input folder was given or chosen; give one with --input DIR");
  }
  return false;
}

}  // namespace

int run(const std::vector<OsString>& arguments) {
  term::init();
  platform::install_signal_handlers();

  const ParseResult parsed = parse_arguments(arguments);
  switch (parsed.status) {
    case ParseStatus::kHelp:
      term::write_out(usage_text());
      return kExitOk;
    case ParseStatus::kVersion:
      term::write_out(version_text());
      return kExitOk;
    case ParseStatus::kExitFailure:
      complain(parsed.error);
      term::write_out(usage_text());
      return kExitNothingMerged;
    case ParseStatus::kRun:
      break;
  }

  const Options options = parsed.options;
  term::set_pause_enabled(!options.no_pause);

  std::filesystem::path input = options.input;
  if (!options.input_given && !obtain_input_folder(&input)) {
    return kExitNothingMerged;
  }

  std::string reason;
  if (!is_usable_directory(input, &reason)) {
    complain(concat("cannot use the input folder ", reason));
    return kExitNothingMerged;
  }

  // Spec section 7: with no output folder the result goes beside the pieces.
  std::filesystem::path output = options.output_given ? options.output : input;
  if (options.output_given && !is_usable_directory(output, &reason)) {
    complain(concat("cannot use the output folder ", reason));
    return kExitNothingMerged;
  }

  std::vector<std::string> scan_warnings;
  const std::vector<PieceSet> found =
      scan_for_sets(input, options.recursive, &scan_warnings);
  for (const std::string& message : scan_warnings) {
    warn(message);
  }

  if (found.empty()) {
    complain(concat("no <TITLE_ID>_<N>.pkg pieces were found in ", display_path(input),
                    options.recursive ? "" : "; only the folder itself was searched, so try --recursive",
                    " if the pieces are in sub-folders"));
    return kExitNothingMerged;
  }

  MergeRequest request;
  request.output_directory = output;
  request.overwrite = options.overwrite;
  request.no_clobber = options.no_clobber;
  request.verify = options.verify;
  request.dry_run = options.dry_run;
  request.quiet = options.quiet;

  if (!options.quiet) {
    info(concat(options.dry_run ? "Would merge " : "Merging ", format_count(found.size()),
                " set(s) from ", display_path(input), " into ", display_path(output),
                options.recursive ? " (recursive)" : ""));
  }

  std::size_t merged = 0;
  std::size_t skipped = 0;
  std::size_t failed = 0;
  bool cancelled = false;

  // Each set is taken by value: validate_set() records the size it measured on
  // every piece, and `found` is const, so the copy is what isolates that mutation
  // from the scan results. It is one copy per set, not per piece.
  for (PieceSet set : found) {
    // Rule 6 runs first, and completely: nothing is written for a set that does
    // not pass, while every other set carries on (spec sections 6 and 12).
    const ValidationReport report = validate_set(&set);
    for (const std::string& message : report.warnings) {
      warn(concat(set.title_id, ": ", message));
    }
    if (!report.ok) {
      for (const std::string& message : report.problems) {
        complain(concat(set.title_id, ": ", message));
      }
      if (!options.quiet) {
        info(concat("  ", set.title_id, ": rejected, its ", format_count(set.pieces.size()),
                    " piece(s) are not a complete set"));
      }
      ++failed;
      continue;
    }

    const MergeOutcome outcome = merge_set(set, request);
    switch (outcome.status) {
      case MergeStatus::kMerged:
        ++merged;
        if (!options.quiet) {
          info(concat("  ", set.title_id, ": ", options.dry_run ? "would write " : "wrote ",
                      format_bytes(outcome.bytes), " from ", format_count(set.pieces.size()),
                      " piece(s) to ", display_path(outcome.output)));
        }
        break;
      case MergeStatus::kSkipped:
        ++skipped;
        if (!options.quiet) {
          info(concat("  ", set.title_id, ": kept the existing file; ", outcome.note));
        } else {
          warn(concat(set.title_id, ": ", outcome.note));
        }
        break;
      case MergeStatus::kCancelled:
        cancelled = true;
        break;
      case MergeStatus::kFailed:
        ++failed;
        complain(concat(set.title_id, ": ", outcome.note));
        break;
    }
    if (cancelled) {
      break;
    }
  }

  if (cancelled) {
    complain("cancelled; no output file was left behind");
    return kExitCancelled;
  }

  if (!options.quiet) {
    info(concat("Summary: ", format_count(merged), " merged, ", format_count(skipped),
                " skipped, ", format_count(failed), " failed"));
    if (merged > 0 && failed == 0) {
      info(options.dry_run ? "Dry run finished; nothing was written." : "Done.");
    }
  }

  // Spec section 13.
  if (failed > 0) {
    return merged > 0 ? kExitPartial : kExitNothingMerged;
  }
  return kExitOk;
}

}  // namespace pkgmerge
