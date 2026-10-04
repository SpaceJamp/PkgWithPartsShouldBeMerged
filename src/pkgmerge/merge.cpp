// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/merge.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "pkgmerge/fileio.h"
#include "pkgmerge/platform.h"
#include "pkgmerge/term.h"
#include "pkgmerge/text.h"

namespace pkgmerge {
namespace {

// The container magic rule 6.5 looks for. Alternative container variants exist,
// which is exactly why this rule can only warn.
constexpr unsigned char kContainerMagic[4] = {0x7FU, 0x43U, 0x4EU, 0x54U};

/// How often the progress line is rewritten (spec section 11: at most roughly
/// ten times a second).
constexpr std::chrono::milliseconds kProgressInterval(100);

/// The name the missing member of a set would have had. Reported for rules 6.1
/// and 6.2, both of which must name it.
std::string expected_piece_name(const PieceSet& set, const std::uint64_t number) {
  return concat(set.title_id, "_", number, ".pkg");
}

bool add_size(std::uint64_t* total, const std::uint64_t value) {
  if (value > (std::numeric_limits<std::uint64_t>::max)() - *total) {
    return false;
  }
  *total += value;
  return true;
}

/// Reads the first four bytes of a piece. False when they cannot be read, in
/// which case rule 6.4 has already reported the file.
bool has_container_magic(const Piece& piece) {
  InputFile probe;
  std::string error;
  if (!probe.open(piece.path, &error)) {
    return false;
  }
  unsigned char head[sizeof(kContainerMagic)] = {};
  const std::int64_t got = probe.read(head, sizeof(head), &error);
  if (got != static_cast<std::int64_t>(sizeof(head))) {
    return false;
  }
  return std::equal(std::begin(kContainerMagic), std::end(kContainerMagic), std::begin(head));
}

std::string progress_line(const PieceSet& set, const std::uint64_t done,
                          const std::uint64_t total, const double seconds) {
  const double done_value = static_cast<double>(done);
  const double total_value = static_cast<double>(total);
  const double percent = total > 0U ? (100.0 * done_value / total_value) : 100.0;
  const double rate = seconds > 0.0 ? done_value / seconds : 0.0;

  std::string line = concat("  ", set.title_id, "  ");
  if (percent < 10.0) {
    line += ' ';
  }
  if (percent < 100.0) {
    line += ' ';
  }
  line += concat(static_cast<long long>(percent + 0.5), "%  ", format_bytes(done), " / ",
                 format_bytes(total));
  if (rate > 0.0) {
    line += concat("  ", format_bytes(static_cast<std::uint64_t>(rate)), "/s");
    const std::uint64_t left = (done < total) ? (total - done) : 0U;
    line += concat("  ", format_duration(static_cast<double>(left) / rate), " left");
  }
  return line;
}

/// Rate-limited progress, so a fast local merge cannot flood a redirected log
/// (spec section 11).
class Progress {
 public:
  Progress(const bool enabled, const PieceSet& set, const std::uint64_t total)
      : enabled_(enabled), set_(set), total_(total) {}

  void advance(const std::uint64_t done, const bool force) {
    if (!enabled_) {
      return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!force && now - last_ < kProgressInterval) {
      return;
    }
    last_ = now;
    if (!begun_) {
      begun_ = true;
      started_ = now;
    }
    const double seconds =
        std::chrono::duration<double>(now - started_).count();
    term::progress_update(progress_line(set_, done, total_, seconds));
  }

  void finish() {
    if (!enabled_) {
      return;
    }
    advance(total_, true);
    term::progress_end();
  }

  /// Ends the line without claiming the merge got anywhere: used when the merge
  /// stops early, so no 100% is ever shown for a merge that did not finish.
  void stop() {
    if (enabled_) {
      term::progress_end();
    }
  }

