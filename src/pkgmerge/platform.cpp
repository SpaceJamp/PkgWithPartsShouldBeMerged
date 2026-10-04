// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/platform.h"

#include "pkgmerge/term.h"
#include "pkgmerge/text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <csignal>
#include <cstring>
#include <cwchar>
#else
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pkgmerge::platform {
namespace {

// Only ever set from a signal or control handler.
volatile std::sig_atomic_t g_interrupt = 0;

extern "C" void handle_interrupt(int /*signal_number*/) { g_interrupt = 1; }

#ifdef _WIN32

// Read by the console control handler, which has to be able to delete the
// temporary file without touching anything that could deadlock. A fixed
// buffer: no allocation happens on the cleanup path.
constexpr std::size_t kTempPathCapacity = 512;
wchar_t g_active_temp[kTempPathCapacity] = L"";

BOOL WINAPI console_control_handler(const DWORD control_type) {
  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      g_interrupt = 1;
      return TRUE;  // handled: keep running so the merge can clean up
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      g_interrupt = 1;
      if (g_active_temp[0] != L'\0') {
        // The window is going away. DeleteFileW is a leaf call; nothing here
        // can throw or block on a lock for long.
        ::DeleteFileW(g_active_temp);
        g_active_temp[0] = L'\0';
      }
      return TRUE;
    default:
      break;
  }
  return FALSE;
}

// A \\?\ path, so the Win32 calls below are not limited to 260 characters
// (spec section 15).
std::wstring extended_path(const std::filesystem::path& p) {
  const std::wstring raw = p.wstring();
  if (raw.rfind(L"\\\\?\\", 0) == 0 || raw.rfind(L"\\\\.\\", 0) == 0) {
    return raw;
  }
  std::wstring normalised = std::filesystem::path(p).lexically_normal().wstring();
  for (wchar_t& c : normalised) {
    if (c == L'/') {
      c = L'\\';
    }
  }
  if (normalised.size() >= 2 && normalised[1] == L':') {
    return L"\\\\?\\" + normalised;
  }
  if (normalised.rfind(L"\\\\", 0) == 0) {
    return L"\\\\?\\UNC\\" + normalised.substr(2);
  }
  return L"\\\\?\\" + normalised;
}

#else  // !_WIN32

void set_active_temp_file_posix(const std::filesystem::path& temp) {
  // Nothing to do: POSIX has no console-close event, and the temporary file is
  // removed by TempFile's destructor on every exit path that returns normally.
  (void)temp;
}

#endif

}  // namespace

void install_signal_handlers() {
#ifdef _WIN32
  ::SetConsoleCtrlHandler(console_control_handler, TRUE);
  std::signal(SIGINT, handle_interrupt);
  std::signal(SIGTERM, handle_interrupt);
#else
  struct sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_handler = handle_interrupt;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;  // no SA_RESTART: a blocking read returns EINTR
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
#endif
}

bool interrupt_requested() { return g_interrupt != 0; }

bool escape_pressed() {
#ifdef _WIN32
  const HANDLE handle = ::GetStdHandle(STD_INPUT_HANDLE);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD mode = 0;
  if (::GetConsoleMode(handle, &mode) == FALSE) {
    return false;  // no console: nothing to read a key from
  }
  INPUT_RECORD record;
  DWORD available = 0;
  while (::PeekConsoleInput(handle, &record, 1, &available) != FALSE && available > 0) {
    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown != 0 &&
        record.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
      return true;
    }
  }
  return false;
#else
  return false;  // spec section 11 requires the Escape key on Windows only
#endif
}

void set_active_temp_file(const std::filesystem::path& temp) {
#ifdef _WIN32
  const std::wstring text = temp.wstring();
  const std::size_t count = (text.size() < kTempPathCapacity - 1) ? text.size() : kTempPathCapacity - 1;
  std::wmemcpy(g_active_temp, text.c_str(), count);
  g_active_temp[count] = L'\0';
#else
  set_active_temp_file_posix(temp);
#endif
}

void clear_active_temp_file() {
#ifdef _WIN32
  g_active_temp[0] = L'\0';
#endif
}

