// SPDX-License-Identifier: GPL-3.0-only
//
// Platform services that have no equivalent in the standard library:
// the folder dialog, the interrupt signal, the Escape key, free disk space and
// the console-close hook that removes the temporary file.
//
// No third-party code is used anywhere (spec section 15). Windows uses the
// platform's own IFileDialog; POSIX looks for zenity or kdialog at *run time*
// and is perfectly usable without either of them.

#ifndef PKG_MERGE_PLATFORM_H
#define PKG_MERGE_PLATFORM_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pkgmerge::platform {

/// Installs the interrupt handler, and on Windows the console control handler
/// that also removes the temporary file if the console window is closed.
void install_signal_handlers();

/// True once the platform's interrupt signal has been seen. Handlers only set
/// a flag, so this is the only thing they do.
bool interrupt_requested();

/// Escape pressed on the attached console (Windows, spec section 11). Does not
/// consume the key. Always false when there is no console, so a merge that
/// runs with output redirected cannot be cancelled by an unrelated keystroke.
bool escape_pressed();

/// Remembers the temporary file of the merge in progress so the console-close
/// handler can delete it. Passing an empty path clears it.
void set_active_temp_file(const std::filesystem::path& temp);
void clear_active_temp_file();

/// Result of asking the user for a folder.
struct FolderChoice {
  bool chosen = false;        // the user picked a folder
  bool cancelled = false;     // a dialog appeared and was dismissed
  bool unavailable = false;   // no dialog can be shown at all
  std::filesystem::path path;
  std::string detail;  // why it is unavailable, for the message
};

/// Shows the platform's folder dialog. Never blocks waiting for a dialog that
/// cannot appear: if no picker is available the result says so immediately.
FolderChoice choose_input_folder(const std::filesystem::path& start_dir);

/// Bytes available to this user on the volume holding `dir`. Empty when the
/// question cannot be answered, in which case the caller proceeds rather than
/// refusing a merge it cannot judge.
std::optional<std::uint64_t> available_space(const std::filesystem::path& dir);

}  // namespace pkgmerge::platform

#endif  // PKG_MERGE_PLATFORM_H