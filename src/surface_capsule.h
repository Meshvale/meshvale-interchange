// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_INTERCHANGE_SRC_SURFACE_CAPSULE_H_
#define MESHVALE_INTERCHANGE_SRC_SURFACE_CAPSULE_H_

#include <meshvale/interchange/surface_write.h>

#include <cstddef>
#include <span>

namespace meshvale::interchange::surface_detail {
struct Run;
struct OutputPayload;
struct Member;
struct CapsuleNumericClaims;
[[nodiscard]] SurfaceDigest Digest(std::span<const std::byte> bytes, Run& run);
// Copy only: source remains borrowed through all joins; destination is a
// fresh CapsuleJson Member using run.allocation. No proof or comparison work.
void CopyCapsuleJson(std::span<const std::byte> source, Member& destination,
                     Run& run);
void BuildCapsule(OutputPayload& output, Run& run);
// Parsed claims only. The core compares these with independently measured,
// completely retired V groups at Compare; this function creates no proof/facts.
[[nodiscard]] CapsuleNumericClaims InspectCapsule(
    const OutputPayload& expected, std::span<const SurfaceMemberView> captured,
    Run& run);
}  // namespace meshvale::interchange::surface_detail
#endif  // MESHVALE_INTERCHANGE_SRC_SURFACE_CAPSULE_H_