FolderChoice choose_input_folder(const std::filesystem::path& start_dir) {
  FolderChoice result;

#ifdef _WIN32
  // The platform's own folder picker (spec section 15). Directories only.
  const HRESULT init_result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool must_uninitialize = SUCCEEDED(init_result);

  IFileOpenDialog* dialog = nullptr;
  const HRESULT created =
      ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(created) || dialog == nullptr) {
    if (must_uninitialize) {
      ::CoUninitialize();
    }
    result.unavailable = true;
    result.detail = "the Windows file dialog could not be created";
    return result;
  }

  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                       FOS_NOCHANGEDIR);
  }
  if (!start_dir.empty()) {
    IShellItem* folder = nullptr;
    if (SUCCEEDED(::SHCreateItemFromParsingName(start_dir.c_str(), nullptr, IID_PPV_ARGS(&folder))) &&
        folder != nullptr) {
      dialog->SetFolder(folder);
      folder->Release();
    }
  }

  const HRESULT shown = dialog->Show(nullptr);
  if (SUCCEEDED(shown)) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
      PWSTR name = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name != nullptr) {
        result.path = std::filesystem::path(name);
        result.chosen = true;
        ::CoTaskMemFree(name);
      }
      item->Release();
    }
  }
  dialog->Release();
  if (must_uninitialize) {
    ::CoUninitialize();
  }

  if (!result.chosen) {
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      result.cancelled = true;
    } else {
      // No interactive window station, for instance: report it rather than
      // sitting there waiting for a dialog that cannot appear.
      result.unavailable = true;
      result.detail = "the Windows file dialog could not be shown";
    }
  }
#else
  // POSIX: run whatever picker is installed. Nothing here is a build-time
  // dependency (spec section 15); a machine with neither tool installed runs
  // perfectly well from a terminal with --input.
  struct Picker {
    const char* program;
    bool zenity;
  };
  const Picker kPickers[2] = {{"zenity", true}, {"kdialog", false}};

  std::string tool_found;
  for (const Picker& picker : kPickers) {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
      continue;
    }
    const pid_t child = ::fork();
    if (child < 0) {
      ::close(pipe_fds[0]);
      ::close(pipe_fds[1]);
      continue;
    }
    if (child == 0) {
      // Only async-signal-safe calls between fork() and exec*().
      ::close(pipe_fds[0]);
      if (::dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
        ::_exit(127);
      }
      if (pipe_fds[1] != STDOUT_FILENO) {
        ::close(pipe_fds[1]);
      }
      // The tool's own diagnostics must not be mistaken for our output.
      const int devnull = ::open("/dev/null", O_WRONLY);
      if (devnull >= 0) {
        (void)::dup2(devnull, STDERR_FILENO);
        if (devnull != STDERR_FILENO) {
          ::close(devnull);
        }
      }
      // Both tools accept a starting directory; "." when the caller has none.
      const std::string start = start_dir.empty() ? std::string(".") : start_dir.string();
      if (picker.zenity) {
        ::execlp("zenity", "zenity", "--file-selection", "--directory", "--title",
                 "Select the folder that holds the PKG pieces", "--filename", start.c_str(),
                 static_cast<char*>(nullptr));
      } else {
        ::execlp("kdialog", "kdialog", "--title", "Select the folder that holds the PKG pieces",
                 "--getexistingdirectory", start.c_str(), static_cast<char*>(nullptr));
      }
      ::_exit(127);  // the tool is not installed
    }

    ::close(pipe_fds[1]);
    std::string chosen;
    char buffer[512];
    for (;;) {
      const ssize_t got = ::read(pipe_fds[0], buffer, sizeof(buffer));
      if (got > 0) {
        chosen.append(buffer, static_cast<std::size_t>(got));
        continue;
      }
      if (got == 0) {
        break;  // end of output
      }
      if (errno == EINTR) {
        continue;
      }
      break;  // a read error: fall through to what we already have
    }
    ::close(pipe_fds[0]);

    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
      // retry
    }
    const bool exited = WIFEXITED(status);
    const int code = exited ? WEXITSTATUS(status) : -1;

    if (tool_found.empty()) {
      if (code == 127) {
        continue;  // not installed; try the next one
      }
      tool_found = picker.program;
    }
    if (code == 0) {
      const std::string_view trimmed = trim_ascii(chosen);
      if (trimmed.empty()) {
        continue;  // accepted nothing
      }
      result.path = std::filesystem::path(std::string(trimmed));
      result.chosen = true;
      return result;
    }
    // The tool ran and produced no folder: dismissed, or no GUI session.
    result.cancelled = true;
    return result;
  }

  if (tool_found.empty()) {
    result.unavailable = true;
    result.detail = "neither zenity nor kdialog is installed";
  }
#endif

  return result;
}

std::optional<std::uint64_t> available_space(const std::filesystem::path& dir) {
#ifdef _WIN32
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(dir, ec);
  if (ec) {
    return std::nullopt;
  }
  const std::wstring volume = extended_path(absolute / L"\\");
  ULARGE_INTEGER available = {};
  if (::GetDiskFreeSpaceExW(volume.c_str(), &available, nullptr, nullptr) == FALSE) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(available.QuadPart);
#else
  struct statvfs info;
  if (::statvfs(dir.c_str(), &info) != 0) {
    return std::nullopt;
  }
  // f_bavail is what this user may actually write; both fields are wider than
  // 32 bits on every supported platform, and the product is computed in 64.
  const std::uint64_t blocks = static_cast<std::uint64_t>(info.f_bavail);
  const std::uint64_t block_size = static_cast<std::uint64_t>(info.f_frsize);
  return blocks * block_size;
#endif
}

}  // namespace pkgmerge::platform