 private:
  bool enabled_ = false;
  const PieceSet& set_;
  std::uint64_t total_ = 0;
  std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
  bool begun_ = false;
};

/// A temporary name in the destination directory, beside the output file so the
/// final step is a rename within one directory (spec section 7.1).
std::filesystem::path temporary_path_for(const std::filesystem::path& output,
                                         const unsigned attempt) {
  std::string name = path_to_utf8(output.filename());
  name += ".pkgmerge-tmp";
  if (attempt > 0U) {
    name += concat("-", attempt);
  }
  return output.parent_path() / path_from_utf8(name);
}

/// Creates a fresh temporary file. The name is never one that already exists,
/// so nothing belonging to the user can be clobbered.
bool create_temporary(TempFile* temporary, const std::filesystem::path& output,
                      std::string* error) {
  std::string first_failure;
  for (unsigned attempt = 0; attempt < 64U; ++attempt) {
    const std::filesystem::path candidate = temporary_path_for(output, attempt);
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      continue;  // a leftover from an earlier run: take the next name
    }
    if (temporary->create(candidate, error)) {
      return true;
    }
    if (first_failure.empty()) {
      first_failure = *error;
    }
  }
  *error = first_failure.empty()
               ? concat("cannot create a temporary file next to ", display_path(output))
               : first_failure;
  return false;
}

/// Rule 9: may an existing output file be replaced?
enum class Overwrite {
  kProceed,
  kKeep,
};

Overwrite decide_overwrite(const std::filesystem::path& output, const MergeRequest& request,
                           std::string* note) {
  std::error_code ec;
  if (!std::filesystem::exists(output, ec)) {
    return Overwrite::kProceed;
  }
  if (request.no_clobber) {
    *note = concat("already exists and --no-clobber was given, so it was kept (",
                   display_path(output), ")");
    return Overwrite::kKeep;
  }
  if (request.overwrite) {
    return Overwrite::kProceed;
  }
  // "if a terminal is interactive": both ends have to be a terminal, or the
  // question could not be seen and could not be answered.
  if (term::stdin_is_terminal() && term::stdout_is_terminal()) {
    const std::string question =
        concat(display_path(output), " already exists. Replace it?");
    if (term::confirm_yes(question)) {
      return Overwrite::kProceed;
    }
    *note = concat("already exists and the answer was no, so it was kept (", display_path(output),
                   ")");
    return Overwrite::kKeep;
  }
  *note = concat("already exists and there is no terminal to ask on, so it was kept (",
                 display_path(output), ")");
  return Overwrite::kKeep;
}

/// Re-reads the finished result and compares it with the sources block by block
/// (spec section 10, --verify). Bounded memory: two fixed blocks, whatever the
/// size of the files.
bool verify_against_sources(const std::filesystem::path& produced, const PieceSet& set,
                            std::string* problem) {
  InputFile result;
  std::string error;
  if (!result.open(produced, &error)) {
    *problem = error;
    return false;
  }

  std::vector<char> result_block(kCopyBlockSize);
  std::vector<char> source_block(kCopyBlockSize);
  std::uint64_t offset = 0;

  for (const Piece& piece : set.pieces) {
    InputFile source;
    if (!source.open(piece.path, &error)) {
      *problem = error;
      return false;
    }
    // The source is walked exactly as far as it is long. Reading a whole block
    // from each side without this would compare the tail of one piece against
    // the head of the next.
    std::uint64_t piece_left = source.size();
    while (piece_left > 0U) {
      const std::size_t want = static_cast<std::size_t>(
          (piece_left < static_cast<std::uint64_t>(kCopyBlockSize))
              ? piece_left
              : static_cast<std::uint64_t>(kCopyBlockSize));
      const std::int64_t got_result = result.read(result_block.data(), want, &error);
      if (got_result < 0) {
        *problem = error;
        return false;
      }
      const std::int64_t got_source = source.read(source_block.data(), want, &error);
      if (got_source < 0) {
        *problem = error;
        return false;
      }
      if (got_result != got_source) {
        *problem = concat("the merged file differs from ", piece.name, " at byte ", offset);
        return false;
      }
      if (got_result == 0) {
        break;  // the piece shrank; the length check below will catch it
      }
      const std::size_t length = static_cast<std::size_t>(got_result);
      if (std::memcmp(result_block.data(), source_block.data(), length) != 0) {
        // Name the first byte that differs: a bare "differs" is not actionable.
        std::size_t index = 0;
        while (index < length && result_block[index] == source_block[index]) {
          ++index;
        }
        *problem = concat("the merged file differs from ", piece.name, " at byte ",
                          offset + index);
        return false;
      }
      offset += static_cast<std::uint64_t>(length);
      piece_left -= static_cast<std::uint64_t>(length);
    }
  }

  // The result must not be longer than the pieces put together.
  const std::int64_t extra = result.read(result_block.data(), 1, &error);
  if (extra < 0) {
    *problem = error;
    return false;
  }
  if (extra > 0) {
    *problem = concat("the merged file is longer than the ", format_count(set.pieces.size()),
                      " pieces put together");
    return false;
  }
  return true;
}

MergeOutcome failed(const std::string& note) {
  MergeOutcome outcome;
  outcome.status = MergeStatus::kFailed;
  outcome.note = note;
  return outcome;
}

}  // namespace

std::filesystem::path output_path_for(const PieceSet& set,
                                      const std::filesystem::path& output_directory) {
  return output_directory / path_from_utf8(concat(set.title_id, "-merged.pkg"));
}

