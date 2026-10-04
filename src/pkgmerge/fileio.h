// SPDX-License-Identifier: GPL-3.0-only
//
// Raw, streaming file access.
//
// Every size and offset in this module is std::uint64_t, on every target, so
// a 32-bit build still merges sets whose total is larger than its own address
// space (spec sections 8 and 17.6). Nothing is mapped and no piece is ever read
// into memory as a whole: callers move a fixed-size block at a time.
//
// Both implementations keep the read path separate from the write path, so a
// short read or a partial write can be retried without confusing the offsets.

#ifndef PKG_MERGE_FILEIO_H
#define PKG_MERGE_FILEIO_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pkgmerge {

#if defined(_WIN32)
using NativeFileHandle = void*;  // Win32 HANDLE
// The "no handle" value. A real Win32 handle is never null, so nullptr is used
// rather than INVALID_HANDLE_VALUE: a constant expression may not reinterpret an
// integer as a pointer, and nothing here ever compares against the Win32
// spelling - the open_ flag says whether there is a handle.
inline constexpr NativeFileHandle kInvalidFileHandle = nullptr;
#else
using NativeFileHandle = int;  // POSIX file descriptor
inline constexpr NativeFileHandle kInvalidFileHandle = -1;
#endif

/// A piece file, opened for reading only.
class InputFile {
 public:
  InputFile() = default;
  ~InputFile();

  InputFile(const InputFile&) = delete;
  InputFile& operator=(const InputFile&) = delete;

  /// Opens `path` for reading. Returns false and fills `error` on failure.
  bool open(const std::filesystem::path& path, std::string* error);

  /// Bytes the file held when it was opened. Only valid while open.
  std::uint64_t size() const { return size_; }

  /// Reads up to `bytes` into `buffer`. Returns the number of bytes read, 0 at
  /// end of file, or -1 on error (with `error` filled).
  std::int64_t read(void* buffer, std::size_t bytes, std::string* error);

  void close();

  bool is_open() const { return open_; }

 private:
  NativeFileHandle handle_ = kInvalidFileHandle;
  bool open_ = false;
  std::uint64_t size_ = 0;
};

/// The temporary output file of one merge: created fresh, so it never
/// truncates anything that already exists, and removed by the destructor unless
/// the merge released it after a successful rename (spec section 7.1).
class TempFile {
 public:
  TempFile() = default;
  ~TempFile();

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;

  /// Creates `path`, failing if it exists already.
  bool create(const std::filesystem::path& path, std::string* error);

  /// Writes all `bytes`, retrying short writes. False on error.
  bool write_all(const void* buffer, std::size_t bytes, std::string* error);

  /// Flushes, then reads the length the file system reports. This is the
  /// verification step of spec section 7.2, taken before the rename.
  bool measured_size(std::uint64_t* size, std::string* error);

  /// Closes the handle without deleting the file. Only call this once the file
  /// has been renamed or after discard().
  void close();

  /// Closes and deletes. Safe to call more than once; used on every failure
  /// path, so no temporary file can survive.
  void discard();

  /// Forgets the file, which now lives under its final name.
  void release() { owns_path_ = false; }

  bool is_open() const { return open_; }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  bool owns_path_ = false;
  NativeFileHandle handle_ = kInvalidFileHandle;
  bool open_ = false;
};

/// Size of an existing file, or false if it cannot be determined.
bool file_size(const std::filesystem::path& path, std::uint64_t* size, std::string* error);

/// Moves `from` onto `to`, replacing `to` atomically where the platform allows
/// it.
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to,
                  std::string* error);

/// Deletes `path`, ignoring failure. Used only for this program's own
/// temporary file.
void remove_file_quietly(const std::filesystem::path& path);

}  // namespace pkgmerge

#endif  // PKG_MERGE_FILEIO_H