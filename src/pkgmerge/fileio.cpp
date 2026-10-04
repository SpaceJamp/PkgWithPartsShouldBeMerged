// SPDX-License-Identifier: GPL-3.0-only

#include "pkgmerge/fileio.h"

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "pkgmerge/text.h"

namespace pkgmerge {
namespace {

#ifdef _WIN32

std::string wide_to_utf8(const wchar_t* text, const std::size_t length) {
  if (length == 0) {
    return std::string();
  }
  const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0,
                                         nullptr, nullptr);
  if (size <= 0) {
    return std::string();
  }
  std::string out;
  out.resize(static_cast<std::size_t>(size));
  const int written = ::WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(),
                                            size, nullptr, nullptr);
  if (written <= 0) {
    return std::string();
  }
  out.resize(static_cast<std::size_t>(written));
  return out;
}

std::string os_error_text(const DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD length = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::string text;
  if (length > 0 && buffer != nullptr) {
    std::size_t used = 0;
    while (used < static_cast<std::size_t>(length) && buffer[used] != L'\0') {
      ++used;
    }
    text = wide_to_utf8(buffer, used);
    ::LocalFree(buffer);
  }
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
    text.pop_back();
  }
  if (text.empty()) {
    text = concat("Windows error ", static_cast<unsigned long>(code));
  }
  return text;
}

std::wstring extended(const std::filesystem::path& path) {
  // The \\?\ form is what makes paths longer than 260 characters work
  // (spec section 15). Already-extended paths are passed through untouched.
  const std::wstring raw = path.wstring();
  if (raw.rfind(L"\\\\?\\", 0) == 0 || raw.rfind(L"\\\\.\\", 0) == 0) {
    return raw;
  }
  std::wstring normalised = path.lexically_normal().wstring();
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

bool open_read_handle(const std::filesystem::path& path, NativeFileHandle* handle) {
  const std::wstring wide = extended(path);
  *handle = ::CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  return *handle != INVALID_HANDLE_VALUE;
}

std::uint64_t handle_size(const NativeFileHandle handle) {
  LARGE_INTEGER size = {};
  if (::GetFileSizeEx(handle, &size) == FALSE) {
    return 0;
  }
  return static_cast<std::uint64_t>(size.QuadPart);
}

#else

std::string os_error_text(const int code) {
  // strerror() is not thread safe, but this program is single threaded and the
  // text is converted immediately.
  const char* text = std::strerror(code);
  if (text == nullptr) {
    return concat("errno ", code);
  }
  return std::string(text);
}

#endif

}  // namespace

InputFile::~InputFile() { close(); }

bool InputFile::open(const std::filesystem::path& path, std::string* error) {
  close();
#ifdef _WIN32
  NativeFileHandle handle = kInvalidFileHandle;
  if (!open_read_handle(path, &handle)) {
    const DWORD code = ::GetLastError();
    if (error != nullptr) {
      *error = concat("cannot open ", display_path(path), ": ", os_error_text(code));
    }
    return false;
  }
  handle_ = handle;
  open_ = true;
  size_ = handle_size(handle);
#else
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    if (error != nullptr) {
      *error = concat("cannot open ", display_path(path), ": ", os_error_text(errno));
    }
    return false;
  }
  handle_ = descriptor;
  open_ = true;
  struct stat info = {};
  if (::fstat(descriptor, &info) != 0) {
    if (error != nullptr) {
      *error = concat("cannot read the size of ", display_path(path), ": ",
                      os_error_text(errno));
    }
    close();
    return false;
  }
  size_ = (info.st_size < 0) ? 0U : static_cast<std::uint64_t>(info.st_size);
#endif
  return true;
}

std::int64_t InputFile::read(void* buffer, const std::size_t bytes, std::string* error) {
  if (!open_ || bytes == 0) {
    return 0;
  }
#ifdef _WIN32
  // Narrowing size_t -> DWORD is safe only because every caller passes at most
  // kCopyBlockSize (1 MiB). A single read is never larger than one block, and
  // the file offsets are tracked in 64-bit, so a multi-gigabyte file is many
  // reads rather than one large one.
  const DWORD want = static_cast<DWORD>(bytes);
  DWORD got = 0;
  if (::ReadFile(handle_, buffer, want, &got, nullptr) == FALSE) {
    if (error != nullptr) {
      *error = concat("read failed: ", os_error_text(::GetLastError()));
    }
    return -1;
  }
  return static_cast<std::int64_t>(got);
#else
  // A short read is normal and is reported as such; an interrupted read is not
  // an error and is retried here rather than being mistaken for one.
  ssize_t got = 0;
  do {
    got = ::read(static_cast<int>(handle_), buffer, bytes);
  } while (got < 0 && errno == EINTR);
  if (got < 0) {
    if (error != nullptr) {
      *error = concat("read failed: ", os_error_text(errno));
    }
    return -1;
  }
  return static_cast<std::int64_t>(got);
#endif
}

void InputFile::close() {
  if (!open_) {
    return;
  }
#ifdef _WIN32
  ::CloseHandle(handle_);
  handle_ = kInvalidFileHandle;
#else
  ::close(static_cast<int>(handle_));
  handle_ = kInvalidFileHandle;
#endif
  open_ = false;
  size_ = 0;
}

TempFile::~TempFile() {
  if (owns_path_) {
    discard();
  }
}