ValidationReport validate_set(PieceSet* set) {
  ValidationReport report;
  std::vector<Piece>& pieces = set->pieces;
  if (pieces.empty()) {
    report.problems.push_back("the set has no pieces");
    return report;
  }

  // The pieces arrive sorted by number; sorting again keeps this function
  // correct on its own rather than depending on the caller.
  std::sort(pieces.begin(), pieces.end(), [](const Piece& left, const Piece& right) {
    if (left.number != right.number) {
      return left.number < right.number;
    }
    return left.name < right.name;
  });

  // Rule 6.1: a root piece must be there.
  const bool has_root = pieces.front().number == 0U;
  if (!has_root) {
    report.problems.push_back(
        concat("no root piece; expected ", expected_piece_name(*set, 0U)));
  }

  // Rule 6.3 is checked before rule 6.2 so that a set whose numbers were
  // shifted by a duplicate is reported as the duplicate it is, rather than as a
  // gap that was never really there.
  bool duplicate_found = false;
  for (std::size_t index = 1; index < pieces.size(); ++index) {
    if (pieces[index].number == pieces[index - 1].number) {
      duplicate_found = true;
      report.problems.push_back(concat("piece number ", pieces[index].number,
                                       " appears twice: ", pieces[index - 1].name, " and ",
                                       pieces[index].name));
    }
  }

  // Rule 6.2: an unbroken run 1..N with no gaps. Without a root the anchor is
  // missing, and a duplicate has already been reported, so in both cases the
  // run check would only add noise.
  if (has_root && !duplicate_found) {
    std::uint64_t expected = 1U;
    for (const Piece& piece : pieces) {
      if (piece.number == 0U) {
        continue;
      }
      if (piece.number != expected) {
        report.problems.push_back(
            concat("piece number ", expected, " is missing; expected ",
                   expected_piece_name(*set, expected)));
        break;  // the first gap is what matters
      }
      if (expected == (std::numeric_limits<std::uint64_t>::max)()) {
        break;
      }
      ++expected;
    }
  }

  // Rules 6.4 and 6.5.
  for (Piece& piece : pieces) {
    InputFile probe;
    std::string error;
    if (!probe.open(piece.path, &error)) {
      report.problems.push_back(error);
      piece.size = 0U;
      continue;
    }
    piece.size = probe.size();
    probe.close();
    if (piece.size == 0U) {
      report.problems.push_back(concat(piece.name, " is empty"));
      continue;
    }
    if (piece.number == 0U && !has_container_magic(piece)) {
      // Warning only: it must never prevent a merge (spec section 6.5).
      report.warnings.push_back(concat(piece.name,
                                       " does not begin with the usual container marker; "
                                       "merging it anyway"));
    }
  }

  std::uint64_t total = 0;
  if (!total_size(pieces, &total)) {
    report.problems.push_back("the pieces are too large to be addressed together");
    return report;
  }
  report.total = total;
  report.ok = report.problems.empty();
  return report;
}

