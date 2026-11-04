// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_WORK_H_
#define MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_WORK_H_

#include <meshvale/interchange/gltf.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_detail {

// Private deformation-only scheduling bound. It is never an observed count or
// a reservation against a semantic hard cap. These comparison phases use R/B;
// C/I are present only to keep the owning S=C+I+R contract explicit.
struct DeformationRangeExtent {
  std::uint64_t scalars = 0;
  std::uint64_t integer_steps = 0;
  std::uint64_t relationships = 0;
  std::uint64_t bytes = 0;
};
struct DeformationWorkCounters {
  std::uint64_t relationships = 0;
  std::uint64_t bytes = 0;
};
enum class DeformationWorkRefusal { kRelationshipLimit, kSizeOverflow };

// A lexical borrow of one existing producer's policy, facts and allocation
// parent. It creates no context, thread pool or public resource/counter leaf.
// Only the caller merges public facts. Disjoint worker callbacks update their
// own local counters and atomically admit each actual reference before its
// visit.
class DeformationWork {
 public:
  using Refuse = void (*)(void*, DeformationWorkRefusal);
  DeformationWork(const GltfDeformationPolicy& policy,
                  GltfDeformationObservation& observation,
                  const AllocationContext& allocation, Refuse refuse,
                  void* refusal_state);
  DeformationWork(const DeformationWork&) = delete;
  DeformationWork& operator=(const DeformationWork&) = delete;
  void SetAllocation(const AllocationContext& allocation);
  void Reference();
  void Bytes(std::uint64_t count);
  void Checkpoint() const;
  void Merge(const DeformationWorkCounters& counters);
  void CheckMerged() const;
  [[nodiscard]] std::uint64_t Add(std::uint64_t a, std::uint64_t b) const;
  [[nodiscard]] std::uint64_t Multiply(std::uint64_t a, std::uint64_t b) const;
  [[nodiscard]] std::uint64_t ScalarWork(
      const DeformationRangeExtent& extent) const;
  [[nodiscard]] const GltfDeformationWorkPolicy& Policy() const;
  static void ReferenceCallback(void* state);

  class Local {
   public:
    Local(DeformationWork& owner, DeformationWorkCounters& counters);
    void Reference();
    void Bytes(std::uint64_t count);
    void Checkpoint() const;
    static void ReferenceCallback(void* state);

   private:
    DeformationWork& owner_;
    DeformationWorkCounters& counters_;
  };

  // Construct only after joins, an actual known mismatch and later canceled
  // work. This finite allocation-free prefix preserves the earlier diagnostic;
  // it suppresses cancellation alone. Actual cap/CAS/overflow/lifetime checks
  // remain active, with no counter reset or public ignore-stop switch.
  class FinalizeMismatchPrefix {
   public:
    explicit FinalizeMismatchPrefix(DeformationWork& owner);
    ~FinalizeMismatchPrefix();
    FinalizeMismatchPrefix(const FinalizeMismatchPrefix&) = delete;
    FinalizeMismatchPrefix& operator=(const FinalizeMismatchPrefix&) = delete;

   private:
    DeformationWork& owner_;
    bool previous_;
  };

 private:
  [[noreturn]] void Fail(DeformationWorkRefusal reason) const;
  void ReserveReference();
  void CountBytes(std::uint64_t& total, std::uint64_t count);
  const GltfDeformationPolicy& policy_;
  GltfDeformationObservation& observation_;
  AllocationContext allocation_;
  Refuse refuse_;
  void* refusal_state_;
  std::atomic<std::uint64_t> relationships_;
  std::uint64_t serial_bytes_ = 0;
  bool finalizing_mismatch_prefix_ = false;
};
}  // namespace meshvale::interchange::gltf_detail
#endif  // MESHVALE_INTERCHANGE_SRC_GLTF_DEFORMATION_WORK_H_
