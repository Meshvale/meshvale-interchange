// SPDX-License-Identifier: Apache-2.0
#include "file_publication_native.h"

#include <cerrno>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

namespace meshvale::interchange::file_publication_detail {
CommitResult CommitNoReplace(const NativeChar* source,
                             const NativeChar* destination) noexcept {
#ifdef _WIN32
  if (MoveFileExW(source, destination, 0))
    return {CommitStatus::kCommitted, {}};
  const auto error = GetLastError();
  return {error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS
              ? CommitStatus::kDestinationExists
              : CommitStatus::kFailed,
          std::error_code(static_cast<int>(error), std::system_category())};
#elif defined(__linux__)
  if (syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination,
              RENAME_NOREPLACE) == 0)
    return {CommitStatus::kCommitted, {}};
  const int error = errno;
  return {error == EEXIST ? CommitStatus::kDestinationExists
                          : CommitStatus::kFailed,
          std::error_code(error, std::generic_category())};
#elif defined(__APPLE__)
  if (renamex_np(source, destination, RENAME_EXCL) == 0)
    return {CommitStatus::kCommitted, {}};
  const int error = errno;
  return {error == EEXIST ? CommitStatus::kDestinationExists
                          : CommitStatus::kFailed,
          std::error_code(error, std::generic_category())};
#else
  (void)source;
  (void)destination;
  return {CommitStatus::kUnsupported,
          std::make_error_code(std::errc::function_not_supported)};
#endif
}
}  // namespace meshvale::interchange::file_publication_detail