bool TempFile::create(const std::filesystem::path& path, std::string* error) {
  discard();
#ifdef _WIN32
  const std::wstring wide = extended(path);
  // FILE_SHARE_DELETE so that the console-close handler can delete this file
  // while the merge still has it open. Without it, an exclusive handle makes
  // that DeleteFileW fail and a temporary file survives an interrupted run
  // (spec section 7.1). CREATE_NEW still guarantees nothing of the user's is
  // ever clobbered.
  NativeFileHandle handle = ::CreateFileW(wide.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr,
                                          CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (error != nullptr) {
      *error = concat("cannot create ", display_path(path), ": ",
                      os_error_text(::GetLastError()));
    }
    return false;
  }
  handle_ = handle;
#else
  // 0666 so the file ends up with the user's usual permissions; O_EXCL so a
  // temporary file belonging to something else is never clobbered.
  const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
  if (descriptor < 0) {
    if (error != nullptr) {
      *error = concat("cannot create ", display_path(path), ": ", os_error_text(errno));
    }
    return false;
  }
  handle_ = descriptor;
#endif
  path_ = path;
  owns_path_ = true;
  open_ = true;
  return true;
}

bool TempFile::write_all(const void* buffer, const std::size_t bytes, std::string* error) {
  if (!open_) {
    if (error != nullptr) {
      *error = "the output file is not open";
    }
    return false;
  }
  const char* data = static_cast<const char*>(buffer);
  std::size_t left = bytes;
  while (left > 0) {
#ifdef _WIN32
    const DWORD want = static_cast<DWORD>(left);
    DWORD written = 0;
    if (::WriteFile(handle_, data, want, &written, nullptr) == FALSE) {
      if (error != nullptr) {
        *error = concat("write failed: ", os_error_text(::GetLastError()));
      }
      return false;
    }
    if (written == 0) {
      if (error != nullptr) {
        *error = "write failed: the device accepted no data";
      }
      return false;
    }
    const std::size_t step = static_cast<std::size_t>(written);
#else
    const ssize_t written = ::write(handle_, data, left);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (error != nullptr) {
        *error = concat("write failed: ", os_error_text(errno));
      }
      return false;
    }
    if (written == 0) {
      if (error != nullptr) {
        *error = "write failed: the device accepted no data";
      }
      return false;
    }
    const std::size_t step = static_cast<std::size_t>(written);
#endif
    data += step;
    left -= step;
  }
  return true;
}

bool TempFile::measured_size(std::uint64_t* size, std::string* error) {
  if (!open_) {
    if (error != nullptr) {
      *error = "the output file is not open";
    }
    return false;
  }
#ifdef _WIN32
  if (::FlushFileBuffers(handle_) == FALSE) {
    if (error != nullptr) {
      *error = concat("cannot flush the output: ", os_error_text(::GetLastError()));
    }
    return false;
  }
  LARGE_INTEGER written = {};
  if (::GetFileSizeEx(handle_, &written) == FALSE) {
    if (error != nullptr) {
      *error = concat("cannot measure the output: ", os_error_text(::GetLastError()));
    }
    return false;
  }
  *size = static_cast<std::uint64_t>(written.QuadPart);
  close();
  return true;
#else
  // Durability is not required, but a successful fsync turns a deferred write
  // error into an error we can report instead of one the user finds later.
  (void)::fsync(handle_);
  struct stat info = {};
  if (::fstat(handle_, &info) != 0) {
    if (error != nullptr) {
      *error = concat("cannot measure the output: ", os_error_text(errno));
    }
    return false;
  }
  *size = (info.st_size < 0) ? 0U : static_cast<std::uint64_t>(info.st_size);
  close();
  return true;
#endif
}

void TempFile::close() {
  if (!open_) {
    return;
  }
#ifdef _WIN32
  ::CloseHandle(handle_);
  handle_ = kInvalidFileHandle;
#else
  ::close(handle_);
  handle_ = kInvalidFileHandle;
#endif
  open_ = false;
}

void TempFile::discard() {
  const bool owned = owns_path_;
  const std::filesystem::path victim = path_;
  close();
  owns_path_ = false;
  path_.clear();
  if (owned) {
    remove_file_quietly(victim);
  }
}

bool file_size(const std::filesystem::path& path, std::uint64_t* size, std::string* error) {
  InputFile probe;
  if (!probe.open(path, error)) {
    return false;
  }
  *size = probe.size();
  probe.close();
  return true;
}

bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to,
                  std::string* error) {
#ifdef _WIN32
  // Same directory, so this is a rename, not a copy: atomic, and no moment at
  // which either name is missing (spec section 7.1).
  const std::wstring wide_from = extended(from);
  const std::wstring wide_to = extended(to);
  if (::MoveFileExW(wide_from.c_str(), wide_to.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
    if (error != nullptr) {
      *error = concat("cannot rename ", display_path(from), " to ", display_path(to), ": ",
                      os_error_text(::GetLastError()));
    }
    return false;
  }
  return true;
#else
  if (::rename(from.c_str(), to.c_str()) != 0) {
    if (error != nullptr) {
      *error = concat("cannot rename ", display_path(from), " to ", display_path(to), ": ",
                      os_error_text(errno));
    }
    return false;
  }
  return true;
#endif
}

void remove_file_quietly(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
#ifdef _WIN32
  const std::wstring wide = extended(path);
  ::DeleteFileW(wide.c_str());
#else
  ::unlink(path.c_str());
#endif
}

}  // namespace pkgmerge