MergeOutcome merge_set(const PieceSet& set, const MergeRequest& request) {
  MergeOutcome outcome;
  outcome.output = output_path_for(set, request.output_directory);

  std::string note;
  if (decide_overwrite(outcome.output, request, &note) == Overwrite::kKeep) {
    outcome.status = MergeStatus::kSkipped;
    outcome.note = std::move(note);
    return outcome;
  }

  std::uint64_t expected_total = 0;
  if (!total_size(set.pieces, &expected_total)) {
    return failed(concat("the pieces of ", set.title_id, " are too large to be addressed"));
  }
  outcome.bytes = expected_total;

  if (request.dry_run) {
    // Nothing is written, so the free-space question of spec section 12 does not
    // arise either.
    outcome.status = MergeStatus::kMerged;
    return outcome;
  }

  const std::optional<std::uint64_t> free_space =
      platform::available_space(request.output_directory);
  if (free_space.has_value() && *free_space < expected_total) {
    return failed(concat("not enough free space in ", display_path(request.output_directory),
                         ": the pieces need ", format_bytes(expected_total), " and only ",
                         format_bytes(*free_space), " are available"));
  }

  TempFile temporary;
  std::string error;
  if (!create_temporary(&temporary, outcome.output, &error)) {
    return failed(error);
  }
  // From here on every exit path discards the temporary file, so nothing can be
  // left behind (spec sections 7.1 and 11).
  platform::set_active_temp_file(temporary.path());

  const auto abandon = [&outcome](const std::string& why) {
    outcome.status = MergeStatus::kFailed;
    outcome.note = why;
  };

  Progress progress(!request.quiet, set, expected_total);
  std::vector<char> buffer(kCopyBlockSize);
  std::uint64_t total_copied = 0;      // across the whole merge
  std::uint64_t sizes_at_open = 0;     // what the pieces claimed when opened
  bool cancelled = false;
  bool ok = true;

  progress.advance(0, true);
  for (const Piece& piece : set.pieces) {
    if (platform::interrupt_requested() || platform::escape_pressed()) {
      cancelled = true;
      ok = false;
      break;
    }

    InputFile input;
    if (!input.open(piece.path, &error)) {
      abandon(error);
      ok = false;
      break;
    }
    const std::uint64_t piece_size = input.size();
    if (!add_size(&sizes_at_open, piece_size)) {
      abandon(concat("the pieces of ", set.title_id, " are too large to be addressed"));
      ok = false;
      break;
    }
    if (piece_size != piece.size) {
      term::write_err_line(concat("warning: ", piece.name, " changed size since it was checked; ",
                                  "merging its current contents"));
    }

    // Per-piece counter. Deliberately not the running total: reusing that as a
    // per-file offset is how a piece silently loses all but its first byte.
    std::uint64_t piece_copied = 0;
    for (;;) {
      if (platform::interrupt_requested() || platform::escape_pressed()) {
        cancelled = true;
        ok = false;
        break;
      }
      const std::int64_t got = input.read(buffer.data(), kCopyBlockSize, &error);
      if (got < 0) {
        abandon(error);
        ok = false;
        break;
      }
      if (got == 0) {
        break;
      }
      const std::size_t length = static_cast<std::size_t>(got);
      if (!temporary.write_all(buffer.data(), length, &error)) {
        abandon(concat("cannot write ", display_path(temporary.path()), ": ", error));
        ok = false;
        break;
      }
      const std::uint64_t step = static_cast<std::uint64_t>(got);
      piece_copied += step;
      total_copied += step;
      progress.advance(total_copied, false);
    }
    if (!ok) {
      break;
    }
    if (piece_copied != piece_size) {
      abandon(concat(piece.name, " changed while it was being merged: read ", piece_copied,
                     " of ", piece_size, " bytes"));
      ok = false;
      break;
    }
  }

  if (cancelled) {
    progress.stop();
    temporary.discard();
    platform::clear_active_temp_file();
    outcome.status = MergeStatus::kCancelled;
    outcome.note = "cancelled";
    return outcome;
  }
  if (!ok) {
    progress.stop();
    temporary.discard();
    platform::clear_active_temp_file();
    if (outcome.status != MergeStatus::kFailed) {
      abandon("the merge failed");
    }
    return outcome;
  }
  // The progress line is not finished here: the length check and --verify below
  // can still fail, and Progress::stop() exists so that no 100% is ever shown for
  // a merge that did not finish.

  // Spec section 7.2: verify the length before the file is published.
  std::uint64_t measured = 0;
  if (!temporary.measured_size(&measured, &error)) {
    progress.stop();
    temporary.discard();
    platform::clear_active_temp_file();
    return failed(error);
  }
  if (measured != sizes_at_open) {
    progress.stop();
    temporary.discard();
    platform::clear_active_temp_file();
    return failed(concat("the merged file came out at ", format_bytes(measured),
                         " but the pieces total ", format_bytes(sizes_at_open),
                         ", so it was discarded"));
  }

  // --verify runs before the rename, so a result that does not match its
  // sources never becomes the visible output file.
  if (request.verify) {
    std::string problem;
    if (!verify_against_sources(temporary.path(), set, &problem)) {
      progress.stop();
      temporary.discard();
      platform::clear_active_temp_file();
      return failed(concat(problem, "; the result was discarded"));
    }
  }

  // The temporary file is opened with FILE_SHARE_DELETE so that the console
  // close handler can remove it (spec section 7.1). The price of that is that
  // something else could take the name away, so confirm the name still holds the
  // bytes that were just measured before publishing it: only a file we measured
  // may be renamed into place.
  std::uint64_t on_disk = 0;
  if (!file_size(temporary.path(), &on_disk, &error) || on_disk != measured) {
    progress.stop();
    temporary.discard();
    platform::clear_active_temp_file();
    return failed(error.empty()
                      ? concat("the temporary file changed before it could be published; ",
                               display_path(temporary.path()), " was discarded")
                      : error);
  }

  // Everything that could still reject the result has now passed, so the merge is
  // genuinely finished and the progress line may say so.
  progress.finish();

  if (!replace_file(temporary.path(), outcome.output, &error)) {
    temporary.discard();
    platform::clear_active_temp_file();
    return failed(error);
  }  temporary.release();
  platform::clear_active_temp_file();

  outcome.status = MergeStatus::kMerged;
  outcome.bytes = measured;
  outcome.note.clear();
  return outcome;
}

}  // namespace pkgmerge
