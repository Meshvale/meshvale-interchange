// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_FILE_PUBLICATION_NATIVE_H_
#define MESHVALE_INTERCHANGE_SRC_FILE_PUBLICATION_NATIVE_H_

#include <system_error>

namespace meshvale::interchange::file_publication_detail {
#ifdef _WIN32
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
enum class CommitStatus {
  kCommitted,
  kDestinationExists,
  kFailed,
  kUnsupported
};
struct CommitResult {
  CommitStatus status;
  std::error_code error;
};
// Paths are preexisting, terminated native strings. This performs exactly one
// no-replace native rename; it never allocates a path or falls back to copying.
[[nodiscard]] CommitResult CommitNoReplace(
    const NativeChar* source, const NativeChar* destination) noexcept;
}  // namespace meshvale::interchange::file_publication_detail
#endif  // MESHVALE_INTERCHANGE_SRC_FILE_PUBLICATION_NATIVE_H_
