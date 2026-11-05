// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_PYTHON_SURFACE_RECORD_CAPTURE_H_
#define MESHVALE_INTERCHANGE_PYTHON_SURFACE_RECORD_CAPTURE_H_

#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <nanobind/nanobind.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

#include "../src/gltf_allocation.h"
#include "../src/gltf_operation_workers.h"

namespace meshvale::interchange::surface_python_detail {

// Private binding observations, not Surface proofs or a new public wire schema.
enum class RecordIngressStatus {
  kCaptured,
  kLimitExceeded,
  kBudgetExceeded,
  kCanceled
};
enum class RecordIngressField {
  kRecord,
  kPositions,
  kFaceOffsets,
  kCornerVertices,
  kAttributes,
  kAttributeDescriptor,
  kAttributeValues,
  kAttributeOffsets,
  kAttributePresence,
  kMetadata,
  kCatalogJson,
  kCatalogResource,
  kWorkspace
};
struct RecordIngressLimits {
  std::uint64_t max_buffer_views = 0;
  std::uint64_t max_buffer_bytes = 0;
  std::uint64_t max_total_buffer_bytes = 0;
  std::uint64_t max_attributes = 0;
  std::uint64_t max_metadata_entries = 0;
  std::uint64_t max_descriptor_bytes = 0;
};
struct RecordIngressWork {
  std::uint64_t cancellation_block_bytes = 0;
  std::uint64_t cancellation_block_items = 0;
};
struct RecordIngressObservation {
  std::optional<RecordIngressStatus> status;
  // Populated only from an actually classified allocator refusal. A count or
  // descriptor limit does not establish allocator code/provenance.
  std::optional<gltf_detail::AdmissionCode> allocation_code;
  std::optional<gltf_detail::AdmissionProvenance> allocation_provenance;
  std::optional<RecordIngressField> first_field;
  std::optional<std::uint64_t> first_row;
  std::uint64_t attempted_buffers = 0;
  std::uint64_t buffer_views = 0;
  std::uint64_t buffer_bytes = 0;
  std::uint64_t attributes = 0;
  std::uint64_t metadata_entries = 0;
  std::uint64_t descriptor_bytes = 0;
  // Addressed source bytes above are not owned storage or copy work. These
  // counters include only successful real staging allocations; request bytes
  // include the actual allocator header/alignment charge, not a context peak.
  std::uint64_t allocated_staging_payload_bytes = 0;
  std::uint64_t allocated_staging_request_bytes = 0;
  std::uint64_t active_staging_payload_bytes = 0;
  std::uint64_t copied_buffer_bytes = 0;
  std::uint64_t copied_descriptor_bytes = 0;
  std::uint64_t byte_checkpoints = 0;
  std::uint64_t item_checkpoints = 0;
  std::uint64_t planned_jobs = 0;
  std::uint64_t launched_workers = 0;
  std::uint64_t joined_workers = 0;
  std::uint64_t caller_jobs = 0;
  bool preflight_stop_observed = false;
};
class RecordIngressRefusal final : public std::exception {
 public:
  const char* what() const noexcept override;
};
enum class RecordBufferKind {
  kBytes,
  kFloat32,
  kFloat64,
  kInt32,
  kUint8,
  kUint16,
  kUint32,
  kUint64
};
class SharedSurfaceIngress;
class RecordBufferId {
 public:
  RecordBufferId() noexcept = default;

 private:
  friend class SharedSurfaceIngress;
  const SharedSurfaceIngress* owner_ = nullptr;
  std::size_t ordinal_ = 0;
};

// Construct after Python operation/result fallback shells exist. All native
// capacity uses the supplied already capped bridge, whose initially unstopped
// operation token is linked only after complete record AND catalog preflight.
// This single owner/counter set is shared by record and raw catalog buffers.
// GIL required through preflight and destruction until sources are released.
class SharedSurfaceIngress {
 public:
  SharedSurfaceIngress(const RecordIngressLimits&, const RecordIngressWork&,
                       const geometry::ExecutionContext&,
                       const gltf_detail::AllocationContext& bridge,
                       RecordIngressObservation& reached);
  ~SharedSurfaceIngress();
  SharedSurfaceIngress(const SharedSurfaceIngress&) = delete;
  SharedSurfaceIngress& operator=(const SharedSurfaceIngress&) = delete;

