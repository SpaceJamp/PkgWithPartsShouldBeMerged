// SPDX-License-Identifier: GPL-3.0-only
//
// All console output goes through this module.
//
// Rationale (spec section 14): on Windows a path is UTF-16, so a message is
// built as UTF-8 (path_to_utf8) and then written either with WriteConsoleW -
// which takes the characters as they are and cannot mangle them whatever the
// active code page is - or, when the stream is redirected to a file or a pipe,
// as UTF-8 bytes. Nothing is ever written through std::cout / printf, so the
// two paths cannot interleave badly and there is exactly one place where the
// encoding is decided.
//
// Progress lines (spec section 11) are rewritten in place on an interactive
// terminal and emitted as discrete lines otherwise.

#ifndef PKG_MERGE_TERM_H
#define PKG_MERGE_TERM_H

#include <cstddef>
#include <string_view>

namespace pkgmerge::term {

/// Puts the console into UTF-8 mode (Windows only) and detects whether the
/// standard streams are a terminal. Call once, before anything is printed.
void init();

bool stdout_is_terminal();
bool stdin_is_terminal();

/// True on Windows when this process is the only one attached to the console,
/// i.e. the program was started by a double-click rather than from a shell.
bool owns_console();

void write_out(std::string_view utf8);
void write_out_line(std::string_view utf8);
void write_err_line(std::string_view utf8);

/// Rewrites the current progress line, padding with spaces to erase whatever
/// the previous, longer, line left behind. On a terminal this rewrites in
/// place; when redirected it prints a new line.
void progress_update(std::string_view utf8);

/// Ends a pending progress line: a newline on a terminal, nothing at all when
/// the lines are already discrete.
void progress_end();

/// Asks a yes/no question. Anything that is not an explicit yes - including an
/// empty answer, an unrecognised answer or end of input - means no
/// (spec section 9).
bool confirm_yes(std::string_view question_utf8);

/// Keeps a double-clicked console window open until the user acknowledges
/// (spec section 15). Does nothing unless owns_console() is true and pausing
/// has not been switched off with --no-pause.
void set_pause_enabled(bool enabled);
void pause_before_exit();

}  // namespace pkgmerge::term

#endif  // PKG_MERGE_TERM_H