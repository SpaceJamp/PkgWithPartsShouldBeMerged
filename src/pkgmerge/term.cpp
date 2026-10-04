// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/term.h"

#include <cstdio>
#include <iostream>
#include <string>

#include "pkgmerge/text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pkgmerge::term {
namespace {

bool g_stdout_tty = false;
bool g_stdin_tty = false;
bool g_pause_enabled = true;
bool g_progress_pending = false;
std::size_t g_progress_width = 0;

void write_bytes(std::FILE* stream, std::string_view bytes) {
  if (bytes.empty()) {
    return;
  }
  std::fwrite(bytes.data(), 1, bytes.size(), stream);
  std::fflush(stream);
}

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view utf8) {
  if (utf8.empty()) {
    return std::wstring();
  }
  const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           utf8.data(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
  if (needed <= 0) {
    return std::wstring();
  }
  std::wstring wide(static_cast<std::size_t>(needed), L'\0');
  const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            utf8.data(), static_cast<int>(utf8.size()),
                                            wide.data(), needed);
  if (written <= 0) {
    return std::wstring();
  }
  wide.resize(static_cast<std::size_t>(written));
  return wide;
}

// Writes to a real console as characters. Returns false when the handle is not
// a console or the write failed, so the caller can fall back to bytes.
bool write_console(const HANDLE handle, std::string_view utf8) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  if (::GetFileType(handle) != FILE_TYPE_CHAR) {
    return false;  // redirected: the caller writes UTF-8 bytes instead
  }
  const std::wstring wide = utf8_to_wide(utf8);
  if (wide.empty() && !utf8.empty()) {
    return false;  // not valid UTF-8; bytes are all that is left
  }
  DWORD offset = 0;
  while (offset < wide.size()) {
    DWORD written = 0;
    const DWORD want = static_cast<DWORD>(wide.size() - offset);
    if (::WriteConsoleW(handle, wide.data() + offset, want, &written, nullptr) == FALSE) {
      return false;
    }
    if (written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}
#endif

}  // namespace

void init() {
#ifdef _WIN32
  // Belt and braces: every byte this program writes to a console goes through
  // WriteConsoleW, but a UTF-8 code page also keeps anything the user types
  // (the confirmation prompt) readable.
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
#endif
  g_stdout_tty = stdout_is_terminal();
  g_stdin_tty = stdin_is_terminal();
}

bool stdout_is_terminal() {
#ifdef _WIN32
  const HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD mode = 0;
  return ::GetConsoleMode(handle, &mode) != FALSE;
#else
  return ::isatty(STDOUT_FILENO) != 0;
#endif
}

bool stdin_is_terminal() {
#ifdef _WIN32
  const HANDLE handle = ::GetStdHandle(STD_INPUT_HANDLE);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD mode = 0;
  return ::GetConsoleMode(handle, &mode) != FALSE;
#else
  return ::isatty(STDIN_FILENO) != 0;
#endif
}

bool owns_console() {
#ifdef _WIN32
  // A double-clicked program gets a console of its own, so it is the only
  // process in the process list. Started from a shell, the shell is in there
  // too and closing the window on exit would be rude.
  DWORD pids[8];
  const DWORD count =
      ::GetConsoleProcessList(pids, static_cast<DWORD>(sizeof(pids) / sizeof(pids[0])));
  return count == 1;
#else
  return false;
#endif
}

void write_out(std::string_view utf8) {
#ifdef _WIN32
  if (write_console(::GetStdHandle(STD_OUTPUT_HANDLE), utf8)) {
    return;
  }
#endif
  write_bytes(stdout, utf8);
}

void write_out_line(std::string_view utf8) {
  write_out(utf8);
  write_out("\n");
}

void write_err_line(std::string_view utf8) {
#ifdef _WIN32
  const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
  if (write_console(handle, utf8)) {
    (void)write_console(handle, "\n");
    return;
  }
#endif
  write_bytes(stderr, utf8);
  write_bytes(stderr, "\n");
}

void progress_update(std::string_view utf8) {
  g_progress_pending = true;
  if (!g_stdout_tty) {
    write_out_line(utf8);
    g_progress_width = 0;
    return;
  }
  std::string line;
  line.reserve(utf8.size() + 2);
  line.push_back('\r');
  line.append(utf8);
  if (g_progress_width > utf8.size()) {
    line.append(g_progress_width - utf8.size(), ' ');
  }
  g_progress_width = utf8.size();
  write_out(line);
}

void progress_end() {
  if (!g_progress_pending) {
    return;
  }
  g_progress_pending = false;
  g_progress_width = 0;
  if (g_stdout_tty) {
    write_out("\n");
  }
}

bool confirm_yes(std::string_view question_utf8) {
  write_out(question_utf8);
  write_out(" [y/N]: ");
  std::string answer;
  if (!std::getline(std::cin, answer)) {
    return false;  // end of input: the default is no (spec section 9)
  }
  const std::string_view trimmed = trim_ascii(answer);
  return iequals_ascii(trimmed, "y") || iequals_ascii(trimmed, "yes");
}

void set_pause_enabled(bool enabled) { g_pause_enabled = enabled; }

void pause_before_exit() {
  if (!g_pause_enabled || !g_stdout_tty || !g_stdin_tty || !owns_console()) {
    return;
  }
  progress_end();
  write_out_line("");
  write_out("Press Enter to close this window...");
  std::string ignored;
  (void)std::getline(std::cin, ignored);
}

}  // namespace pkgmerge::term