  // One actual read export, with common checked metadata/extent/admission.
  // kBytes admits native one-byte B/b/c buffers; no numeric conversion occurs.
  RecordBufferId Acquire(nanobind::handle, RecordBufferKind, RecordIngressField,
                         std::optional<std::uint64_t> row,
                         std::stop_token external_stop);
  void AdmitAttributes(std::uint64_t count);
  void AdmitMetadata(std::uint64_t count, std::uint64_t attribute);
  void AdmitDescriptorBytes(std::uint64_t count, RecordIngressField,
                            std::optional<std::uint64_t> row);
  void PreflightCheckpoint(std::stop_token external_stop,
                           std::uint64_t bytes = 0);
  // After all record/catalog representation checks and before staging. Shares
  // the caller's token without allocating a second stop state or replacing the
  // admitted root/request cap. Preparation-time fixed-capacity allocators are
  // unchanged; subsequent runtime storage uses Bridge().
  void LinkStopToken(std::stop_token operation_stop);
  // GIL on entry/return. All source exports stay pinned while native copies
  // release the GIL. Actual helper joins precede expected or unexpected exits.
  void StageBuffers(const geometry::MeshMaterializationWorkPolicy&,
                    std::stop_token operation_stop,
                    const gltf_detail::NativeExecutionOptions& execution = {});
  std::span<const std::byte> Bytes(RecordBufferId) const;
  std::size_t Elements(RecordBufferId) const;
  void ReleasePythonSources();
  // Native readers of this buffer must all have joined. Other catalog/record
  // buffers remain intact. Invalidates Bytes(id), without changing work totals.
  void ReleaseBufferStorage(RecordBufferId);
  // Caller invokes only after record and catalog descriptors/copies finish.
  void Complete();
  void Checkpoint(std::stop_token operation_stop);
  [[noreturn]] void Refuse(RecordIngressStatus, RecordIngressField,
                           std::optional<std::uint64_t> row = {});
  // Returns false for arithmetic/invalid-object/unexpected native errors.
  // Root can use the same actual classification for its own catalog storage.
  bool ClassifyAllocation(const gltf_detail::AdmissionFailure&,
                          RecordIngressField,
                          std::optional<std::uint64_t> row = {});
  const gltf_detail::AllocationContext& Bridge() const noexcept;
  const RecordIngressLimits& Limits() const noexcept;
  RecordIngressObservation& Observation() noexcept;

 private:
  struct BufferSlot;
  RecordIngressLimits limits_;
  RecordIngressWork work_;
  geometry::ExecutionContext context_;
  gltf_detail::AllocationContext bridge_;
  RecordIngressObservation& reached_;
  std::vector<BufferSlot, gltf_detail::Allocator<BufferSlot>> buffers_;
  std::size_t count_ = 0;
  std::uint64_t preflight_items_ = 0;
  std::uint64_t preflight_bytes_ = 0;
  bool staged_ = false;
  bool staging_started_ = false;
};

// Stable native span owner for one mesh/1 record, using the shared ingress.
// Order: Preflight record + Root's catalog preflight; link stop; StageBuffers;
// CaptureDescriptors; release Python sources; Input; MaterializeMesh ONCE;
// destroy capture owners only after MaterializeMesh returns/throws and joins.
class SurfaceRecordCapture {
 public:
  explicit SurfaceRecordCapture(SharedSurfaceIngress&);
  ~SurfaceRecordCapture();
  SurfaceRecordCapture(const SurfaceRecordCapture&) = delete;
  SurfaceRecordCapture& operator=(const SurfaceRecordCapture&) = delete;
  void Preflight(nanobind::handle record, std::stop_token external_stop);
  void CaptureDescriptors(std::stop_token operation_stop);
  void ReleasePythonSources();
  // After MaterializeMesh has joined: releases this record's staging/text;
  // shared catalog buffers survive. No Python references may remain here.
  void ReleaseCapturedStorage();
  geometry::MeshMaterializationInput Input() const;

 private:
  struct AttributeRow;
  struct MetadataRow;
  SharedSurfaceIngress& ingress_;
  nanobind::object source_;
  RecordBufferId positions_, face_offsets_, corner_vertices_;
  std::vector<AttributeRow, gltf_detail::Allocator<AttributeRow>> attributes_;
  std::vector<MetadataRow, gltf_detail::Allocator<MetadataRow>> metadata_;
  std::vector<geometry::AttributeMaterializationInput,
              gltf_detail::Allocator<geometry::AttributeMaterializationInput>>
      views_;
  std::vector<geometry::MetadataEntryView,
              gltf_detail::Allocator<geometry::MetadataEntryView>>
      metadata_views_;
  void* text_ = nullptr;
  std::size_t attribute_count_ = 0;
  std::size_t metadata_count_ = 0;
  std::size_t text_bytes_ = 0;
  bool preflighted_ = false;
  bool captured_ = false;
  bool started_ = false;
  bool capture_started_ = false;
};

}  // namespace meshvale::interchange::surface_python_detail
#endif  // MESHVALE_INTERCHANGE_PYTHON_SURFACE_RECORD_CAPTURE_H_
