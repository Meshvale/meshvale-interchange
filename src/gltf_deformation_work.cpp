// SPDX-License-Identifier: Apache-2.0
#include "gltf_deformation_work.h"

#include <exception>
#include <limits>

namespace meshvale::interchange::gltf_detail {
DeformationWork::DeformationWork(const GltfDeformationPolicy& policy,
                                 GltfDeformationObservation& observation,
                                 const AllocationContext& allocation,
                                 Refuse refuse, void* refusal_state)
    : policy_(policy),
      observation_(observation),
      allocation_(allocation),
      refuse_(refuse),
      refusal_state_(refusal_state),
      relationships_(observation.counts.relationship_steps) {
  if (!refuse_) throw AdmissionFailure(AdmissionCode::kInvalidObject);
}
void DeformationWork::SetAllocation(const AllocationContext& allocation) {
  if (!allocation_.SharesExecutionBudget(allocation))
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
  allocation_ = allocation;
}
[[noreturn]] void DeformationWork::Fail(DeformationWorkRefusal reason) const {
  refuse_(refusal_state_, reason);
  std::terminate();
}
std::uint64_t DeformationWork::Add(std::uint64_t a, std::uint64_t b) const {
  if (a > std::numeric_limits<std::uint64_t>::max() - b)
    Fail(DeformationWorkRefusal::kSizeOverflow);
  return a + b;
}
std::uint64_t DeformationWork::Multiply(std::uint64_t a,
                                        std::uint64_t b) const {
  if (b && a > std::numeric_limits<std::uint64_t>::max() / b)
    Fail(DeformationWorkRefusal::kSizeOverflow);
  return a * b;
}
std::uint64_t DeformationWork::ScalarWork(
    const DeformationRangeExtent& extent) const {
  return Add(Add(extent.scalars, extent.integer_steps), extent.relationships);
}
const GltfDeformationWorkPolicy& DeformationWork::Policy() const {
  return policy_.work;
}
void DeformationWork::Checkpoint() const {
  try {
    allocation_.Checkpoint();
  } catch (const AdmissionFailure& failure) {
    if (!finalizing_mismatch_prefix_ ||
        failure.Code() != AdmissionCode::kCanceled)
      throw;
  }
}
void DeformationWork::ReserveReference() {
  auto previous = relationships_.load(std::memory_order_relaxed);
  for (;;) {
    if (previous >= policy_.limits.max_relationship_steps)
      Fail(DeformationWorkRefusal::kRelationshipLimit);
    if (relationships_.compare_exchange_weak(previous, previous + 1,
                                             std::memory_order_relaxed))
      return;
    Checkpoint();
  }
}
void DeformationWork::Reference() {
  ReserveReference();
  auto& count = observation_.counts.relationship_steps;
  count = Add(count, 1);
  if (count % policy_.work.max_relationship_block == 0) Checkpoint();
}
void DeformationWork::CountBytes(std::uint64_t& total, std::uint64_t count) {
  const auto before = total;
  total = Add(total, count);
  if (before / policy_.work.cancellation_block_bytes !=
      total / policy_.work.cancellation_block_bytes)
    Checkpoint();
}
void DeformationWork::Bytes(std::uint64_t count) {
  CountBytes(serial_bytes_, count);
}
void DeformationWork::Merge(const DeformationWorkCounters& counters) {
  auto& count = observation_.counts.relationship_steps;
  count = Add(count, counters.relationships);
}
void DeformationWork::CheckMerged() const {
  if (relationships_.load(std::memory_order_relaxed) !=
      observation_.counts.relationship_steps)
    throw AdmissionFailure(AdmissionCode::kInvalidObject);
}
void DeformationWork::ReferenceCallback(void* state) {
  static_cast<DeformationWork*>(state)->Reference();
}
DeformationWork::Local::Local(DeformationWork& owner,
                              DeformationWorkCounters& counters)
    : owner_(owner), counters_(counters) {}
void DeformationWork::Local::Reference() {
  owner_.ReserveReference();
  counters_.relationships = owner_.Add(counters_.relationships, 1);
  if (counters_.relationships % owner_.policy_.work.max_relationship_block == 0)
    owner_.Checkpoint();
}
void DeformationWork::Local::Bytes(std::uint64_t count) {
  owner_.CountBytes(counters_.bytes, count);
}
void DeformationWork::Local::Checkpoint() const { owner_.Checkpoint(); }
void DeformationWork::Local::ReferenceCallback(void* state) {
  static_cast<Local*>(state)->Reference();
}
DeformationWork::FinalizeMismatchPrefix::FinalizeMismatchPrefix(
    DeformationWork& owner)
    : owner_(owner), previous_(owner.finalizing_mismatch_prefix_) {
  owner_.finalizing_mismatch_prefix_ = true;
}
DeformationWork::FinalizeMismatchPrefix::~FinalizeMismatchPrefix() {
  owner_.finalizing_mismatch_prefix_ = previous_;
}
}  // namespace meshvale::interchange::gltf_detail
