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

/// One set's result, kept so that --json can report every set rather than only
/// the ones that printed a line.
struct SetResult {
  std::string title_id;
  std::string status;   // merged, skipped, failed or rejected
  std::uint64_t bytes = 0;
  std::size_t pieces = 0;
  std::filesystem::path output;
  std::filesystem::path backup;
  std::vector<std::string> warnings;
  std::vector<std::string> problems;
};

std::string json_array(const char* key, const std::vector<std::string>& values,
                       const std::string& indent) {
  if (values.empty()) {
    return concat(indent, "\"", key, "\": []");
  }
  std::string out = concat(indent, "\"", key, "\": [\n");
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += concat(indent, "  ", json_string(values[i]));
    out += (i + 1U < values.size()) ? ",\n" : "\n";
  }
  out += concat(indent, "]");
  return out;
}

std::string json_number(std::uint64_t value) { return std::to_string(value); }

/// The whole run as one JSON object on stdout. This is the only output --json
/// produces, so a caller can parse stdout without stripping anything.
std::string render_json(const Options& options, const std::filesystem::path& input,
                        const std::filesystem::path& output,
                        const std::vector<SetResult>& results, const std::size_t merged,
                        const std::size_t skipped, const std::size_t failed,
                        const int exit_code) {
  std::string out = "{\n";
  out += concat("  \"tool\": ", json_string("pkg-merge"), ",\n");
  out += concat("  \"version\": ", json_string(version_number()), ",\n");
  out += concat("  \"input\": ", json_string(display_path(input)), ",\n");
  out += concat("  \"output\": ", json_string(display_path(output)), ",\n");
  out += concat("  \"recursive\": ", options.recursive ? "true" : "false", ",\n");
  out += concat("  \"dryRun\": ", options.dry_run ? "true" : "false", ",\n");
  out += concat("  \"verified\": ", options.verify ? "true" : "false", ",\n");
  out += concat("  \"exitCode\": ", json_number(static_cast<std::uint64_t>(exit_code)),
                ",\n");
  out += concat("  \"counts\": {\"merged\": ", json_number(static_cast<std::uint64_t>(merged)),
                ", \"skipped\": ", json_number(static_cast<std::uint64_t>(skipped)),
                ", \"failed\": ", json_number(static_cast<std::uint64_t>(failed)), "},\n");
  out += "  \"sets\": [\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const SetResult& r = results[i];
    out += "    {\n";
    out += concat("      \"titleId\": ", json_string(r.title_id), ",\n");
    out += concat("      \"status\": ", json_string(r.status), ",\n");
    out += concat("      \"pieces\": ", json_number(static_cast<std::uint64_t>(r.pieces)),
                  ",\n");
    out += concat("      \"bytes\": ", json_number(r.bytes), ",\n");
    out += concat("      \"output\": ", json_string(display_path(r.output)), ",\n");
    if (!r.backup.empty()) {
      out += concat("      \"backup\": ", json_string(display_path(r.backup)), ",\n");
    }
    out += json_array("warnings", r.warnings, "      ");
    out += ",\n";
    out += json_array("problems", r.problems, "      ");
    out += "\n    }";
    out += (i + 1U < results.size()) ? ",\n" : "\n";
  }
  out += "  ]\n}\n";
  return out;
}

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

  // Rule 6 runs first, and completely, before anything is written for any set.
  // Doing it as a separate pass is what lets progress describe the whole scan
  // rather than one set at a time: the total of every valid set is known before
  // the first byte is copied. Nothing is written here.
  std::vector<PieceSet> sets;
  std::vector<ValidationReport> reports;
  sets.reserve(found.size());
  reports.reserve(found.size());
  std::uint64_t scan_total = 0;
  std::size_t valid_count = 0;
  for (const PieceSet& original : found) {
    PieceSet set = original;
    ValidationReport report = validate_set(&set);
    if (report.ok) {
      scan_total += report.total;
      ++valid_count;
    }
    sets.push_back(std::move(set));
    reports.push_back(std::move(report));
  }

  MergeRequest request;
  request.output_directory = output;
  request.overwrite = options.overwrite;
  request.no_clobber = options.no_clobber;
  request.verify = options.verify;
  request.dry_run = options.dry_run;
  request.quiet = options.quiet || options.json;
  request.backup = options.backup;
  request.progress_total = scan_total;
  request.set_count = valid_count;

  // Progress is suppressed for --json because it shares stdout with the JSON:
  // a caller parsing the result cannot have a progress bar in the middle of it.
  if (!options.quiet && !options.json) {
    info(concat(options.dry_run ? "Would merge " : "Merging ", format_count(valid_count),
                " set(s) from ", display_path(input), " into ", display_path(output),
                options.recursive ? " (recursive)" : ""));
  }

  std::size_t merged = 0;
  std::size_t skipped = 0;
  std::size_t failed = 0;
  bool cancelled = false;
  std::vector<SetResult> results;
  results.reserve(sets.size());
  // Bytes already accounted for by sets that have finished, so the progress
  // line can show the scan total rather than this set's.
  std::uint64_t written = 0;
  std::size_t attempted = 0;

  for (std::size_t index = 0; index < sets.size(); ++index) {
    PieceSet& set = sets[index];
    const ValidationReport& report = reports[index];

    SetResult record;
    record.title_id = set.title_id;
    record.pieces = set.pieces.size();
    record.warnings = report.warnings;
    record.problems = report.problems;

    if (!options.json) {
      for (const std::string& message : report.warnings) {
        warn(concat(set.title_id, ": ", message));
      }
    }
    if (!report.ok) {
      if (!options.json) {
        for (const std::string& message : report.problems) {
          complain(concat(set.title_id, ": ", message));
        }
        if (!options.quiet) {
          info(concat("  ", set.title_id, ": rejected, its ", format_count(set.pieces.size()),
                      " piece(s) are not a complete set"));
        }
      }
      ++failed;
      record.status = "rejected";
      record.bytes = report.total;
      results.push_back(std::move(record));
      continue;
    }

    request.progress_base = written;
    request.set_index = attempted++;

    const MergeOutcome outcome = merge_set(set, request);
    record.output = outcome.output;
    record.backup = outcome.backup;
    record.bytes = outcome.bytes;
    switch (outcome.status) {
      case MergeStatus::kMerged:
        ++merged;
        record.status = "merged";
        written += outcome.bytes;
        if (!options.quiet && !options.json) {
          info(concat("  ", set.title_id, ": ", options.dry_run ? "would write " : "wrote ",
                      format_bytes(outcome.bytes), " from ", format_count(set.pieces.size()),
                      " piece(s) to ", display_path(outcome.output)));
          if (!outcome.backup.empty()) {
            info(concat("  ", set.title_id, ": previous output kept as ",
                        display_path(outcome.backup)));
          }
        }
        break;
      case MergeStatus::kSkipped:
        ++skipped;
        record.status = "skipped";
        record.bytes = 0;
        if (!options.json) {
          if (!options.quiet) {
            info(concat("  ", set.title_id, ": kept the existing file; ", outcome.note));
          } else {
            warn(concat(set.title_id, ": ", outcome.note));
          }
        }
        break;
      case MergeStatus::kCancelled:
        cancelled = true;
        record.status = "cancelled";
        break;
      case MergeStatus::kFailed:
        ++failed;
        record.status = "failed";
        record.bytes = 0;
        if (!options.json) {
          complain(concat(set.title_id, ": ", outcome.note));
        }
        break;
    }
    results.push_back(std::move(record));
    if (cancelled) {
      break;
    }
  }

  // Spec section 13.
  int exit_code = kExitOk;
  if (cancelled) {
    exit_code = kExitCancelled;
  } else if (failed > 0) {
    exit_code = merged > 0 ? kExitPartial : kExitNothingMerged;
  }

  if (options.json) {
    term::write_out(render_json(options, input, output, results, merged, skipped, failed,
                                exit_code));
    return exit_code;
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
  return exit_code;
}

}  // namespace pkgmerge
