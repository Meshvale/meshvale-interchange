// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_FILE_PUBLISH_INTERNAL_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_FILE_PUBLISH_INTERNAL_H_

#include <meshvale/interchange/gltf_files.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <system_error>

#include "gltf_allocation.h"
#include "gltf_files_internal.h"
#include "gltf_files_test_internal.h"

namespace meshvale::interchange::gltf_files_detail {
struct WalkFrame;
struct WalkEntry;
using InventoryVisitor = bool (*)(NativeView path, bool directory,
                                  void* state) noexcept;
// All explicit walk/path/frame capacity is admitted before staging. Walking and
// cleanup never grow it. OS directory-enumerator internals retain their
// declared exclusion. Root removal requires an unchanged owned root identity.
class NativePublication {
 public:
  NativePublication(const gltf_detail::AllocationContext& allocation,
                    const GltfFileLimits& limits,
                    const GltfFilePublicationHooks& hooks,
                    std::stop_token stop);
  NativePublication(const NativePublication&) = delete;
  NativePublication& operator=(const NativePublication&) = delete;
  ~NativePublication() noexcept;
  void Plan(NativeView destination);
  void AdmitParent();
  void CreateStage();
  void CreateSubdirectory(NativeView path);
  void Inventory(InventoryVisitor visitor, void* state);
  void Commit();
  GltfFileCleanupOutcome Cleanup(std::error_code& error) noexcept;
  [[nodiscard]] NativeView Stage() const noexcept { return stage_; }
  [[nodiscard]] bool OwnsStage() const noexcept { return owned_; }

 private:
  std::error_code Walk(bool remove, InventoryVisitor visitor,
                       void* state) noexcept;
  gltf_detail::AllocationContext allocation_;
  const GltfFileLimits& limits_;
  const GltfFilePublicationHooks& hooks_;
  NativeString destination_;
  NativeString parent_;
  NativeString stage_;
  NativeString scratch_;
  Vector<WalkFrame> frames_;
  Vector<WalkEntry> entries_;
  Vector<NativeFile> root_handles_;
  NativeMetadata identity_;
  bool owned_ = false;
  std::stop_token stop_;
};
NativeFile OpenWrite(NativeView path, std::uint64_t ordinal,
                     const GltfFileHooks& hooks, bool exclusive);
void WriteBlock(const NativeFile& file, std::uint64_t offset,
                std::span<const std::byte> bytes);
// Explicit close errors are expected staging failures, before native commit.
// The ordinary destructor remains only a nonthrowing fallback.
void CloseWrittenFile(NativeFile& file);
}  // namespace meshvale::interchange::gltf_files_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_FILE_PUBLISH_INTERNAL_H_
