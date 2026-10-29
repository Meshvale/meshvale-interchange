// SPDX-License-Identifier: Apache-2.0
#include "gltf_numerical_profile.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#if defined(_MSC_VER) && defined(_M_X64)
#include <crtversion.h>
#include <xmmintrin.h>
#include <yvals.h>
#endif

#include "gltf_allocation.h"

namespace meshvale::interchange::gltf_detail {
namespace {
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER == 195136248 && defined(_M_X64) && \
    _M_X64 == 100 && defined(_M_FP_STRICT) && _M_FP_STRICT == 1 &&             \
    defined(_MSVC_LANG) && _MSVC_LANG == 202002L &&                            \
    defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 &&                  \
    defined(_MSVC_STL_UPDATE) && _MSVC_STL_UPDATE == 202604L &&                \
    defined(_VC_CRT_MAJOR_VERSION) && _VC_CRT_MAJOR_VERSION == 14 &&           \
    defined(_VC_CRT_MINOR_VERSION) && _VC_CRT_MINOR_VERSION == 51 &&           \
    defined(_VC_CRT_BUILD_VERSION) && _VC_CRT_BUILD_VERSION == 36244 &&        \
    defined(_VC_CRT_RBUILD_VERSION) && _VC_CRT_RBUILD_VERSION == 0 &&          \
    defined(_DLL) && _DLL == 1 && defined(_MT) && _MT == 1 &&                  \
    defined(NDEBUG) && !defined(_DEBUG) && defined(_ITERATOR_DEBUG_LEVEL) &&   \
    _ITERATOR_DEBUG_LEVEL == 0
constexpr std::uint32_t kSse2UnitBit = 1;
constexpr std::uint32_t kAllExceptionMasks = 63;
constexpr unsigned kExceptionMaskShift = 7;
constexpr std::uint32_t kRoundingMask = 3U << 13;
constexpr std::uint32_t kFlushToZero = 1U << 15;
constexpr std::uint32_t kDenormalsAreZero = 1U << 6;
constexpr std::string_view kCompiledProfile =
    "msvc-195136248-stl145-202604-crthead14.51.36244.0-md-release-it0-x64-sse2-"
    "strict-v1";
#endif

void Increment(std::uint64_t& value) {
  if (value == std::numeric_limits<std::uint64_t>::max())
    throw AdmissionFailure(AdmissionCode::kSizeLimit);
  ++value;
}
bool Precedes(std::optional<std::uint64_t> partition, bool caller,
              std::optional<std::uint64_t> previous_partition,
              bool previous_caller) {
  return partition.value_or(0) < previous_partition.value_or(0) ||
         (partition.value_or(0) == previous_partition.value_or(0) && caller &&
          !previous_caller);
}
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER == 195136248 && defined(_M_X64) && \
    _M_X64 == 100 && defined(_M_FP_STRICT) && _M_FP_STRICT == 1 &&             \
    defined(_MSVC_LANG) && _MSVC_LANG == 202002L &&                            \
    defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 &&                  \
    defined(_MSVC_STL_UPDATE) && _MSVC_STL_UPDATE == 202604L &&                \
    defined(_VC_CRT_MAJOR_VERSION) && _VC_CRT_MAJOR_VERSION == 14 &&           \
    defined(_VC_CRT_MINOR_VERSION) && _VC_CRT_MINOR_VERSION == 51 &&           \
    defined(_VC_CRT_BUILD_VERSION) && _VC_CRT_BUILD_VERSION == 36244 &&        \
    defined(_VC_CRT_RBUILD_VERSION) && _VC_CRT_RBUILD_VERSION == 0 &&          \
    defined(_DLL) && _DLL == 1 && defined(_MT) && _MT == 1 &&                  \
    defined(NDEBUG) && !defined(_DEBUG) && defined(_ITERATOR_DEBUG_LEVEL) &&   \
    _ITERATOR_DEBUG_LEVEL == 0
void MergeSample(GltfDeformationExceptionMasks& target,
                 const GltfDeformationExceptionMasks& sample) {
  target.required_units =
      target.required_units.value_or(0) | sample.required_units.value_or(0);
  if (!target.sample_caller ||
      Precedes(sample.sample_partition, sample.sample_caller.value_or(false),
               target.sample_partition, target.sample_caller.value_or(false))) {
    target.sse2_mask = sample.sse2_mask;
    target.x87_mask = sample.x87_mask;
    target.sample_partition = sample.sample_partition;
    target.sample_caller = sample.sample_caller;
  }
}
#endif
}  // namespace

void RecordGltfExactBlock(GltfDeformationNumericObservation& observation) {
  Increment(observation.exact_blocks);
  if (!observation.exception_masks.required_units)
    observation.exception_masks.required_units = 0;
}

bool EstablishGltfNumericalProfile(
    GltfDeformationNumericObservation& observation,
    std::optional<std::uint64_t> partition, bool caller) {
  Increment(observation.guard_calls);
  GltfDeformationNumericRefusal refusal{};
  refusal.partition = partition;
  refusal.caller = caller;
  refusal.reason = GltfDeformationNumericRefusalReason::kCompiledUnsupported;
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER == 195136248 && defined(_M_X64) && \
    _M_X64 == 100 && defined(_M_FP_STRICT) && _M_FP_STRICT == 1 &&             \
    defined(_MSVC_LANG) && _MSVC_LANG == 202002L &&                            \
    defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 &&                  \
    defined(_MSVC_STL_UPDATE) && _MSVC_STL_UPDATE == 202604L &&                \
    defined(_VC_CRT_MAJOR_VERSION) && _VC_CRT_MAJOR_VERSION == 14 &&           \
    defined(_VC_CRT_MINOR_VERSION) && _VC_CRT_MINOR_VERSION == 51 &&           \
    defined(_VC_CRT_BUILD_VERSION) && _VC_CRT_BUILD_VERSION == 36244 &&        \
    defined(_VC_CRT_RBUILD_VERSION) && _VC_CRT_RBUILD_VERSION == 0 &&          \
    defined(_DLL) && _DLL == 1 && defined(_MT) && _MT == 1 &&                  \
    defined(NDEBUG) && !defined(_DEBUG) && defined(_ITERATOR_DEBUG_LEVEL) &&   \
    _ITERATOR_DEBUG_LEVEL == 0
  if constexpr (std::numeric_limits<float>::is_iec559 &&
                std::numeric_limits<float>::digits == 24 &&
                sizeof(float) == sizeof(std::uint32_t) &&
                std::numeric_limits<double>::is_iec559 &&
                std::numeric_limits<double>::digits == 53 &&
                sizeof(double) == sizeof(std::uint64_t)) {
    observation.compiled_profile = kCompiledProfile;
    const auto control = static_cast<std::uint32_t>(_mm_getcsr());
    refusal.nearest_even = (control & kRoundingMask) == 0;
    refusal.ftz_disabled = (control & kFlushToZero) == 0;
    refusal.daz_disabled = (control & kDenormalsAreZero) == 0;
    refusal.unit = GltfDeformationFloatingUnit::kSse2;
    refusal.exception_masks.required_units = kSse2UnitBit;
    refusal.exception_masks.sse2_mask =
        (control >> kExceptionMaskShift) & kAllExceptionMasks;
    refusal.exception_masks.sample_partition = partition;
    refusal.exception_masks.sample_caller = caller;
    MergeSample(observation.exception_masks, refusal.exception_masks);
    if (!*refusal.nearest_even) {
      refusal.reason = GltfDeformationNumericRefusalReason::kRounding;
    } else if (!*refusal.ftz_disabled || !*refusal.daz_disabled) {
      refusal.reason = GltfDeformationNumericRefusalReason::kFlush;
    } else if (*refusal.exception_masks.sse2_mask != kAllExceptionMasks) {
      refusal.reason = GltfDeformationNumericRefusalReason::kExceptions;
    } else {
      Increment(observation.established_calls);
      return true;
    }
  }
#endif
  if (!observation.refusal ||
      Precedes(partition, caller, observation.refusal->partition,
               observation.refusal->caller))
    observation.refusal = refusal;
  return false;
}
}  // namespace meshvale::interchange::gltf_detail
