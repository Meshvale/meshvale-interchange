// SPDX-License-Identifier: Apache-2.0
// Fixed source words, channel bytes and expected values for surface tests.
// Static borrowed storage only; callers materialize on their actual ledger.
#ifndef MESHVALE_INTERCHANGE_TESTS_SURFACE_FIXTURE_H_
#define MESHVALE_INTERCHANGE_TESTS_SURFACE_FIXTURE_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace meshvale::interchange::surface_fixture {
enum class Variant { kOriginal, kTranslated };
enum class MetadataRoute { kRetained, kLegacy };
using WordTriple64 = std::array<std::uint64_t, 3>;
using WordTriple32 = std::array<std::uint32_t, 3>;
using IndexTriple = std::array<geometry::index_t, 3>;
using FractionTriple = std::array<std::string_view, 3>;

inline constexpr std::array<WordTriple64, 12> kOriginalPositionWords = {{
    {0x3fc0000000000000ULL, 0xbfd0000000000000ULL, 0x3fd8000000000000ULL},
    {0x3ff2000000000000ULL, 0x3fd0000000000000ULL, 0x3fc0000000000000ULL},
    {0xbfd8000000000000ULL, 0x3fe8000000000000ULL, 0x3ff2000000000000ULL},
    {0xbfec000000000000ULL, 0x3ffc000000000000ULL, 0x3ffe000000000000ULL},
    {0x3fc0000000000000ULL, 0x4002000000000000ULL, 0x3ffa000000000000ULL},
    {0x3fe4000000000000ULL, 0x3ff4000000000000ULL, 0x3fec000000000000ULL},
    {0xbfd8000000000000ULL, 0x3fe8000000000000ULL, 0x3ff2000000000000ULL},
    {0x3ff2000000000000ULL, 0x3ff8000000000000ULL, 0x3fe8000000000000ULL},
    {0x3fd8000000000000ULL, 0x4008000000000000ULL, 0x3ffe000000000000ULL},
    {0xbfc0000000000000ULL, 0x3ff8000000000000ULL, 0x3ff6000000000000ULL},
    {0xbff2000000000000ULL, 0x4002000000000000ULL, 0x4002000000000000ULL},
    {0x0000000000000000ULL, 0x8000000000000000ULL, 0x3fe2000000000000ULL},
}};
inline constexpr std::array<double, 36> kOriginalPositions = {{
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfec000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffc000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffe000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4002000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffa000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe4000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff4000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fec000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffe000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff6000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4002000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4002000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe2000000000000ULL}),
}};
inline constexpr std::array<WordTriple64, 12> kTranslatedPositionWords = {{
    {0x3fc0000002000000ULL, 0xbfd0000000000000ULL, 0x3fd8000000000000ULL},
    {0x3ff2000000400000ULL, 0x3fd0000000000000ULL, 0x3fc0000000000000ULL},
    {0xbfd7ffffff000000ULL, 0x3fe8000000000000ULL, 0x3ff2000000000000ULL},
    {0xbfebffffff800000ULL, 0x3ffc000000000000ULL, 0x3ffe000000000000ULL},
    {0x3fc0000002000000ULL, 0x4002000000000000ULL, 0x3ffa000000000000ULL},
    {0x3fe4000000800000ULL, 0x3ff4000000000000ULL, 0x3fec000000000000ULL},
    {0xbfd7ffffff000000ULL, 0x3fe8000000000000ULL, 0x3ff2000000000000ULL},
    {0x3ff2000000400000ULL, 0x3ff8000000000000ULL, 0x3fe8000000000000ULL},
    {0x3fd8000001000000ULL, 0x4008000000000000ULL, 0x3ffe000000000000ULL},
    {0xbfbffffffc000000ULL, 0x3ff8000000000000ULL, 0x3ff6000000000000ULL},
    {0xbff1ffffffc00000ULL, 0x4002000000000000ULL, 0x4002000000000000ULL},
    {0x3e10000000000000ULL, 0x8000000000000000ULL, 0x3fe2000000000000ULL},
}};
inline constexpr std::array<double, 36> kTranslatedPositions = {{
    std::bit_cast<double>(std::uint64_t{0x3fc0000002000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000400000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd7ffffff000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfebffffff800000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffc000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffe000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000002000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4002000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffa000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe4000000800000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff4000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fec000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd7ffffff000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000400000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd8000001000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ffe000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfbffffffc000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff6000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbff1ffffffc00000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4002000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4002000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3e10000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe2000000000000ULL}),
}};
inline constexpr std::array<geometry::index_t, 4> kFaceOffsets = {{
    0x0000000000000000ULL,
    0x0000000000000003ULL,
    0x0000000000000007ULL,
    0x000000000000000cULL,
}};
inline constexpr std::array<geometry::index_t, 12> kCornerVertices = {{
    0x0000000000000000ULL,
    0x0000000000000001ULL,
    0x0000000000000002ULL,
    0x0000000000000002ULL,
    0x0000000000000003ULL,
    0x0000000000000004ULL,
    0x0000000000000005ULL,
    0x0000000000000006ULL,
    0x0000000000000007ULL,
    0x0000000000000008ULL,
    0x0000000000000009ULL,
    0x000000000000000aULL,
}};
inline constexpr std::array<float, 24> kAttribute00Values = {{
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x80000000U}),
    std::bit_cast<float>(std::uint32_t{0x7fc12345U}),
    std::bit_cast<float>(std::uint32_t{0xff800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f000000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x7f800000U}),
    std::bit_cast<float>(std::uint32_t{0xffc56789U}),
    std::bit_cast<float>(std::uint32_t{0x00000001U}),
    std::bit_cast<float>(std::uint32_t{0x80000001U}),
    std::bit_cast<float>(std::uint32_t{0x3e800000U}),
    std::bit_cast<float>(std::uint32_t{0xc0200000U}),
    std::bit_cast<float>(std::uint32_t{0x40800000U}),
    std::bit_cast<float>(std::uint32_t{0xc0800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800001U}),
    std::bit_cast<float>(std::uint32_t{0xbf800001U}),
    std::bit_cast<float>(std::uint32_t{0x00800000U}),
    std::bit_cast<float>(std::uint32_t{0x80800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0xc0000000U}),
    std::bit_cast<float>(std::uint32_t{0x41200000U}),
    std::bit_cast<float>(std::uint32_t{0xc1200000U}),
    std::bit_cast<float>(std::uint32_t{0x3d800000U}),
    std::bit_cast<float>(std::uint32_t{0xbd800000U}),
}};
inline constexpr std::array<std::byte, 96> kAttribute00ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x45U}, std::byte{0x23U}, std::byte{0xc1U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x7fU},
    std::byte{0x89U}, std::byte{0x67U}, std::byte{0xc5U}, std::byte{0xffU},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0xc0U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xc0U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x41U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0xc1U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3dU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbdU},
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute00Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<double, 6> kAttribute01Values = {{
    std::bit_cast<double>(std::uint64_t{0x7ff8000000004321ULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xfff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xfff8000000009876ULL}),
}};
inline constexpr std::array<std::byte, 48> kAttribute01ExpectedBytes = {{
    std::byte{0x21U}, std::byte{0x43U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0x7fU},
    std::byte{0x76U}, std::byte{0x98U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0xffU},
}};
inline constexpr std::array<std::uint8_t, 3> kAttribute01Presence = {{
    0x01U,
    0x00U,
    0x01U,
}};
inline constexpr std::array<std::int32_t, 12> kAttribute02Values = {{
    std::bit_cast<std::int32_t>(std::uint32_t{0x80000000U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x7fffffffU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xfffffff9U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x00000000U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x0000000bU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xffffffedU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x00000017U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xffffffe3U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x0000001fU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xffffffdbU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x00000029U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xffffffd5U}),
}};
inline constexpr std::array<std::byte, 48> kAttribute02ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0x7fU},
    std::byte{0xf9U}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x0bU}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xedU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x17U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xe3U}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x1fU}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xdbU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x29U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xd5U}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute02Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute03Values = {{
    std::uint8_t{0x00U},
    std::uint8_t{0xffU},
    std::uint8_t{0x11U},
    std::uint8_t{0x40U},
    std::uint8_t{0x80U},
    std::uint8_t{0x03U},
    std::uint8_t{0xdbU},
    std::uint8_t{0x08U},
    std::uint8_t{0x5bU},
    std::uint8_t{0x02U},
    std::uint8_t{0x07U},
    std::uint8_t{0xc9U},
}};
inline constexpr std::array<std::byte, 12> kAttribute03ExpectedBytes = {{
    std::byte{0x00U},
    std::byte{0xffU},
    std::byte{0x11U},
    std::byte{0x40U},
    std::byte{0x80U},
    std::byte{0x03U},
    std::byte{0xdbU},
    std::byte{0x08U},
    std::byte{0x5bU},
    std::byte{0x02U},
    std::byte{0x07U},
    std::byte{0xc9U},
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute03Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<std::uint16_t, 6> kAttribute04Values = {{
    std::uint16_t{0x0000U},
    std::uint16_t{0xffffU},
    std::uint16_t{0x0013U},
    std::uint16_t{0x8000U},
    std::uint16_t{0x03ffU},
    std::uint16_t{0x1001U},
}};
inline constexpr std::array<std::byte, 12> kAttribute04ExpectedBytes = {{
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0xffU},
    std::byte{0xffU},
    std::byte{0x13U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x80U},
    std::byte{0xffU},
    std::byte{0x03U},
    std::byte{0x01U},
    std::byte{0x10U},
}};
inline constexpr std::array<std::uint8_t, 3> kAttribute04Presence = {{
    0x01U,
    0x00U,
    0x01U,
}};
inline constexpr std::array<std::uint32_t, 12> kAttribute05Values = {{
    std::uint32_t{0x00000000U},
    std::uint32_t{0xffffffffU},
    std::uint32_t{0x80000000U},
    std::uint32_t{0x01020304U},
    std::uint32_t{0x00000011U},
    std::uint32_t{0x00000101U},
    std::uint32_t{0x00010001U},
    std::uint32_t{0xabcdef01U},
    std::uint32_t{0x00000007U},
    std::uint32_t{0x11223344U},
    std::uint32_t{0x0000001fU},
    std::uint32_t{0xfffffffeU},
}};
inline constexpr std::array<std::byte, 48> kAttribute05ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0x11U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0xefU}, std::byte{0xcdU}, std::byte{0xabU},
    std::byte{0x07U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x44U}, std::byte{0x33U}, std::byte{0x22U}, std::byte{0x11U},
    std::byte{0x1fU}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xfeU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute05Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<std::uint64_t, 12> kAttribute06Values = {{
    std::uint64_t{0x0000000000000000ULL},
    std::uint64_t{0xffffffffffffffffULL},
    std::uint64_t{0x8000000000000001ULL},
    std::uint64_t{0x0020000000000001ULL},
    std::uint64_t{0x0000000000000011ULL},
    std::uint64_t{0x0102030405060708ULL},
    std::uint64_t{0x7fffffffffffffffULL},
    std::uint64_t{0xdeadbeef01020304ULL},
    std::uint64_t{0x0000000100000000ULL},
    std::uint64_t{0x00000000ffffffffULL},
    std::uint64_t{0x8000000000000000ULL},
    std::uint64_t{0x0000000000000061ULL},
}};
inline constexpr std::array<std::byte, 96> kAttribute06ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x00U},
    std::byte{0x11U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x08U}, std::byte{0x07U}, std::byte{0x06U}, std::byte{0x05U},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0x7fU},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0xefU}, std::byte{0xbeU}, std::byte{0xadU}, std::byte{0xdeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x61U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute06Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<float, 30> kAttribute07Values = {{
    std::bit_cast<float>(std::uint32_t{0x7fc24681U}),
    std::bit_cast<float>(std::uint32_t{0x80000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x7f800000U}),
    std::bit_cast<float>(std::uint32_t{0xff800000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0xbe800000U}),
    std::bit_cast<float>(std::uint32_t{0x7fc24681U}),
    std::bit_cast<float>(std::uint32_t{0x80000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x7f800000U}),
    std::bit_cast<float>(std::uint32_t{0xff800000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0xbe800000U}),
    std::bit_cast<float>(std::uint32_t{0x7fc24681U}),
    std::bit_cast<float>(std::uint32_t{0x80000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x7f800000U}),
    std::bit_cast<float>(std::uint32_t{0xff800000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0xbe800000U}),
    std::bit_cast<float>(std::uint32_t{0x7fc24681U}),
    std::bit_cast<float>(std::uint32_t{0x80000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x7f800000U}),
    std::bit_cast<float>(std::uint32_t{0xff800000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0xbe800000U}),
    std::bit_cast<float>(std::uint32_t{0x7fc24681U}),
    std::bit_cast<float>(std::uint32_t{0x80000000U}),
}};
inline constexpr std::array<std::byte, 120> kAttribute07ExpectedBytes = {{
    std::byte{0x81U}, std::byte{0x46U}, std::byte{0xc2U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbeU},
    std::byte{0x81U}, std::byte{0x46U}, std::byte{0xc2U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbeU},
    std::byte{0x81U}, std::byte{0x46U}, std::byte{0xc2U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbeU},
    std::byte{0x81U}, std::byte{0x46U}, std::byte{0xc2U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbeU},
    std::byte{0x81U}, std::byte{0x46U}, std::byte{0xc2U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
}};
inline constexpr std::array<geometry::index_t, 13> kAttribute07Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
    0x0000000000000009ULL,
    0x0000000000000009ULL,
    0x000000000000000eULL,
    0x0000000000000010ULL,
    0x0000000000000015ULL,
    0x0000000000000015ULL,
    0x0000000000000017ULL,
    0x000000000000001cULL,
    0x000000000000001eULL,
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute07Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<double, 30> kAttribute08Values = {{
    std::bit_cast<double>(std::uint64_t{0x7ff80000000abcdeULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xfff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff80000000abcdeULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xfff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff80000000abcdeULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xfff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff80000000abcdeULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xfff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x7ff80000000abcdeULL}),
    std::bit_cast<double>(std::uint64_t{0x8000000000000000ULL}),
}};
inline constexpr std::array<std::byte, 240> kAttribute08ExpectedBytes = {{
    std::byte{0xdeU}, std::byte{0xbcU}, std::byte{0x0aU}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbfU},
    std::byte{0xdeU}, std::byte{0xbcU}, std::byte{0x0aU}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbfU},
    std::byte{0xdeU}, std::byte{0xbcU}, std::byte{0x0aU}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbfU},
    std::byte{0xdeU}, std::byte{0xbcU}, std::byte{0x0aU}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf0U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbfU},
    std::byte{0xdeU}, std::byte{0xbcU}, std::byte{0x0aU}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xf8U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
}};
inline constexpr std::array<geometry::index_t, 13> kAttribute08Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
    0x0000000000000009ULL,
    0x0000000000000009ULL,
    0x000000000000000eULL,
    0x0000000000000010ULL,
    0x0000000000000015ULL,
    0x0000000000000015ULL,
    0x0000000000000017ULL,
    0x000000000000001cULL,
    0x000000000000001eULL,
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute08Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<std::int32_t, 7> kAttribute09Values = {{
    std::bit_cast<std::int32_t>(std::uint32_t{0x80000000U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x00000000U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x7fffffffU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xfffffffbU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x0000000dU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xffffffebU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x00000022U}),
}};
inline constexpr std::array<std::byte, 28> kAttribute09ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0x7fU},
    std::byte{0xfbU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x0dU}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0xebU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x22U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
}};
inline constexpr std::array<geometry::index_t, 4> kAttribute09Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
}};
inline constexpr std::array<std::uint8_t, 3> kAttribute09Presence = {{
    0x01U,
    0x00U,
    0x01U,
}};
inline constexpr std::array<std::uint8_t, 30> kAttribute10Values = {{
    std::uint8_t{0xffU}, std::uint8_t{0x00U}, std::uint8_t{0x07U},
    std::uint8_t{0x80U}, std::uint8_t{0x13U}, std::uint8_t{0xc9U},
    std::uint8_t{0x03U}, std::uint8_t{0xffU}, std::uint8_t{0x00U},
    std::uint8_t{0x07U}, std::uint8_t{0x80U}, std::uint8_t{0x13U},
    std::uint8_t{0xc9U}, std::uint8_t{0x03U}, std::uint8_t{0xffU},
    std::uint8_t{0x00U}, std::uint8_t{0x07U}, std::uint8_t{0x80U},
    std::uint8_t{0x13U}, std::uint8_t{0xc9U}, std::uint8_t{0x03U},
    std::uint8_t{0xffU}, std::uint8_t{0x00U}, std::uint8_t{0x07U},
    std::uint8_t{0x80U}, std::uint8_t{0x13U}, std::uint8_t{0xc9U},
    std::uint8_t{0x03U}, std::uint8_t{0xffU}, std::uint8_t{0x00U},
}};
inline constexpr std::array<std::byte, 30> kAttribute10ExpectedBytes = {{
    std::byte{0xffU}, std::byte{0x00U}, std::byte{0x07U}, std::byte{0x80U},
    std::byte{0x13U}, std::byte{0xc9U}, std::byte{0x03U}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x07U}, std::byte{0x80U}, std::byte{0x13U},
    std::byte{0xc9U}, std::byte{0x03U}, std::byte{0xffU}, std::byte{0x00U},
    std::byte{0x07U}, std::byte{0x80U}, std::byte{0x13U}, std::byte{0xc9U},
    std::byte{0x03U}, std::byte{0xffU}, std::byte{0x00U}, std::byte{0x07U},
    std::byte{0x80U}, std::byte{0x13U}, std::byte{0xc9U}, std::byte{0x03U},
    std::byte{0xffU}, std::byte{0x00U},
}};
inline constexpr std::array<geometry::index_t, 13> kAttribute10Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
    0x0000000000000009ULL,
    0x0000000000000009ULL,
    0x000000000000000eULL,
    0x0000000000000010ULL,
    0x0000000000000015ULL,
    0x0000000000000015ULL,
    0x0000000000000017ULL,
    0x000000000000001cULL,
    0x000000000000001eULL,
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute10Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<std::uint16_t, 30> kAttribute11Values = {{
    std::uint16_t{0xffffU}, std::uint16_t{0x0000U}, std::uint16_t{0x8000U},
    std::uint16_t{0x0005U}, std::uint16_t{0x0101U}, std::uint16_t{0x1003U},
    std::uint16_t{0x000bU}, std::uint16_t{0xffffU}, std::uint16_t{0x0000U},
    std::uint16_t{0x8000U}, std::uint16_t{0x0005U}, std::uint16_t{0x0101U},
    std::uint16_t{0x1003U}, std::uint16_t{0x000bU}, std::uint16_t{0xffffU},
    std::uint16_t{0x0000U}, std::uint16_t{0x8000U}, std::uint16_t{0x0005U},
    std::uint16_t{0x0101U}, std::uint16_t{0x1003U}, std::uint16_t{0x000bU},
    std::uint16_t{0xffffU}, std::uint16_t{0x0000U}, std::uint16_t{0x8000U},
    std::uint16_t{0x0005U}, std::uint16_t{0x0101U}, std::uint16_t{0x1003U},
    std::uint16_t{0x000bU}, std::uint16_t{0xffffU}, std::uint16_t{0x0000U},
}};
inline constexpr std::array<std::byte, 60> kAttribute11ExpectedBytes = {{
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x80U}, std::byte{0x05U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x01U}, std::byte{0x03U}, std::byte{0x10U},
    std::byte{0x0bU}, std::byte{0x00U}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x01U},
    std::byte{0x03U}, std::byte{0x10U}, std::byte{0x0bU}, std::byte{0x00U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x80U}, std::byte{0x05U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x01U}, std::byte{0x03U}, std::byte{0x10U},
    std::byte{0x0bU}, std::byte{0x00U}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x01U},
    std::byte{0x03U}, std::byte{0x10U}, std::byte{0x0bU}, std::byte{0x00U},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0x00U}, std::byte{0x00U},
}};
inline constexpr std::array<geometry::index_t, 13> kAttribute11Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
    0x0000000000000009ULL,
    0x0000000000000009ULL,
    0x000000000000000eULL,
    0x0000000000000010ULL,
    0x0000000000000015ULL,
    0x0000000000000015ULL,
    0x0000000000000017ULL,
    0x000000000000001cULL,
    0x000000000000001eULL,
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute11Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<std::uint32_t, 7> kAttribute12Values = {{
    std::uint32_t{0xffffffffU},
    std::uint32_t{0x00000000U},
    std::uint32_t{0x80000000U},
    std::uint32_t{0x00000005U},
    std::uint32_t{0x12345678U},
    std::uint32_t{0xfffffffeU},
    std::uint32_t{0x00010001U},
}};
inline constexpr std::array<std::byte, 28> kAttribute12ExpectedBytes = {{
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x78U}, std::byte{0x56U}, std::byte{0x34U}, std::byte{0x12U},
    std::byte{0xfeU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x01U}, std::byte{0x00U},
}};
inline constexpr std::array<geometry::index_t, 4> kAttribute12Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
}};
inline constexpr std::array<std::uint8_t, 3> kAttribute12Presence = {{
    0x01U,
    0x00U,
    0x01U,
}};
inline constexpr std::array<std::uint64_t, 30> kAttribute13Values = {{
    std::uint64_t{0xffffffffffffffffULL}, std::uint64_t{0x0000000000000000ULL},
    std::uint64_t{0x8000000000000001ULL}, std::uint64_t{0x0020000000000001ULL},
    std::uint64_t{0x0000000000000005ULL}, std::uint64_t{0x1122334455667788ULL},
    std::uint64_t{0xdeadbeefcafef00dULL}, std::uint64_t{0xffffffffffffffffULL},
    std::uint64_t{0x0000000000000000ULL}, std::uint64_t{0x8000000000000001ULL},
    std::uint64_t{0x0020000000000001ULL}, std::uint64_t{0x0000000000000005ULL},
    std::uint64_t{0x1122334455667788ULL}, std::uint64_t{0xdeadbeefcafef00dULL},
    std::uint64_t{0xffffffffffffffffULL}, std::uint64_t{0x0000000000000000ULL},
    std::uint64_t{0x8000000000000001ULL}, std::uint64_t{0x0020000000000001ULL},
    std::uint64_t{0x0000000000000005ULL}, std::uint64_t{0x1122334455667788ULL},
    std::uint64_t{0xdeadbeefcafef00dULL}, std::uint64_t{0xffffffffffffffffULL},
    std::uint64_t{0x0000000000000000ULL}, std::uint64_t{0x8000000000000001ULL},
    std::uint64_t{0x0020000000000001ULL}, std::uint64_t{0x0000000000000005ULL},
    std::uint64_t{0x1122334455667788ULL}, std::uint64_t{0xdeadbeefcafef00dULL},
    std::uint64_t{0xffffffffffffffffULL}, std::uint64_t{0x0000000000000000ULL},
}};
inline constexpr std::array<std::byte, 240> kAttribute13ExpectedBytes = {{
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x00U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x88U}, std::byte{0x77U}, std::byte{0x66U}, std::byte{0x55U},
    std::byte{0x44U}, std::byte{0x33U}, std::byte{0x22U}, std::byte{0x11U},
    std::byte{0x0dU}, std::byte{0xf0U}, std::byte{0xfeU}, std::byte{0xcaU},
    std::byte{0xefU}, std::byte{0xbeU}, std::byte{0xadU}, std::byte{0xdeU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x00U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x88U}, std::byte{0x77U}, std::byte{0x66U}, std::byte{0x55U},
    std::byte{0x44U}, std::byte{0x33U}, std::byte{0x22U}, std::byte{0x11U},
    std::byte{0x0dU}, std::byte{0xf0U}, std::byte{0xfeU}, std::byte{0xcaU},
    std::byte{0xefU}, std::byte{0xbeU}, std::byte{0xadU}, std::byte{0xdeU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x00U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x88U}, std::byte{0x77U}, std::byte{0x66U}, std::byte{0x55U},
    std::byte{0x44U}, std::byte{0x33U}, std::byte{0x22U}, std::byte{0x11U},
    std::byte{0x0dU}, std::byte{0xf0U}, std::byte{0xfeU}, std::byte{0xcaU},
    std::byte{0xefU}, std::byte{0xbeU}, std::byte{0xadU}, std::byte{0xdeU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x00U},
    std::byte{0x05U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x88U}, std::byte{0x77U}, std::byte{0x66U}, std::byte{0x55U},
    std::byte{0x44U}, std::byte{0x33U}, std::byte{0x22U}, std::byte{0x11U},
    std::byte{0x0dU}, std::byte{0xf0U}, std::byte{0xfeU}, std::byte{0xcaU},
    std::byte{0xefU}, std::byte{0xbeU}, std::byte{0xadU}, std::byte{0xdeU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
}};
inline constexpr std::array<geometry::index_t, 13> kAttribute13Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000007ULL,
    0x0000000000000009ULL,
    0x0000000000000009ULL,
    0x000000000000000eULL,
    0x0000000000000010ULL,
    0x0000000000000015ULL,
    0x0000000000000015ULL,
    0x0000000000000017ULL,
    0x000000000000001cULL,
    0x000000000000001eULL,
}};
inline constexpr std::array<std::uint8_t, 12> kAttribute13Presence = {{
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
    0x00U,
    0x01U,
    0x01U,
}};
inline constexpr std::array<float, 36> kAttribute14Values = {{
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xc0000000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xc0000000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xc0000000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xc0000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
}};
inline constexpr std::array<std::byte, 144> kAttribute14ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
}};
inline constexpr std::array<float, 24> kAttribute15Values = {{
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0x3e800000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0x3e800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f600000U}),
    std::bit_cast<float>(std::uint32_t{0x3f400000U}),
    std::bit_cast<float>(std::uint32_t{0x3f600000U}),
    std::bit_cast<float>(std::uint32_t{0x3f400000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0xbf000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0x3f000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
    std::bit_cast<float>(std::uint32_t{0x00000000U}),
    std::bit_cast<float>(std::uint32_t{0x3f800000U}),
    std::bit_cast<float>(std::uint32_t{0xbf000000U}),
    std::bit_cast<float>(std::uint32_t{0x40000000U}),
}};
inline constexpr std::array<std::byte, 96> kAttribute15ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x60U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x60U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
}};
inline constexpr std::array<double, 24> kAttribute16Values = {{
    std::bit_cast<double>(std::uint64_t{0x3fb0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fb0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd4000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fdc000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fdc000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfc8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfb0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfb0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfea000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fb0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd4000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfc8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfea000000000000ULL}),
}};
inline constexpr std::array<std::byte, 192> kAttribute16ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xb0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xe2U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xb0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd4U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xdcU}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xdcU}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xb0U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xe2U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xb0U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xe2U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xeaU}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xb0U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd4U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0xbfU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xeaU}, std::byte{0xbfU},
}};
inline constexpr std::array<float, 24> kAttribute17Values = {{
    std::bit_cast<float>(std::uint32_t{0x3d800000U}),
    std::bit_cast<float>(std::uint32_t{0xbdc00000U}),
    std::bit_cast<float>(std::uint32_t{0x3e000000U}),
    std::bit_cast<float>(std::uint32_t{0xbe000000U}),
    std::bit_cast<float>(std::uint32_t{0x3e400000U}),
    std::bit_cast<float>(std::uint32_t{0xbe200000U}),
    std::bit_cast<float>(std::uint32_t{0x3e800000U}),
    std::bit_cast<float>(std::uint32_t{0xbe400000U}),
    std::bit_cast<float>(std::uint32_t{0x3ea00000U}),
    std::bit_cast<float>(std::uint32_t{0xbe600000U}),
    std::bit_cast<float>(std::uint32_t{0x3ec00000U}),
    std::bit_cast<float>(std::uint32_t{0xbe800000U}),
    std::bit_cast<float>(std::uint32_t{0x3ee00000U}),
    std::bit_cast<float>(std::uint32_t{0xbe900000U}),
    std::bit_cast<float>(std::uint32_t{0x3f000000U}),
    std::bit_cast<float>(std::uint32_t{0xbea00000U}),
    std::bit_cast<float>(std::uint32_t{0x3f100000U}),
    std::bit_cast<float>(std::uint32_t{0xbeb00000U}),
    std::bit_cast<float>(std::uint32_t{0x3f200000U}),
    std::bit_cast<float>(std::uint32_t{0xbec00000U}),
    std::bit_cast<float>(std::uint32_t{0x3f300000U}),
    std::bit_cast<float>(std::uint32_t{0xbed00000U}),
    std::bit_cast<float>(std::uint32_t{0x3f400000U}),
    std::bit_cast<float>(std::uint32_t{0xbee00000U}),
}};
inline constexpr std::array<std::byte, 96> kAttribute17ExpectedBytes = {{
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3dU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0xbdU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xa0U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x60U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xe0U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x90U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xa0U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x10U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xb0U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x30U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xd0U}, std::byte{0xbeU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xe0U}, std::byte{0xbeU},
}};
inline constexpr std::array<std::int32_t, 3> kAttribute18Values = {{
    std::bit_cast<std::int32_t>(std::uint32_t{0x00000011U}),
    std::bit_cast<std::int32_t>(std::uint32_t{0xfffffffcU}),
    std::bit_cast<std::int32_t>(std::uint32_t{0x000f4243U}),
}};
inline constexpr std::array<std::byte, 12> kAttribute18ExpectedBytes = {{
    std::byte{0x11U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0x00U},
    std::byte{0xfcU},
    std::byte{0xffU},
    std::byte{0xffU},
    std::byte{0xffU},
    std::byte{0x43U},
    std::byte{0x42U},
    std::byte{0x0fU},
    std::byte{0x00U},
}};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute00Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute01Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute02Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute03Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute04Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute05Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute06Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute07Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute08Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute09Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute10Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute11Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute12Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute13Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute14Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute15Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute16Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute17Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kRetainedAttribute18Metadata = {{
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
    }};
inline constexpr std::array<geometry::AttributeMaterializationInput, 19>
    kRetainedAttributes = {{
        {geometry::AttributeDomain::vertex,
         std::string_view{
             "\x64\x65\x6e\x73\x65\x5f\x66\x6c\x6f\x61\x74\x33\x32", 13},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 2U,
         std::span<const float>{kAttribute00Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute00Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute00Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x66\x6c\x6f\x61\x74\x36\x34"
                          "\x00\x6e\x61\x6d\x65",
                          18},
         std::string_view{
             "\x4f\x50\x41\x51\x55\x45\x00\x70\x61\x79\x6c\x6f\x61\x64", 14},
         std::nullopt, 2U, std::span<const double>{kAttribute01Values},
         std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute01Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute01Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x69\x6e\x74\x33\x32", 11},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::int32_t>{kAttribute02Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute02Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute02Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x38", 11},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::uint8_t>{kAttribute03Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute03Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute03Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x31\x36",
                          12},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 2U,
         std::span<const std::uint16_t>{kAttribute04Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute04Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute04Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x33\x32",
                          12},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::uint32_t>{kAttribute05Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute05Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute05Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x36\x34",
                          12},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::uint64_t>{kAttribute06Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute06Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute06Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x66\x6c\x6f\x61\x74\x33\x32", 14},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const float>{kAttribute07Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute07Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute07Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute07Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x66\x6c\x6f\x61\x74\x36\x34", 14},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const double>{kAttribute08Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute08Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute08Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute08Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x72\x61\x67\x67\x65\x64\x5f\x69\x6e\x74\x33\x32",
                          12},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::int32_t>{kAttribute09Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute09Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute09Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute09Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x38",
                          12},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint8_t>{kAttribute10Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute10Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute10Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute10Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x31\x36", 13},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint16_t>{kAttribute11Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute11Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute11Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute11Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x33\x32", 13},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint32_t>{kAttribute12Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute12Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute12Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute12Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x36\x34", 13},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint64_t>{kAttribute13Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute13Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute13Presence}},
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute13Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{
             "\x63\x6f\x72\x6e\x65\x72\x5f\x6e\x6f\x72\x6d\x61\x6c\x73", 14},
         std::string_view{"\x4e\x4f\x52\x4d\x41\x4c", 6}, std::nullopt, 3U,
         std::span<const float>{kAttribute14Values}, std::nullopt, std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute14Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x75\x76\x5f\x30", 4},
         std::string_view{"\x54\x45\x58\x43\x4f\x4f\x52\x44", 8},
         std::optional<std::uint32_t>{0U}, 2U,
         std::span<const float>{kAttribute15Values}, std::nullopt, std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute15Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x75\x76\x5f\x32", 4},
         std::string_view{"\x54\x45\x58\x43\x4f\x4f\x52\x44", 8},
         std::optional<std::uint32_t>{2U}, 2U,
         std::span<const double>{kAttribute16Values}, std::nullopt,
         std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute16Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x75\x76\x5f\x35", 4},
         std::string_view{"\x54\x45\x58\x43\x4f\x4f\x52\x44", 8},
         std::optional<std::uint32_t>{5U}, 2U,
         std::span<const float>{kAttribute17Values}, std::nullopt, std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute17Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x6d\x61\x74\x65\x72\x69\x61\x6c\x5f\x69\x64\x73",
                          12},
         std::string_view{"\x4d\x41\x54\x45\x52\x49\x41\x4c", 8}, std::nullopt,
         1U, std::span<const std::int32_t>{kAttribute18Values}, std::nullopt,
         std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kRetainedAttribute18Metadata}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute00Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute01Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute02Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute03Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute04Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute05Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute06Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute07Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute08Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute09Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute10Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute11Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute12Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute13Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute14Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute15Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute16Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute17Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::MetadataEntryView, 3>
    kLegacyAttribute18Metadata = {{
        {std::string_view{"\x61\x6c\x70\x68\x61\x00\x6b\x65\x79", 9},
         std::string_view{"\x68\x65\x61\x64", 4}},
        {std::string_view{"\x6d\x69\x64\x64\x6c\x65", 6},
         std::string_view{"\xce\xa9", 2}},
        {std::string_view{"\x7a\x65\x74\x61", 4},
         std::string_view{"\x74\x61\x69\x6c\x00\x76", 6}},
    }};
inline constexpr std::array<geometry::AttributeMaterializationInput, 19>
    kLegacyAttributes = {{
        {geometry::AttributeDomain::vertex,
         std::string_view{
             "\x64\x65\x6e\x73\x65\x5f\x66\x6c\x6f\x61\x74\x33\x32", 13},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 2U,
         std::span<const float>{kAttribute00Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute00Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute00Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x66\x6c\x6f\x61\x74\x36\x34"
                          "\x00\x6e\x61\x6d\x65",
                          18},
         std::string_view{
             "\x4f\x50\x41\x51\x55\x45\x00\x70\x61\x79\x6c\x6f\x61\x64", 14},
         std::nullopt, 2U, std::span<const double>{kAttribute01Values},
         std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute01Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute01Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x69\x6e\x74\x33\x32", 11},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::int32_t>{kAttribute02Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute02Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute02Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x38", 11},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::uint8_t>{kAttribute03Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute03Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute03Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x31\x36",
                          12},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 2U,
         std::span<const std::uint16_t>{kAttribute04Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute04Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute04Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x33\x32",
                          12},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::uint32_t>{kAttribute05Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute05Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute05Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{"\x64\x65\x6e\x73\x65\x5f\x75\x69\x6e\x74\x36\x34",
                          12},
         std::string_view{"\x4f\x50\x41\x51\x55\x45", 6}, std::nullopt, 1U,
         std::span<const std::uint64_t>{kAttribute06Values}, std::nullopt,
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute06Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute06Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x66\x6c\x6f\x61\x74\x33\x32", 14},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const float>{kAttribute07Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute07Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute07Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute07Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x66\x6c\x6f\x61\x74\x36\x34", 14},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const double>{kAttribute08Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute08Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute08Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute08Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x72\x61\x67\x67\x65\x64\x5f\x69\x6e\x74\x33\x32",
                          12},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::int32_t>{kAttribute09Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute09Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute09Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute09Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x38",
                          12},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint8_t>{kAttribute10Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute10Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute10Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute10Metadata}},
        {geometry::AttributeDomain::vertex,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x31\x36", 13},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint16_t>{kAttribute11Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute11Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute11Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute11Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x33\x32", 13},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint32_t>{kAttribute12Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute12Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute12Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute12Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{
             "\x72\x61\x67\x67\x65\x64\x5f\x75\x69\x6e\x74\x36\x34", 13},
         std::string_view{
             "\x43\x55\x53\x54\x4f\x4d\x5f\x52\x41\x47\x47\x45\x44", 13},
         std::nullopt, 1U, std::span<const std::uint64_t>{kAttribute13Values},
         std::optional<std::span<const geometry::index_t>>{
             std::span<const geometry::index_t>{kAttribute13Offsets}},
         std::optional<std::span<const std::uint8_t>>{
             std::span<const std::uint8_t>{kAttribute13Presence}},
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute13Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{
             "\x63\x6f\x72\x6e\x65\x72\x5f\x6e\x6f\x72\x6d\x61\x6c\x73", 14},
         std::string_view{"\x4e\x4f\x52\x4d\x41\x4c", 6}, std::nullopt, 3U,
         std::span<const float>{kAttribute14Values}, std::nullopt, std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute14Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x75\x76\x5f\x30", 4},
         std::string_view{"\x54\x45\x58\x43\x4f\x4f\x52\x44", 8},
         std::optional<std::uint32_t>{0U}, 2U,
         std::span<const float>{kAttribute15Values}, std::nullopt, std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute15Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x75\x76\x5f\x32", 4},
         std::string_view{"\x54\x45\x58\x43\x4f\x4f\x52\x44", 8},
         std::optional<std::uint32_t>{2U}, 2U,
         std::span<const double>{kAttribute16Values}, std::nullopt,
         std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute16Metadata}},
        {geometry::AttributeDomain::corner,
         std::string_view{"\x75\x76\x5f\x35", 4},
         std::string_view{"\x54\x45\x58\x43\x4f\x4f\x52\x44", 8},
         std::optional<std::uint32_t>{5U}, 2U,
         std::span<const float>{kAttribute17Values}, std::nullopt, std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute17Metadata}},
        {geometry::AttributeDomain::face,
         std::string_view{"\x6d\x61\x74\x65\x72\x69\x61\x6c\x5f\x69\x64\x73",
                          12},
         std::string_view{"\x4d\x41\x54\x45\x52\x49\x41\x4c", 8}, std::nullopt,
         1U, std::span<const std::int32_t>{kAttribute18Values}, std::nullopt,
         std::nullopt,
         std::span<const geometry::MetadataEntryView>{
             kLegacyAttribute18Metadata}},
    }};
inline geometry::MeshMaterializationInput RichInput(
    Variant variant, MetadataRoute route) noexcept {
  const auto positions = variant == Variant::kTranslated
                             ? std::span<const double>{kTranslatedPositions}
                             : std::span<const double>{kOriginalPositions};
  const auto attributes =
      route == MetadataRoute::kLegacy
          ? std::span<const geometry::
                          AttributeMaterializationInput>{kLegacyAttributes}
          : std::span<const geometry::AttributeMaterializationInput>{
                kRetainedAttributes};
  return {positions,
          geometry::PolygonTopologyInput{kFaceOffsets, kCornerVertices},
          attributes};
}
struct AttributeGolden {
  std::span<const std::byte> bytes;
};
inline constexpr std::array<AttributeGolden, 19> kAttributeGoldens = {{
    {std::span<const std::byte>{kAttribute00ExpectedBytes}},
    {std::span<const std::byte>{kAttribute01ExpectedBytes}},
    {std::span<const std::byte>{kAttribute02ExpectedBytes}},
    {std::span<const std::byte>{kAttribute03ExpectedBytes}},
    {std::span<const std::byte>{kAttribute04ExpectedBytes}},
    {std::span<const std::byte>{kAttribute05ExpectedBytes}},
    {std::span<const std::byte>{kAttribute06ExpectedBytes}},
    {std::span<const std::byte>{kAttribute07ExpectedBytes}},
    {std::span<const std::byte>{kAttribute08ExpectedBytes}},
    {std::span<const std::byte>{kAttribute09ExpectedBytes}},
    {std::span<const std::byte>{kAttribute10ExpectedBytes}},
    {std::span<const std::byte>{kAttribute11ExpectedBytes}},
    {std::span<const std::byte>{kAttribute12ExpectedBytes}},
    {std::span<const std::byte>{kAttribute13ExpectedBytes}},
    {std::span<const std::byte>{kAttribute14ExpectedBytes}},
    {std::span<const std::byte>{kAttribute15ExpectedBytes}},
    {std::span<const std::byte>{kAttribute16ExpectedBytes}},
    {std::span<const std::byte>{kAttribute17ExpectedBytes}},
    {std::span<const std::byte>{kAttribute18ExpectedBytes}},
}};
inline std::span<const AttributeGolden> AttributeGoldens() noexcept {
  return kAttributeGoldens;
}
inline constexpr std::array<geometry::index_t, 6> kExpectedFaceSources = {{
    0x0000000000000000ULL,
    0x0000000000000001ULL,
    0x0000000000000001ULL,
    0x0000000000000002ULL,
    0x0000000000000002ULL,
    0x0000000000000002ULL,
}};
inline constexpr std::array<geometry::index_t, 4> kExpectedFaceOutputOffsets = {
    {
        0x0000000000000000ULL,
        0x0000000000000001ULL,
        0x0000000000000003ULL,
        0x0000000000000006ULL,
    }};
inline constexpr std::array<geometry::index_t, 7> kExpectedOutputFaceOffsets = {
    {
        0x0000000000000000ULL,
        0x0000000000000003ULL,
        0x0000000000000006ULL,
        0x0000000000000009ULL,
        0x000000000000000cULL,
        0x000000000000000fULL,
        0x0000000000000012ULL,
    }};
struct TriangleFamily {
  std::span<const IndexTriple> source_corners;
};
struct RichFaceOracle {
  std::uint64_t source_face;
  std::string_view signed_xy_area2;
  FractionTriple twice_area_vector;
  std::span<const TriangleFamily> families;
};
inline constexpr std::array<IndexTriple, 1> kFace0Family0 = {{
    {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
}};
inline constexpr std::array<TriangleFamily, 1> kFace0Families = {{
    {std::span<const IndexTriple>{kFace0Family0}},
}};
inline constexpr std::array<IndexTriple, 2> kFace1Family0 = {{
    {0x0000000000000003ULL, 0x0000000000000004ULL, 0x0000000000000005ULL},
    {0x0000000000000003ULL, 0x0000000000000005ULL, 0x0000000000000006ULL},
}};
inline constexpr std::array<IndexTriple, 2> kFace1Family1 = {{
    {0x0000000000000003ULL, 0x0000000000000004ULL, 0x0000000000000006ULL},
    {0x0000000000000004ULL, 0x0000000000000005ULL, 0x0000000000000006ULL},
}};
inline constexpr std::array<TriangleFamily, 2> kFace1Families = {{
    {std::span<const IndexTriple>{kFace1Family0}},
    {std::span<const IndexTriple>{kFace1Family1}},
}};
inline constexpr std::array<IndexTriple, 3> kFace2Family0 = {{
    {0x0000000000000007ULL, 0x0000000000000008ULL, 0x000000000000000aULL},
    {0x0000000000000007ULL, 0x000000000000000aULL, 0x000000000000000bULL},
    {0x0000000000000008ULL, 0x0000000000000009ULL, 0x000000000000000aULL},
}};
inline constexpr std::array<TriangleFamily, 1> kFace2Families = {{
    {std::span<const IndexTriple>{kFace2Family0}},
}};
inline constexpr std::array<RichFaceOracle, 3> kRichFaceOracles = {{
    {0ULL,
     std::string_view{"\x35\x2f\x34", 3},
     {std::string_view{"\x35\x2f\x38", 3},
      std::string_view{"\x2d\x35\x2f\x38", 4},
      std::string_view{"\x35\x2f\x34", 3}},
     std::span<const TriangleFamily>{kFace0Families}},
    {1ULL,
     std::string_view{"\x2d\x35\x2f\x32", 4},
     {std::string_view{"\x2d\x35\x2f\x34", 4},
      std::string_view{"\x35\x2f\x34", 3},
      std::string_view{"\x2d\x35\x2f\x32", 4}},
     std::span<const TriangleFamily>{kFace1Families}},
    {2ULL,
     std::string_view{"\x31\x35\x2f\x34", 4},
     {std::string_view{"\x31\x35\x2f\x38", 4},
      std::string_view{"\x2d\x31\x35\x2f\x38", 5},
      std::string_view{"\x31\x35\x2f\x34", 4}},
     std::span<const TriangleFamily>{kFace2Families}},
}};
inline std::span<const RichFaceOracle> RichFaceOracles() noexcept {
  return kRichFaceOracles;
}
// Source-order references include unused row 11; native conversion skips it.
inline constexpr std::array<WordTriple32, 12>
    kTranslatedReferenceF32PositionWords = {{
        {0x3e000000U, 0xbe800000U, 0x3ec00000U},
        {0x3f900000U, 0x3e800000U, 0x3e000000U},
        {0xbec00000U, 0x3f400000U, 0x3f900000U},
        {0xbf600000U, 0x3fe00000U, 0x3ff00000U},
        {0x3e000000U, 0x40100000U, 0x3fd00000U},
        {0x3f200000U, 0x3fa00000U, 0x3f600000U},
        {0xbec00000U, 0x3f400000U, 0x3f900000U},
        {0x3f900000U, 0x3fc00000U, 0x3f400000U},
        {0x3ec00000U, 0x40400000U, 0x3ff00000U},
        {0xbe000000U, 0x3fc00000U, 0x3fb00000U},
        {0xbf900000U, 0x40100000U, 0x40100000U},
        {0x30800000U, 0x80000000U, 0x3f100000U},
    }};
inline constexpr std::string_view kTranslatedMaxPositionError =
    std::string_view{"\x31\x2f\x31\x30\x37\x33\x37\x34\x31\x38\x32\x34", 12};
inline constexpr std::string_view kTranslatedPositionBound =
    std::string_view{"\x31\x2f\x35\x33\x36\x38\x37\x30\x39\x31\x32", 11};
inline constexpr char kCatalogJson[] = R"MVGLTF({
  "asset": {
    "version": "2.0",
    "generator": "Meshvale original material-library data fixture"
  },
  "samplers": [
    {
      "magFilter": 9728,
      "minFilter": 9728,
      "wrapS": 10497,
      "wrapT": 10497
    }
  ],
  "images": [
    {
      "uri": "images/warm.png",
      "mimeType": "image/png",
      "name": "warm"
    },
    {
      "uri": "images/green.png",
      "mimeType": "image/png",
      "name": "green"
    },
    {
      "uri": "images/blue.png",
      "mimeType": "image/png",
      "name": "blue"
    }
  ],
  "textures": [
    {
      "sampler": 0,
      "source": 0
    },
    {
      "sampler": 0,
      "source": 1
    },
    {
      "sampler": 0,
      "source": 2
    }
  ],
  "materials": [
    {
      "name": "warm-authored-input",
      "pbrMetallicRoughness": {
        "baseColorFactor": [
          1,
          1,
          1,
          1
        ],
        "baseColorTexture": {
          "index": 0,
          "texCoord": 0
        },
        "metallicFactor": 0,
        "roughnessFactor": 1
      },
      "alphaMode": "OPAQUE",
      "doubleSided": false
    },
    {
      "name": "green-authored-input",
      "pbrMetallicRoughness": {
        "baseColorFactor": [
          1,
          1,
          1,
          1
        ],
        "baseColorTexture": {
          "index": 1,
          "texCoord": 1
        },
        "metallicFactor": 0,
        "roughnessFactor": 1
      },
      "alphaMode": "OPAQUE",
      "doubleSided": false
    },
    {
      "name": "blue-authored-input",
      "pbrMetallicRoughness": {
        "baseColorFactor": [
          1,
          1,
          1,
          1
        ],
        "baseColorTexture": {
          "index": 2,
          "texCoord": 2
        },
        "metallicFactor": 0,
        "roughnessFactor": 1
      },
      "alphaMode": "OPAQUE",
      "doubleSided": false
    }
  ]
}
)MVGLTF";
inline std::string_view CatalogJson() noexcept {
  return {kCatalogJson, sizeof(kCatalogJson) - 1};
}
inline constexpr std::array<std::byte, 81> kImage0Bytes = {{
    std::byte{0x89U}, std::byte{0x50U}, std::byte{0x4eU}, std::byte{0x47U},
    std::byte{0x0dU}, std::byte{0x0aU}, std::byte{0x1aU}, std::byte{0x0aU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x0dU},
    std::byte{0x49U}, std::byte{0x48U}, std::byte{0x44U}, std::byte{0x52U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x08U}, std::byte{0x06U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x72U}, std::byte{0xb6U}, std::byte{0x0dU},
    std::byte{0x24U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x18U}, std::byte{0x49U}, std::byte{0x44U}, std::byte{0x41U},
    std::byte{0x54U}, std::byte{0x78U}, std::byte{0xdaU}, std::byte{0x63U},
    std::byte{0xf8U}, std::byte{0xcfU}, std::byte{0xc0U}, std::byte{0xf0U},
    std::byte{0xffU}, std::byte{0x7fU}, std::byte{0x03U}, std::byte{0xc3U},
    std::byte{0x7fU}, std::byte{0x86U}, std::byte{0x06U}, std::byte{0x10U},
    std::byte{0xe3U}, std::byte{0x3fU}, std::byte{0xc3U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x4cU}, std::byte{0xcfU}, std::byte{0x08U},
    std::byte{0xf9U}, std::byte{0x7cU}, std::byte{0xaaU}, std::byte{0x21U},
    std::byte{0xc6U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x49U}, std::byte{0x45U}, std::byte{0x4eU},
    std::byte{0x44U}, std::byte{0xaeU}, std::byte{0x42U}, std::byte{0x60U},
    std::byte{0x82U},
}};
inline constexpr std::array<std::byte, 80> kImage1Bytes = {{
    std::byte{0x89U}, std::byte{0x50U}, std::byte{0x4eU}, std::byte{0x47U},
    std::byte{0x0dU}, std::byte{0x0aU}, std::byte{0x1aU}, std::byte{0x0aU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x0dU},
    std::byte{0x49U}, std::byte{0x48U}, std::byte{0x44U}, std::byte{0x52U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x08U}, std::byte{0x06U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x72U}, std::byte{0xb6U}, std::byte{0x0dU},
    std::byte{0x24U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x17U}, std::byte{0x49U}, std::byte{0x44U}, std::byte{0x41U},
    std::byte{0x54U}, std::byte{0x78U}, std::byte{0xdaU}, std::byte{0x63U},
    std::byte{0x60U}, std::byte{0xf8U}, std::byte{0x0fU}, std::byte{0x84U},
    std::byte{0x0dU}, std::byte{0xffU}, std::byte{0xffU}, std::byte{0x33U},
    std::byte{0x30U}, std::byte{0x38U}, std::byte{0x30U}, std::byte{0xfcU},
    std::byte{0x6fU}, std::byte{0x00U}, std::byte{0x42U}, std::byte{0x00U},
    std::byte{0x46U}, std::byte{0x96U}, std::byte{0x08U}, std::byte{0xbaU},
    std::byte{0x6bU}, std::byte{0xdfU}, std::byte{0x12U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x49U}, std::byte{0x45U}, std::byte{0x4eU}, std::byte{0x44U},
    std::byte{0xaeU}, std::byte{0x42U}, std::byte{0x60U}, std::byte{0x82U},
}};
inline constexpr std::array<std::byte, 82> kImage2Bytes = {{
    std::byte{0x89U}, std::byte{0x50U}, std::byte{0x4eU}, std::byte{0x47U},
    std::byte{0x0dU}, std::byte{0x0aU}, std::byte{0x1aU}, std::byte{0x0aU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x0dU},
    std::byte{0x49U}, std::byte{0x48U}, std::byte{0x44U}, std::byte{0x52U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x08U}, std::byte{0x06U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x72U}, std::byte{0xb6U}, std::byte{0x0dU},
    std::byte{0x24U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x19U}, std::byte{0x49U}, std::byte{0x44U}, std::byte{0x41U},
    std::byte{0x54U}, std::byte{0x78U}, std::byte{0xdaU}, std::byte{0x63U},
    std::byte{0x60U}, std::byte{0x60U}, std::byte{0xf8U}, std::byte{0xffU},
    std::byte{0xbfU}, std::byte{0x01U}, std::byte{0x88U}, std::byte{0x19U},
    std::byte{0x18U}, std::byte{0x18U}, std::byte{0x1cU}, std::byte{0xfeU},
    std::byte{0x37U}, std::byte{0xfcU}, std::byte{0xffU}, std::byte{0xffU},
    std::byte{0x1fU}, std::byte{0x00U}, std::byte{0x46U}, std::byte{0xd5U},
    std::byte{0x09U}, std::byte{0x39U}, std::byte{0x24U}, std::byte{0x13U},
    std::byte{0xc2U}, std::byte{0x44U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x49U}, std::byte{0x45U},
    std::byte{0x4eU}, std::byte{0x44U}, std::byte{0xaeU}, std::byte{0x42U},
    std::byte{0x60U}, std::byte{0x82U},
}};
inline constexpr std::array<SuppliedGltfResource, 3> kCatalogResources = {{
    {GltfResourceKind::kImage, 0ULL, std::span<const std::byte>{kImage0Bytes}},
    {GltfResourceKind::kImage, 1ULL, std::span<const std::byte>{kImage1Bytes}},
    {GltfResourceKind::kImage, 2ULL, std::span<const std::byte>{kImage2Bytes}},
}};
inline std::span<const SuppliedGltfResource> CatalogResources() noexcept {
  return kCatalogResources;
}
struct ImageAlias {
  std::string_view name;
  std::string_view canonical_name;
  std::span<const std::byte> bytes;
};
inline constexpr std::array<std::byte, 81> kImageAliasBytes = {{
    std::byte{0x89U}, std::byte{0x50U}, std::byte{0x4eU}, std::byte{0x47U},
    std::byte{0x0dU}, std::byte{0x0aU}, std::byte{0x1aU}, std::byte{0x0aU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x0dU},
    std::byte{0x49U}, std::byte{0x48U}, std::byte{0x44U}, std::byte{0x52U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x02U},
    std::byte{0x08U}, std::byte{0x06U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x72U}, std::byte{0xb6U}, std::byte{0x0dU},
    std::byte{0x24U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x18U}, std::byte{0x49U}, std::byte{0x44U}, std::byte{0x41U},
    std::byte{0x54U}, std::byte{0x78U}, std::byte{0xdaU}, std::byte{0x63U},
    std::byte{0xf8U}, std::byte{0xcfU}, std::byte{0xc0U}, std::byte{0xf0U},
    std::byte{0xffU}, std::byte{0x7fU}, std::byte{0x03U}, std::byte{0xc3U},
    std::byte{0x7fU}, std::byte{0x86U}, std::byte{0x06U}, std::byte{0x10U},
    std::byte{0xe3U}, std::byte{0x3fU}, std::byte{0xc3U}, std::byte{0x7fU},
    std::byte{0x00U}, std::byte{0x4cU}, std::byte{0xcfU}, std::byte{0x08U},
    std::byte{0xf9U}, std::byte{0x7cU}, std::byte{0xaaU}, std::byte{0x21U},
    std::byte{0xc6U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x49U}, std::byte{0x45U}, std::byte{0x4eU},
    std::byte{0x44U}, std::byte{0xaeU}, std::byte{0x42U}, std::byte{0x60U},
    std::byte{0x82U},
}};
inline constexpr std::array<ImageAlias, 1> kCatalogAliases = {{
    {std::string_view{"\x69\x6d\x61\x67\x65\x73\x2f\x77\x61\x72\x6d\x2d\x61\x6c"
                      "\x69\x61\x73\x2e\x70\x6e\x67",
                      21},
     std::string_view{
         "\x69\x6d\x61\x67\x65\x73\x2f\x77\x61\x72\x6d\x2e\x70\x6e\x67", 15},
     std::span<const std::byte>{kImageAliasBytes}},
}};
inline std::span<const ImageAlias> CatalogAliases() noexcept {
  return kCatalogAliases;
}
struct MaterialBindingGolden {
  std::int32_t source_id;
  std::uint64_t catalog_material;
  std::uint32_t catalog_uv;
  std::uint64_t source_attribute;
  std::uint32_t source_set;
  std::uint32_t wire_set;
};
inline constexpr std::array<MaterialBindingGolden, 3> kMaterialBindings = {{
    {17, 0ULL, 0U, 15ULL, 0U, 0U},
    {-4, 1ULL, 1U, 16ULL, 2U, 1U},
    {1000003, 2ULL, 2U, 17ULL, 5U, 2U},
}};
inline std::span<const MaterialBindingGolden> MaterialBindings() noexcept {
  return kMaterialBindings;
}
struct ScalarBoundary {
  std::string_view name;
  std::uint64_t source_f64;
  std::optional<std::uint32_t> reference_f32;
  std::optional<std::string_view> exact_source;
  std::optional<std::string_view> absolute_error;
  bool nonfinite;
  bool outside_f32_finite_range;
  bool nonzero_to_zero;
};
inline constexpr std::array<ScalarBoundary, 43> kScalarCases = {{
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x7a\x65\x72\x6f",
                      13},
     0x0000000000000000ULL, std::optional<std::uint32_t>{0x00000000U},
     std::string_view{"\x30", 1}, std::string_view{"\x30", 1}, false, false,
     false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x7a\x65\x72\x6f",
                      13},
     0x8000000000000000ULL, std::optional<std::uint32_t>{0x80000000U},
     std::string_view{"\x30", 1}, std::string_view{"\x30", 1}, false, false,
     false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6d\x69\x6e\x69\x6d\x75\x6d\x5f"
         "\x46\x33\x32\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c",
         30},
     0x36a0000000000000ULL, std::optional<std::uint32_t>{0x00000001U},
     std::string_view{
         "\x31\x2f\x37\x31\x33\x36\x32\x33\x38\x34\x36\x33\x35\x32\x39\x37\x39"
         "\x39\x34\x30\x35\x32\x39\x31\x34\x32\x39\x38\x34\x37\x32\x34\x37\x34"
         "\x37\x35\x36\x38\x31\x39\x31\x33\x37\x33\x33\x31\x32",
         47},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6d\x69\x6e\x69\x6d\x75\x6d\x5f"
         "\x46\x33\x32\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c",
         30},
     0xb6a0000000000000ULL, std::optional<std::uint32_t>{0x80000001U},
     std::string_view{
         "\x2d\x31\x2f\x37\x31\x33\x36\x32\x33\x38\x34\x36\x33\x35\x32\x39\x37"
         "\x39\x39\x34\x30\x35\x32\x39\x31\x34\x32\x39\x38\x34\x37\x32\x34\x37"
         "\x34\x37\x35\x36\x38\x31\x39\x31\x33\x37\x33\x33\x31\x32",
         48},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x68\x61\x6c\x66\x5f\x6d\x69\x6e"
         "\x69\x6d\x75\x6d\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x74\x69"
         "\x65\x5f\x65\x76\x65\x6e\x5f\x7a\x65\x72\x6f",
         45},
     0x3690000000000000ULL, std::optional<std::uint32_t>{0x00000000U},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     false, false, true},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x68\x61\x6c\x66\x5f\x6d\x69\x6e"
         "\x69\x6d\x75\x6d\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x74\x69"
         "\x65\x5f\x65\x76\x65\x6e\x5f\x7a\x65\x72\x6f",
         45},
     0xb690000000000000ULL, std::optional<std::uint32_t>{0x80000000U},
     std::string_view{
         "\x2d\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39"
         "\x35\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39"
         "\x34\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         49},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     false, false, true},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x74\x68\x72\x65\x65\x5f\x68\x61"
         "\x6c\x66\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x74\x69\x65\x5f"
         "\x65\x76\x65\x6e\x5f\x74\x77\x6f",
         42},
     0x36a8000000000000ULL, std::optional<std::uint32_t>{0x00000002U},
     std::string_view{
         "\x33\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     false, false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x74\x68\x72\x65\x65\x5f\x68\x61"
         "\x6c\x66\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x74\x69\x65\x5f"
         "\x65\x76\x65\x6e\x5f\x74\x77\x6f",
         42},
     0xb6a8000000000000ULL, std::optional<std::uint32_t>{0x80000002U},
     std::string_view{
         "\x2d\x33\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39"
         "\x35\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39"
         "\x34\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         49},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     false, false, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6c\x61\x72\x67\x65\x73\x74\x5f"
         "\x46\x33\x32\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c",
         30},
     0x380fffffc0000000ULL, std::optional<std::uint32_t>{0x007fffffU},
     std::string_view{"\x38\x33\x38\x38\x36\x30\x37\x2f\x37\x31\x33\x36\x32\x33"
                      "\x38\x34\x36\x33\x35\x32\x39\x37\x39\x39\x34\x30\x35\x32"
                      "\x39\x31\x34\x32\x39\x38\x34\x37\x32\x34\x37\x34\x37\x35"
                      "\x36\x38\x31\x39\x31\x33\x37\x33\x33\x31\x32",
                      53},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6c\x61\x72\x67\x65\x73\x74\x5f"
         "\x46\x33\x32\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c",
         30},
     0xb80fffffc0000000ULL, std::optional<std::uint32_t>{0x807fffffU},
     std::string_view{"\x2d\x38\x33\x38\x38\x36\x30\x37\x2f\x37\x31\x33\x36\x32"
                      "\x33\x38\x34\x36\x33\x35\x32\x39\x37\x39\x39\x34\x30\x35"
                      "\x32\x39\x31\x34\x32\x39\x38\x34\x37\x32\x34\x37\x34\x37"
                      "\x35\x36\x38\x31\x39\x31\x33\x37\x33\x33\x31\x32",
                      54},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x73\x75\x62\x6e\x6f"
                      "\x72\x6d\x61\x6c\x5f\x6e\x6f\x72\x6d\x61\x6c\x5f\x62\x6f"
                      "\x75\x6e\x64\x61\x72\x79\x5f\x74\x69\x65\x5f\x65\x76\x65"
                      "\x6e\x5f\x6d\x69\x6e\x5f\x6e\x6f\x72\x6d\x61\x6c",
                      54},
     0x380fffffe0000000ULL, std::optional<std::uint32_t>{0x00800000U},
     std::string_view{"\x31\x36\x37\x37\x37\x32\x31\x35\x2f\x31\x34\x32\x37\x32"
                      "\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35\x39\x38\x38\x31"
                      "\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34\x39"
                      "\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
                      55},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     false, false, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x73\x75\x62\x6e\x6f"
                      "\x72\x6d\x61\x6c\x5f\x6e\x6f\x72\x6d\x61\x6c\x5f\x62\x6f"
                      "\x75\x6e\x64\x61\x72\x79\x5f\x74\x69\x65\x5f\x65\x76\x65"
                      "\x6e\x5f\x6d\x69\x6e\x5f\x6e\x6f\x72\x6d\x61\x6c",
                      54},
     0xb80fffffe0000000ULL, std::optional<std::uint32_t>{0x80800000U},
     std::string_view{
         "\x2d\x31\x36\x37\x37\x37\x32\x31\x35\x2f\x31\x34\x32\x37\x32\x34\x37"
         "\x36\x39\x32\x37\x30\x35\x39\x35\x39\x38\x38\x31\x30\x35\x38\x32\x38"
         "\x35\x39\x36\x39\x34\x34\x39\x34\x39\x35\x31\x33\x36\x33\x38\x32\x37"
         "\x34\x36\x36\x32\x34",
         56},
     std::string_view{
         "\x31\x2f\x31\x34\x32\x37\x32\x34\x37\x36\x39\x32\x37\x30\x35\x39\x35"
         "\x39\x38\x38\x31\x30\x35\x38\x32\x38\x35\x39\x36\x39\x34\x34\x39\x34"
         "\x39\x35\x31\x33\x36\x33\x38\x32\x37\x34\x36\x36\x32\x34",
         48},
     false, false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6d\x69\x6e\x69\x6d"
                      "\x75\x6d\x5f\x46\x33\x32\x5f\x6e\x6f\x72\x6d\x61\x6c",
                      27},
     0x3810000000000000ULL, std::optional<std::uint32_t>{0x00800000U},
     std::string_view{"\x31\x2f\x38\x35\x30\x37\x30\x35\x39\x31\x37\x33\x30\x32"
                      "\x33\x34\x36\x31\x35\x38\x36\x35\x38\x34\x33\x36\x35\x31"
                      "\x38\x35\x37\x39\x34\x32\x30\x35\x32\x38\x36\x34",
                      40},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6d\x69\x6e\x69\x6d"
                      "\x75\x6d\x5f\x46\x33\x32\x5f\x6e\x6f\x72\x6d\x61\x6c",
                      27},
     0xb810000000000000ULL, std::optional<std::uint32_t>{0x80800000U},
     std::string_view{"\x2d\x31\x2f\x38\x35\x30\x37\x30\x35\x39\x31\x37\x33\x30"
                      "\x32\x33\x34\x36\x31\x35\x38\x36\x35\x38\x34\x33\x36\x35"
                      "\x31\x38\x35\x37\x39\x34\x32\x30\x35\x32\x38\x36\x34",
                      41},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6f\x6e\x65\x5f\x70"
                      "\x6c\x75\x73\x5f\x68\x61\x6c\x66\x5f\x75\x6c\x70\x5f\x74"
                      "\x69\x65\x5f\x65\x76\x65\x6e\x5f\x6f\x6e\x65",
                      39},
     0x3ff0000010000000ULL, std::optional<std::uint32_t>{0x3f800000U},
     std::string_view{
         "\x31\x36\x37\x37\x37\x32\x31\x37\x2f\x31\x36\x37\x37\x37\x32\x31\x36",
         17},
     std::string_view{"\x31\x2f\x31\x36\x37\x37\x37\x32\x31\x36", 10}, false,
     false, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6f\x6e\x65\x5f\x70"
                      "\x6c\x75\x73\x5f\x68\x61\x6c\x66\x5f\x75\x6c\x70\x5f\x74"
                      "\x69\x65\x5f\x65\x76\x65\x6e\x5f\x6f\x6e\x65",
                      39},
     0xbff0000010000000ULL, std::optional<std::uint32_t>{0xbf800000U},
     std::string_view{"\x2d\x31\x36\x37\x37\x37\x32\x31\x37\x2f\x31\x36\x37\x37"
                      "\x37\x32\x31\x36",
                      18},
     std::string_view{"\x31\x2f\x31\x36\x37\x37\x37\x32\x31\x36", 10}, false,
     false, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6f\x6e\x65\x5f\x70\x6c\x75\x73"
         "\x5f\x74\x68\x72\x65\x65\x5f\x68\x61\x6c\x66\x5f\x75\x6c\x70\x5f\x74"
         "\x69\x65\x5f\x65\x76\x65\x6e\x5f\x74\x77\x6f",
         45},
     0x3ff0000030000000ULL, std::optional<std::uint32_t>{0x3f800002U},
     std::string_view{
         "\x31\x36\x37\x37\x37\x32\x31\x39\x2f\x31\x36\x37\x37\x37\x32\x31\x36",
         17},
     std::string_view{"\x31\x2f\x31\x36\x37\x37\x37\x32\x31\x36", 10}, false,
     false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6f\x6e\x65\x5f\x70\x6c\x75\x73"
         "\x5f\x74\x68\x72\x65\x65\x5f\x68\x61\x6c\x66\x5f\x75\x6c\x70\x5f\x74"
         "\x69\x65\x5f\x65\x76\x65\x6e\x5f\x74\x77\x6f",
         45},
     0xbff0000030000000ULL, std::optional<std::uint32_t>{0xbf800002U},
     std::string_view{"\x2d\x31\x36\x37\x37\x37\x32\x31\x39\x2f\x31\x36\x37\x37"
                      "\x37\x32\x31\x36",
                      18},
     std::string_view{"\x31\x2f\x31\x36\x37\x37\x37\x32\x31\x36", 10}, false,
     false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6d\x61\x78\x69\x6d"
                      "\x75\x6d\x5f\x46\x33\x32\x5f\x66\x69\x6e\x69\x74\x65",
                      27},
     0x47efffffe0000000ULL, std::optional<std::uint32_t>{0x7f7fffffU},
     std::string_view{"\x33\x34\x30\x32\x38\x32\x33\x34\x36\x36\x33\x38\x35\x32"
                      "\x38\x38\x35\x39\x38\x31\x31\x37\x30\x34\x31\x38\x33\x34"
                      "\x38\x34\x35\x31\x36\x39\x32\x35\x34\x34\x30",
                      39},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6d\x61\x78\x69\x6d"
                      "\x75\x6d\x5f\x46\x33\x32\x5f\x66\x69\x6e\x69\x74\x65",
                      27},
     0xc7efffffe0000000ULL, std::optional<std::uint32_t>{0xff7fffffU},
     std::string_view{"\x2d\x33\x34\x30\x32\x38\x32\x33\x34\x36\x36\x33\x38\x35"
                      "\x32\x38\x38\x35\x39\x38\x31\x31\x37\x30\x34\x31\x38\x33"
                      "\x34\x38\x34\x35\x31\x36\x39\x32\x35\x34\x34\x30",
                      40},
     std::string_view{"\x30", 1}, false, false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6e\x65\x78\x74\x5f"
                      "\x46\x36\x34\x5f\x61\x62\x6f\x76\x65\x5f\x46\x33\x32\x5f"
                      "\x66\x69\x6e\x69\x74\x65\x5f\x72\x61\x6e\x67\x65",
                      40},
     0x47efffffe0000001ULL, std::optional<std::uint32_t>{0x7f7fffffU},
     std::string_view{"\x33\x34\x30\x32\x38\x32\x33\x34\x36\x36\x33\x38\x35\x32"
                      "\x38\x38\x39\x37\x35\x39\x30\x36\x33\x36\x30\x34\x36\x34"
                      "\x34\x31\x36\x37\x38\x36\x33\x35\x30\x30\x38",
                      39},
     std::string_view{"\x33\x37\x37\x37\x38\x39\x33\x31\x38\x36\x32\x39\x35\x37"
                      "\x31\x36\x31\x37\x30\x39\x35\x36\x38",
                      23},
     false, true, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6e\x65\x78\x74\x5f"
                      "\x46\x36\x34\x5f\x61\x62\x6f\x76\x65\x5f\x46\x33\x32\x5f"
                      "\x66\x69\x6e\x69\x74\x65\x5f\x72\x61\x6e\x67\x65",
                      40},
     0xc7efffffe0000001ULL, std::optional<std::uint32_t>{0xff7fffffU},
     std::string_view{"\x2d\x33\x34\x30\x32\x38\x32\x33\x34\x36\x36\x33\x38\x35"
                      "\x32\x38\x38\x39\x37\x35\x39\x30\x36\x33\x36\x30\x34\x36"
                      "\x34\x34\x31\x36\x37\x38\x36\x33\x35\x30\x30\x38",
                      40},
     std::string_view{"\x33\x37\x37\x37\x38\x39\x33\x31\x38\x36\x32\x39\x35\x37"
                      "\x31\x36\x31\x37\x30\x39\x35\x36\x38",
                      23},
     false, true, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6f\x76\x65\x72\x66"
                      "\x6c\x6f\x77\x5f\x68\x61\x6c\x66\x77\x61\x79\x5f\x74\x68"
                      "\x72\x65\x73\x68\x6f\x6c\x64",
                      35},
     0x47effffff0000000ULL, std::optional<std::uint32_t>{0x7f800000U},
     std::string_view{"\x33\x34\x30\x32\x38\x32\x33\x35\x36\x37\x37\x39\x37\x33"
                      "\x33\x36\x36\x31\x36\x33\x37\x35\x33\x39\x33\x39\x35\x34"
                      "\x35\x38\x31\x34\x32\x35\x36\x38\x34\x34\x38",
                      39},
     std::nullopt, false, true, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6f\x76\x65\x72\x66"
                      "\x6c\x6f\x77\x5f\x68\x61\x6c\x66\x77\x61\x79\x5f\x74\x68"
                      "\x72\x65\x73\x68\x6f\x6c\x64",
                      35},
     0xc7effffff0000000ULL, std::optional<std::uint32_t>{0xff800000U},
     std::string_view{"\x2d\x33\x34\x30\x32\x38\x32\x33\x35\x36\x37\x37\x39\x37"
                      "\x33\x33\x36\x36\x31\x36\x33\x37\x35\x33\x39\x33\x39\x35"
                      "\x34\x35\x38\x31\x34\x32\x35\x36\x38\x34\x34\x38",
                      40},
     std::nullopt, false, true, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x74\x77\x6f\x5f\x70"
                      "\x6f\x77\x65\x72\x5f\x31\x32\x38",
                      22},
     0x47f0000000000000ULL, std::optional<std::uint32_t>{0x7f800000U},
     std::string_view{"\x33\x34\x30\x32\x38\x32\x33\x36\x36\x39\x32\x30\x39\x33"
                      "\x38\x34\x36\x33\x34\x36\x33\x33\x37\x34\x36\x30\x37\x34"
                      "\x33\x31\x37\x36\x38\x32\x31\x31\x34\x35\x36",
                      39},
     std::nullopt, false, true, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x74\x77\x6f\x5f\x70"
                      "\x6f\x77\x65\x72\x5f\x31\x32\x38",
                      22},
     0xc7f0000000000000ULL, std::optional<std::uint32_t>{0xff800000U},
     std::string_view{"\x2d\x33\x34\x30\x32\x38\x32\x33\x36\x36\x39\x32\x30\x39"
                      "\x33\x38\x34\x36\x33\x34\x36\x33\x33\x37\x34\x36\x30\x37"
                      "\x34\x33\x31\x37\x36\x38\x32\x31\x31\x34\x35\x36",
                      40},
     std::nullopt, false, true, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6d\x69\x6e\x69\x6d\x75\x6d\x5f"
         "\x46\x36\x34\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c",
         30},
     0x0000000000000001ULL, std::optional<std::uint32_t>{0x00000000U},
     std::string_view{
         "\x31\x2f\x32\x30\x32\x34\x30\x32\x32\x35\x33\x33\x30\x37\x33\x31\x30"
         "\x36\x31\x38\x33\x35\x32\x34\x39\x35\x33\x34\x36\x37\x31\x38\x39\x31"
         "\x37\x33\x30\x37\x30\x34\x39\x35\x35\x36\x36\x34\x39\x37\x36\x34\x31"
         "\x34\x32\x31\x31\x38\x33\x35\x36\x39\x30\x31\x33\x35\x38\x30\x32\x37"
         "\x34\x33\x30\x33\x33\x39\x35\x36\x37\x39\x39\x35\x33\x34\x36\x38\x39"
         "\x31\x39\x36\x30\x33\x38\x33\x37\x30\x31\x34\x33\x37\x31\x32\x34\x34"
         "\x39\x35\x31\x38\x37\x30\x37\x37\x38\x36\x34\x33\x31\x36\x38\x31\x31"
         "\x39\x31\x31\x33\x38\x39\x38\x30\x38\x37\x33\x37\x33\x38\x35\x37\x39"
         "\x33\x34\x37\x36\x38\x36\x37\x30\x31\x33\x33\x39\x39\x39\x34\x30\x37"
         "\x33\x38\x35\x30\x39\x39\x32\x31\x35\x31\x37\x34\x32\x34\x32\x37\x36"
         "\x35\x36\x36\x33\x36\x31\x33\x36\x34\x34\x36\x36\x39\x30\x37\x37\x34"
         "\x32\x30\x39\x33\x32\x31\x36\x33\x34\x31\x32\x33\x39\x37\x36\x37\x36"
         "\x37\x38\x34\x37\x32\x37\x34\x35\x30\x36\x38\x35\x36\x32\x30\x30\x37"
         "\x34\x38\x33\x34\x32\x34\x36\x39\x32\x36\x39\x38\x36\x31\x38\x31\x30"
         "\x33\x33\x35\x35\x36\x34\x39\x31\x35\x39\x35\x35\x36\x33\x34\x30\x38"
         "\x31\x30\x30\x35\x36\x35\x31\x32\x33\x35\x38\x37\x36\x39\x35\x35\x32"
         "\x33\x33\x33\x34\x31\x34\x36\x31\x35\x32\x33\x30\x35\x30\x32\x35\x33"
         "\x32\x31\x38\x36\x33\x32\x37\x35\x30\x38\x36\x34\x36\x30\x30\x36\x32"
         "\x36\x33\x33\x30\x37\x37\x30\x37\x37\x34\x31\x30\x39\x33\x34\x39\x34"
         "\x37\x38\x34",
         326},
     std::string_view{
         "\x31\x2f\x32\x30\x32\x34\x30\x32\x32\x35\x33\x33\x30\x37\x33\x31\x30"
         "\x36\x31\x38\x33\x35\x32\x34\x39\x35\x33\x34\x36\x37\x31\x38\x39\x31"
         "\x37\x33\x30\x37\x30\x34\x39\x35\x35\x36\x36\x34\x39\x37\x36\x34\x31"
         "\x34\x32\x31\x31\x38\x33\x35\x36\x39\x30\x31\x33\x35\x38\x30\x32\x37"
         "\x34\x33\x30\x33\x33\x39\x35\x36\x37\x39\x39\x35\x33\x34\x36\x38\x39"
         "\x31\x39\x36\x30\x33\x38\x33\x37\x30\x31\x34\x33\x37\x31\x32\x34\x34"
         "\x39\x35\x31\x38\x37\x30\x37\x37\x38\x36\x34\x33\x31\x36\x38\x31\x31"
         "\x39\x31\x31\x33\x38\x39\x38\x30\x38\x37\x33\x37\x33\x38\x35\x37\x39"
         "\x33\x34\x37\x36\x38\x36\x37\x30\x31\x33\x33\x39\x39\x39\x34\x30\x37"
         "\x33\x38\x35\x30\x39\x39\x32\x31\x35\x31\x37\x34\x32\x34\x32\x37\x36"
         "\x35\x36\x36\x33\x36\x31\x33\x36\x34\x34\x36\x36\x39\x30\x37\x37\x34"
         "\x32\x30\x39\x33\x32\x31\x36\x33\x34\x31\x32\x33\x39\x37\x36\x37\x36"
         "\x37\x38\x34\x37\x32\x37\x34\x35\x30\x36\x38\x35\x36\x32\x30\x30\x37"
         "\x34\x38\x33\x34\x32\x34\x36\x39\x32\x36\x39\x38\x36\x31\x38\x31\x30"
         "\x33\x33\x35\x35\x36\x34\x39\x31\x35\x39\x35\x35\x36\x33\x34\x30\x38"
         "\x31\x30\x30\x35\x36\x35\x31\x32\x33\x35\x38\x37\x36\x39\x35\x35\x32"
         "\x33\x33\x33\x34\x31\x34\x36\x31\x35\x32\x33\x30\x35\x30\x32\x35\x33"
         "\x32\x31\x38\x36\x33\x32\x37\x35\x30\x38\x36\x34\x36\x30\x30\x36\x32"
         "\x36\x33\x33\x30\x37\x37\x30\x37\x37\x34\x31\x30\x39\x33\x34\x39\x34"
         "\x37\x38\x34",
         326},
     false, false, true},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6d\x69\x6e\x69\x6d\x75\x6d\x5f"
         "\x46\x36\x34\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c",
         30},
     0x8000000000000001ULL, std::optional<std::uint32_t>{0x80000000U},
     std::string_view{
         "\x2d\x31\x2f\x32\x30\x32\x34\x30\x32\x32\x35\x33\x33\x30\x37\x33\x31"
         "\x30\x36\x31\x38\x33\x35\x32\x34\x39\x35\x33\x34\x36\x37\x31\x38\x39"
         "\x31\x37\x33\x30\x37\x30\x34\x39\x35\x35\x36\x36\x34\x39\x37\x36\x34"
         "\x31\x34\x32\x31\x31\x38\x33\x35\x36\x39\x30\x31\x33\x35\x38\x30\x32"
         "\x37\x34\x33\x30\x33\x33\x39\x35\x36\x37\x39\x39\x35\x33\x34\x36\x38"
         "\x39\x31\x39\x36\x30\x33\x38\x33\x37\x30\x31\x34\x33\x37\x31\x32\x34"
         "\x34\x39\x35\x31\x38\x37\x30\x37\x37\x38\x36\x34\x33\x31\x36\x38\x31"
         "\x31\x39\x31\x31\x33\x38\x39\x38\x30\x38\x37\x33\x37\x33\x38\x35\x37"
         "\x39\x33\x34\x37\x36\x38\x36\x37\x30\x31\x33\x33\x39\x39\x39\x34\x30"
         "\x37\x33\x38\x35\x30\x39\x39\x32\x31\x35\x31\x37\x34\x32\x34\x32\x37"
         "\x36\x35\x36\x36\x33\x36\x31\x33\x36\x34\x34\x36\x36\x39\x30\x37\x37"
         "\x34\x32\x30\x39\x33\x32\x31\x36\x33\x34\x31\x32\x33\x39\x37\x36\x37"
         "\x36\x37\x38\x34\x37\x32\x37\x34\x35\x30\x36\x38\x35\x36\x32\x30\x30"
         "\x37\x34\x38\x33\x34\x32\x34\x36\x39\x32\x36\x39\x38\x36\x31\x38\x31"
         "\x30\x33\x33\x35\x35\x36\x34\x39\x31\x35\x39\x35\x35\x36\x33\x34\x30"
         "\x38\x31\x30\x30\x35\x36\x35\x31\x32\x33\x35\x38\x37\x36\x39\x35\x35"
         "\x32\x33\x33\x33\x34\x31\x34\x36\x31\x35\x32\x33\x30\x35\x30\x32\x35"
         "\x33\x32\x31\x38\x36\x33\x32\x37\x35\x30\x38\x36\x34\x36\x30\x30\x36"
         "\x32\x36\x33\x33\x30\x37\x37\x30\x37\x37\x34\x31\x30\x39\x33\x34\x39"
         "\x34\x37\x38\x34",
         327},
     std::string_view{
         "\x31\x2f\x32\x30\x32\x34\x30\x32\x32\x35\x33\x33\x30\x37\x33\x31\x30"
         "\x36\x31\x38\x33\x35\x32\x34\x39\x35\x33\x34\x36\x37\x31\x38\x39\x31"
         "\x37\x33\x30\x37\x30\x34\x39\x35\x35\x36\x36\x34\x39\x37\x36\x34\x31"
         "\x34\x32\x31\x31\x38\x33\x35\x36\x39\x30\x31\x33\x35\x38\x30\x32\x37"
         "\x34\x33\x30\x33\x33\x39\x35\x36\x37\x39\x39\x35\x33\x34\x36\x38\x39"
         "\x31\x39\x36\x30\x33\x38\x33\x37\x30\x31\x34\x33\x37\x31\x32\x34\x34"
         "\x39\x35\x31\x38\x37\x30\x37\x37\x38\x36\x34\x33\x31\x36\x38\x31\x31"
         "\x39\x31\x31\x33\x38\x39\x38\x30\x38\x37\x33\x37\x33\x38\x35\x37\x39"
         "\x33\x34\x37\x36\x38\x36\x37\x30\x31\x33\x33\x39\x39\x39\x34\x30\x37"
         "\x33\x38\x35\x30\x39\x39\x32\x31\x35\x31\x37\x34\x32\x34\x32\x37\x36"
         "\x35\x36\x36\x33\x36\x31\x33\x36\x34\x34\x36\x36\x39\x30\x37\x37\x34"
         "\x32\x30\x39\x33\x32\x31\x36\x33\x34\x31\x32\x33\x39\x37\x36\x37\x36"
         "\x37\x38\x34\x37\x32\x37\x34\x35\x30\x36\x38\x35\x36\x32\x30\x30\x37"
         "\x34\x38\x33\x34\x32\x34\x36\x39\x32\x36\x39\x38\x36\x31\x38\x31\x30"
         "\x33\x33\x35\x35\x36\x34\x39\x31\x35\x39\x35\x35\x36\x33\x34\x30\x38"
         "\x31\x30\x30\x35\x36\x35\x31\x32\x33\x35\x38\x37\x36\x39\x35\x35\x32"
         "\x33\x33\x33\x34\x31\x34\x36\x31\x35\x32\x33\x30\x35\x30\x32\x35\x33"
         "\x32\x31\x38\x36\x33\x32\x37\x35\x30\x38\x36\x34\x36\x30\x30\x36\x32"
         "\x36\x33\x33\x30\x37\x37\x30\x37\x37\x34\x31\x30\x39\x33\x34\x39\x34"
         "\x37\x38\x34",
         326},
     false, false, true},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x62\x65\x66\x6f"
         "\x72\x65\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x6e\x6f\x72\x6d"
         "\x61\x6c\x5f\x62\x6f\x75\x6e\x64\x61\x72\x79\x5f\x74\x69\x65",
         49},
     0x380fffffdfffffffULL, std::optional<std::uint32_t>{0x007fffffU},
     std::string_view{
         "\x39\x30\x30\x37\x31\x39\x38\x37\x31\x37\x38\x37\x30\x30\x37\x39\x2f"
         "\x37\x36\x36\x32\x34\x37\x37\x37\x30\x34\x33\x32\x39\x34\x34\x34\x32"
         "\x39\x31\x37\x39\x31\x37\x33\x35\x31\x33\x35\x37\x35\x31\x35\x34\x35"
         "\x39\x31\x38\x30\x39\x33\x36\x39\x35\x36\x31\x30\x39\x31\x38\x30\x31"
         "\x30\x38\x38",
         71},
     std::string_view{
         "\x35\x33\x36\x38\x37\x30\x39\x31\x31\x2f\x37\x36\x36\x32\x34\x37\x37"
         "\x37\x30\x34\x33\x32\x39\x34\x34\x34\x32\x39\x31\x37\x39\x31\x37\x33"
         "\x35\x31\x33\x35\x37\x35\x31\x35\x34\x35\x39\x31\x38\x30\x39\x33\x36"
         "\x39\x35\x36\x31\x30\x39\x31\x38\x30\x31\x30\x38\x38",
         64},
     false, false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x62\x65\x66\x6f"
         "\x72\x65\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x6e\x6f\x72\x6d"
         "\x61\x6c\x5f\x62\x6f\x75\x6e\x64\x61\x72\x79\x5f\x74\x69\x65",
         49},
     0xb80fffffdfffffffULL, std::optional<std::uint32_t>{0x807fffffU},
     std::string_view{
         "\x2d\x39\x30\x30\x37\x31\x39\x38\x37\x31\x37\x38\x37\x30\x30\x37\x39"
         "\x2f\x37\x36\x36\x32\x34\x37\x37\x37\x30\x34\x33\x32\x39\x34\x34\x34"
         "\x32\x39\x31\x37\x39\x31\x37\x33\x35\x31\x33\x35\x37\x35\x31\x35\x34"
         "\x35\x39\x31\x38\x30\x39\x33\x36\x39\x35\x36\x31\x30\x39\x31\x38\x30"
         "\x31\x30\x38\x38",
         72},
     std::string_view{
         "\x35\x33\x36\x38\x37\x30\x39\x31\x31\x2f\x37\x36\x36\x32\x34\x37\x37"
         "\x37\x30\x34\x33\x32\x39\x34\x34\x34\x32\x39\x31\x37\x39\x31\x37\x33"
         "\x35\x31\x33\x35\x37\x35\x31\x35\x34\x35\x39\x31\x38\x30\x39\x33\x36"
         "\x39\x35\x36\x31\x30\x39\x31\x38\x30\x31\x30\x38\x38",
         64},
     false, false, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x61\x66\x74\x65"
         "\x72\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x6e\x6f\x72\x6d\x61"
         "\x6c\x5f\x62\x6f\x75\x6e\x64\x61\x72\x79\x5f\x74\x69\x65",
         48},
     0x380fffffe0000001ULL, std::optional<std::uint32_t>{0x00800000U},
     std::string_view{
         "\x39\x30\x30\x37\x31\x39\x38\x37\x31\x37\x38\x37\x30\x30\x38\x31\x2f"
         "\x37\x36\x36\x32\x34\x37\x37\x37\x30\x34\x33\x32\x39\x34\x34\x34\x32"
         "\x39\x31\x37\x39\x31\x37\x33\x35\x31\x33\x35\x37\x35\x31\x35\x34\x35"
         "\x39\x31\x38\x30\x39\x33\x36\x39\x35\x36\x31\x30\x39\x31\x38\x30\x31"
         "\x30\x38\x38",
         71},
     std::string_view{
         "\x35\x33\x36\x38\x37\x30\x39\x31\x31\x2f\x37\x36\x36\x32\x34\x37\x37"
         "\x37\x30\x34\x33\x32\x39\x34\x34\x34\x32\x39\x31\x37\x39\x31\x37\x33"
         "\x35\x31\x33\x35\x37\x35\x31\x35\x34\x35\x39\x31\x38\x30\x39\x33\x36"
         "\x39\x35\x36\x31\x30\x39\x31\x38\x30\x31\x30\x38\x38",
         64},
     false, false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x61\x66\x74\x65"
         "\x72\x5f\x73\x75\x62\x6e\x6f\x72\x6d\x61\x6c\x5f\x6e\x6f\x72\x6d\x61"
         "\x6c\x5f\x62\x6f\x75\x6e\x64\x61\x72\x79\x5f\x74\x69\x65",
         48},
     0xb80fffffe0000001ULL, std::optional<std::uint32_t>{0x80800000U},
     std::string_view{
         "\x2d\x39\x30\x30\x37\x31\x39\x38\x37\x31\x37\x38\x37\x30\x30\x38\x31"
         "\x2f\x37\x36\x36\x32\x34\x37\x37\x37\x30\x34\x33\x32\x39\x34\x34\x34"
         "\x32\x39\x31\x37\x39\x31\x37\x33\x35\x31\x33\x35\x37\x35\x31\x35\x34"
         "\x35\x39\x31\x38\x30\x39\x33\x36\x39\x35\x36\x31\x30\x39\x31\x38\x30"
         "\x31\x30\x38\x38",
         72},
     std::string_view{
         "\x35\x33\x36\x38\x37\x30\x39\x31\x31\x2f\x37\x36\x36\x32\x34\x37\x37"
         "\x37\x30\x34\x33\x32\x39\x34\x34\x34\x32\x39\x31\x37\x39\x31\x37\x33"
         "\x35\x31\x33\x35\x37\x35\x31\x35\x34\x35\x39\x31\x38\x30\x39\x33\x36"
         "\x39\x35\x36\x31\x30\x39\x31\x38\x30\x31\x30\x38\x38",
         64},
     false, false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x62"
                      "\x65\x66\x6f\x72\x65\x5f\x6f\x76\x65\x72\x66\x6c\x6f\x77"
                      "\x5f\x74\x68\x72\x65\x73\x68\x6f\x6c\x64",
                      38},
     0x47efffffefffffffULL, std::optional<std::uint32_t>{0x7f7fffffU},
     std::string_view{"\x33\x34\x30\x32\x38\x32\x33\x35\x36\x37\x37\x39\x37\x33"
                      "\x33\x36\x32\x33\x38\x35\x38\x36\x30\x37\x35\x33\x32\x35"
                      "\x30\x30\x39\x38\x30\x38\x35\x38\x38\x38\x30",
                      39},
     std::string_view{
         "\x31\x30\x31\x34\x31\x32\x30\x34\x37\x36\x34\x30\x34\x36\x39\x30\x33"
         "\x33\x34\x39\x30\x31\x36\x34\x36\x33\x39\x33\x33\x34\x34\x30",
         32},
     false, true, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x62"
                      "\x65\x66\x6f\x72\x65\x5f\x6f\x76\x65\x72\x66\x6c\x6f\x77"
                      "\x5f\x74\x68\x72\x65\x73\x68\x6f\x6c\x64",
                      38},
     0xc7efffffefffffffULL, std::optional<std::uint32_t>{0xff7fffffU},
     std::string_view{"\x2d\x33\x34\x30\x32\x38\x32\x33\x35\x36\x37\x37\x39\x37"
                      "\x33\x33\x36\x32\x33\x38\x35\x38\x36\x30\x37\x35\x33\x32"
                      "\x35\x30\x30\x39\x38\x30\x38\x35\x38\x38\x38\x30",
                      40},
     std::string_view{
         "\x31\x30\x31\x34\x31\x32\x30\x34\x37\x36\x34\x30\x34\x36\x39\x30\x33"
         "\x33\x34\x39\x30\x31\x36\x34\x36\x33\x39\x33\x33\x34\x34\x30",
         32},
     false, true, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x61"
                      "\x66\x74\x65\x72\x5f\x6f\x76\x65\x72\x66\x6c\x6f\x77\x5f"
                      "\x74\x68\x72\x65\x73\x68\x6f\x6c\x64",
                      37},
     0x47effffff0000001ULL, std::optional<std::uint32_t>{0x7f800000U},
     std::string_view{"\x33\x34\x30\x32\x38\x32\x33\x35\x36\x37\x37\x39\x37\x33"
                      "\x33\x36\x39\x39\x34\x31\x36\x34\x37\x31\x32\x35\x38\x34"
                      "\x31\x35\x33\x30\x34\x32\x37\x38\x30\x31\x36",
                      39},
     std::nullopt, false, true, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x46\x36\x34\x5f\x61"
                      "\x66\x74\x65\x72\x5f\x6f\x76\x65\x72\x66\x6c\x6f\x77\x5f"
                      "\x74\x68\x72\x65\x73\x68\x6f\x6c\x64",
                      37},
     0xc7effffff0000001ULL, std::optional<std::uint32_t>{0xff800000U},
     std::string_view{"\x2d\x33\x34\x30\x32\x38\x32\x33\x35\x36\x37\x37\x39\x37"
                      "\x33\x33\x36\x39\x39\x34\x31\x36\x34\x37\x31\x32\x35\x38"
                      "\x34\x31\x35\x33\x30\x34\x32\x37\x38\x30\x31\x36",
                      40},
     std::nullopt, false, true, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6d\x61\x78\x69\x6d"
                      "\x75\x6d\x5f\x46\x36\x34\x5f\x66\x69\x6e\x69\x74\x65",
                      27},
     0x7fefffffffffffffULL, std::optional<std::uint32_t>{0x7f800000U},
     std::string_view{
         "\x31\x37\x39\x37\x36\x39\x33\x31\x33\x34\x38\x36\x32\x33\x31\x35\x37"
         "\x30\x38\x31\x34\x35\x32\x37\x34\x32\x33\x37\x33\x31\x37\x30\x34\x33"
         "\x35\x36\x37\x39\x38\x30\x37\x30\x35\x36\x37\x35\x32\x35\x38\x34\x34"
         "\x39\x39\x36\x35\x39\x38\x39\x31\x37\x34\x37\x36\x38\x30\x33\x31\x35"
         "\x37\x32\x36\x30\x37\x38\x30\x30\x32\x38\x35\x33\x38\x37\x36\x30\x35"
         "\x38\x39\x35\x35\x38\x36\x33\x32\x37\x36\x36\x38\x37\x38\x31\x37\x31"
         "\x35\x34\x30\x34\x35\x38\x39\x35\x33\x35\x31\x34\x33\x38\x32\x34\x36"
         "\x34\x32\x33\x34\x33\x32\x31\x33\x32\x36\x38\x38\x39\x34\x36\x34\x31"
         "\x38\x32\x37\x36\x38\x34\x36\x37\x35\x34\x36\x37\x30\x33\x35\x33\x37"
         "\x35\x31\x36\x39\x38\x36\x30\x34\x39\x39\x31\x30\x35\x37\x36\x35\x35"
         "\x31\x32\x38\x32\x30\x37\x36\x32\x34\x35\x34\x39\x30\x30\x39\x30\x33"
         "\x38\x39\x33\x32\x38\x39\x34\x34\x30\x37\x35\x38\x36\x38\x35\x30\x38"
         "\x34\x35\x35\x31\x33\x33\x39\x34\x32\x33\x30\x34\x35\x38\x33\x32\x33"
         "\x36\x39\x30\x33\x32\x32\x32\x39\x34\x38\x31\x36\x35\x38\x30\x38\x35"
         "\x35\x39\x33\x33\x32\x31\x32\x33\x33\x34\x38\x32\x37\x34\x37\x39\x37"
         "\x38\x32\x36\x32\x30\x34\x31\x34\x34\x37\x32\x33\x31\x36\x38\x37\x33"
         "\x38\x31\x37\x37\x31\x38\x30\x39\x31\x39\x32\x39\x39\x38\x38\x31\x32"
         "\x35\x30\x34\x30\x34\x30\x32\x36\x31\x38\x34\x31\x32\x34\x38\x35\x38"
         "\x33\x36\x38",
         309},
     std::nullopt, false, true, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x6d\x61\x78\x69\x6d"
                      "\x75\x6d\x5f\x46\x36\x34\x5f\x66\x69\x6e\x69\x74\x65",
                      27},
     0xffefffffffffffffULL, std::optional<std::uint32_t>{0xff800000U},
     std::string_view{
         "\x2d\x31\x37\x39\x37\x36\x39\x33\x31\x33\x34\x38\x36\x32\x33\x31\x35"
         "\x37\x30\x38\x31\x34\x35\x32\x37\x34\x32\x33\x37\x33\x31\x37\x30\x34"
         "\x33\x35\x36\x37\x39\x38\x30\x37\x30\x35\x36\x37\x35\x32\x35\x38\x34"
         "\x34\x39\x39\x36\x35\x39\x38\x39\x31\x37\x34\x37\x36\x38\x30\x33\x31"
         "\x35\x37\x32\x36\x30\x37\x38\x30\x30\x32\x38\x35\x33\x38\x37\x36\x30"
         "\x35\x38\x39\x35\x35\x38\x36\x33\x32\x37\x36\x36\x38\x37\x38\x31\x37"
         "\x31\x35\x34\x30\x34\x35\x38\x39\x35\x33\x35\x31\x34\x33\x38\x32\x34"
         "\x36\x34\x32\x33\x34\x33\x32\x31\x33\x32\x36\x38\x38\x39\x34\x36\x34"
         "\x31\x38\x32\x37\x36\x38\x34\x36\x37\x35\x34\x36\x37\x30\x33\x35\x33"
         "\x37\x35\x31\x36\x39\x38\x36\x30\x34\x39\x39\x31\x30\x35\x37\x36\x35"
         "\x35\x31\x32\x38\x32\x30\x37\x36\x32\x34\x35\x34\x39\x30\x30\x39\x30"
         "\x33\x38\x39\x33\x32\x38\x39\x34\x34\x30\x37\x35\x38\x36\x38\x35\x30"
         "\x38\x34\x35\x35\x31\x33\x33\x39\x34\x32\x33\x30\x34\x35\x38\x33\x32"
         "\x33\x36\x39\x30\x33\x32\x32\x32\x39\x34\x38\x31\x36\x35\x38\x30\x38"
         "\x35\x35\x39\x33\x33\x32\x31\x32\x33\x33\x34\x38\x32\x37\x34\x37\x39"
         "\x37\x38\x32\x36\x32\x30\x34\x31\x34\x34\x37\x32\x33\x31\x36\x38\x37"
         "\x33\x38\x31\x37\x37\x31\x38\x30\x39\x31\x39\x32\x39\x39\x38\x38\x31"
         "\x32\x35\x30\x34\x30\x34\x30\x32\x36\x31\x38\x34\x31\x32\x34\x38\x35"
         "\x38\x33\x36\x38",
         310},
     std::nullopt, false, true, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x69\x6e\x66\x69\x6e\x69\x74\x79",
         17},
     0x7ff0000000000000ULL, std::optional<std::uint32_t>{0x7f800000U},
     std::nullopt, std::nullopt, true, false, false},
    {std::string_view{
         "\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x69\x6e\x66\x69\x6e\x69\x74\x79",
         17},
     0xfff0000000000000ULL, std::optional<std::uint32_t>{0xff800000U},
     std::nullopt, std::nullopt, true, false, false},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x71\x75\x69\x65\x74"
                      "\x5f\x6e\x61\x6e\x5f\x70\x61\x79\x6c\x6f\x61\x64",
                      26},
     0x7ff8000000004321ULL, std::nullopt, std::nullopt, std::nullopt, true,
     false, false},
    {std::string_view{"\x6e\x65\x67\x61\x74\x69\x76\x65\x5f\x71\x75\x69\x65\x74"
                      "\x5f\x6e\x61\x6e\x5f\x70\x61\x79\x6c\x6f\x61\x64",
                      26},
     0xfff8000000009876ULL, std::nullopt, std::nullopt, std::nullopt, true,
     false, false},
    {std::string_view{
         "\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x73\x69\x67\x6e\x61\x6c\x69\x6e"
         "\x67\x5f\x6e\x61\x6e\x5f\x70\x61\x79\x6c\x6f\x61\x64",
         30},
     0x7ff0000000000001ULL, std::nullopt, std::nullopt, std::nullopt, true,
     false, false},
}};
inline std::span<const ScalarBoundary> ScalarCases() noexcept {
  return kScalarCases;
}
struct TriangleFaceOracle {
  IndexTriple vertices;
  FractionTriple source_normal;
  FractionTriple reference_normal;
  std::string_view normal_dot;
  FractionTriple source_edges_squared;
  FractionTriple reference_edges_squared;
  bool collapsed;
};
struct TriangleBoundary {
  std::string_view name;
  std::span<const WordTriple64> source_words;
  std::span<const WordTriple32> reference_words;
  std::span<const IndexTriple> faces;
  std::uint64_t bound_f64;
  std::string_view max_component_error;
  std::span<const TriangleFaceOracle> face_oracles;
  std::span<const std::array<std::uint64_t, 2>> original_coincidences;
  std::span<const std::array<std::uint64_t, 2>> new_coincidences;
  geometry::MeshMaterializationInput input;
};
inline constexpr std::array<WordTriple64, 3> kTriangleCase0SourceWords = {{
    {0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x0000000000000000ULL},
    {0x3ff0000000400000ULL, 0x3ff0000000000000ULL, 0x0000000000000000ULL},
    {0x3ff0000000000000ULL, 0x4000000000000000ULL, 0x0000000000000000ULL},
}};
inline constexpr std::array<double, 9> kTriangleCase0Positions = {{
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000400000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
}};
inline constexpr std::array<WordTriple32, 3> kTriangleCase0ReferenceWords = {{
    {0x3f800000U, 0x3f800000U, 0x00000000U},
    {0x3f800000U, 0x3f800000U, 0x00000000U},
    {0x3f800000U, 0x40000000U, 0x00000000U},
}};
inline constexpr std::array<IndexTriple, 1> kTriangleCase0Faces = {{
    {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
}};
inline constexpr std::array<geometry::index_t, 2> kTriangleCase0Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000003ULL,
}};
inline constexpr std::array<geometry::index_t, 3> kTriangleCase0Corners = {{
    0x0000000000000000ULL,
    0x0000000000000001ULL,
    0x0000000000000002ULL,
}};
inline constexpr std::array<TriangleFaceOracle, 1> kTriangleCase0FaceOracles = {
    {
        {{0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x31\x2f\x31\x30\x37\x33\x37\x34\x31\x38\x32\x34",
                           12}},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x30", 1}},
         std::string_view{"\x30", 1},
         {std::string_view{"\x31\x2f\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34"
                           "\x36\x30\x36\x38\x34\x36\x39\x37\x36",
                           21},
          std::string_view{
              "\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34\x36\x30\x36\x38\x34\x36"
              "\x39\x37\x37\x2f\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34\x36\x30"
              "\x36\x38\x34\x36\x39\x37\x36",
              39},
          std::string_view{"\x31", 1}},
         {std::string_view{"\x30", 1}, std::string_view{"\x31", 1},
          std::string_view{"\x31", 1}},
         true},
    }};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase0OriginalCoincidences = {{}};
inline constexpr std::array<std::array<std::uint64_t, 2>, 1>
    kTriangleCase0NewCoincidences = {{
        {0x0000000000000000ULL, 0x0000000000000001ULL},
    }};
inline constexpr std::array<WordTriple64, 3> kTriangleCase1SourceWords = {{
    {0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x0000000000000000ULL},
    {0x4000000000000000ULL, 0x4000000000200000ULL, 0x0000000000000000ULL},
    {0x4008000000000000ULL, 0x4008000000000000ULL, 0x0000000000000000ULL},
}};
inline constexpr std::array<double, 9> kTriangleCase1Positions = {{
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4000000000200000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
}};
inline constexpr std::array<WordTriple32, 3> kTriangleCase1ReferenceWords = {{
    {0x3f800000U, 0x3f800000U, 0x00000000U},
    {0x40000000U, 0x40000000U, 0x00000000U},
    {0x40400000U, 0x40400000U, 0x00000000U},
}};
inline constexpr std::array<IndexTriple, 1> kTriangleCase1Faces = {{
    {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
}};
inline constexpr std::array<geometry::index_t, 2> kTriangleCase1Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000003ULL,
}};
inline constexpr std::array<geometry::index_t, 3> kTriangleCase1Corners = {{
    0x0000000000000000ULL,
    0x0000000000000001ULL,
    0x0000000000000002ULL,
}};
inline constexpr std::array<TriangleFaceOracle, 1> kTriangleCase1FaceOracles = {
    {
        {{0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x2d\x31\x2f\x35\x33\x36\x38\x37\x30\x39\x31\x32",
                           12}},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x30", 1}},
         std::string_view{"\x30", 1},
         {std::string_view{
              "\x32\x33\x30\x35\x38\x34\x33\x30\x31\x31\x33\x36\x31\x31\x37\x37"
              "\x36\x30\x31\x2f\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34\x36\x30"
              "\x36\x38\x34\x36\x39\x37\x36",
              39},
          std::string_view{
              "\x32\x33\x30\x35\x38\x34\x33\x30\x30\x37\x30\x36\x36\x32\x31\x30"
              "\x33\x30\x35\x2f\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34\x36\x30"
              "\x36\x38\x34\x36\x39\x37\x36",
              39},
          std::string_view{"\x38", 1}},
         {std::string_view{"\x32", 1}, std::string_view{"\x32", 1},
          std::string_view{"\x38", 1}},
         true},
    }};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase1OriginalCoincidences = {{}};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase1NewCoincidences = {{}};
inline constexpr std::array<WordTriple64, 3> kTriangleCase2SourceWords = {{
    {0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x0000000000000000ULL},
    {0x4000000000000000ULL, 0x400000000fe00000ULL, 0x0000000000000000ULL},
    {0x4008000000000000ULL, 0x4008000010200000ULL, 0x0000000000000000ULL},
}};
inline constexpr std::array<double, 9> kTriangleCase2Positions = {{
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x400000000fe00000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x4008000010200000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
}};
inline constexpr std::array<WordTriple32, 3> kTriangleCase2ReferenceWords = {{
    {0x3f800000U, 0x3f800000U, 0x00000000U},
    {0x40000000U, 0x40000000U, 0x00000000U},
    {0x40400000U, 0x40400001U, 0x00000000U},
}};
inline constexpr std::array<IndexTriple, 1> kTriangleCase2Faces = {{
    {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
}};
inline constexpr std::array<geometry::index_t, 2> kTriangleCase2Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000003ULL,
}};
inline constexpr std::array<geometry::index_t, 3> kTriangleCase2Corners = {{
    0x0000000000000000ULL,
    0x0000000000000001ULL,
    0x0000000000000002ULL,
}};
inline constexpr std::array<TriangleFaceOracle, 1> kTriangleCase2FaceOracles = {
    {
        {{0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{
              "\x2d\x31\x32\x35\x2f\x31\x30\x37\x33\x37\x34\x31\x38\x32\x34",
              15}},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x31\x2f\x34\x31\x39\x34\x33\x30\x34", 9}},
         std::string_view{"\x2d\x31\x32\x35\x2f\x34\x35\x30\x33\x35\x39\x39\x36"
                          "\x32\x37\x33\x37\x30\x34\x39\x36",
                          21},
         {std::string_view{
              "\x32\x33\x30\x35\x38\x34\x33\x32\x38\x31\x39\x34\x34\x31\x33\x33"
              "\x33\x37\x37\x2f\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34\x36\x30"
              "\x36\x38\x34\x36\x39\x37\x36",
              39},
          std::string_view{
              "\x35\x37\x36\x34\x36\x30\x37\x35\x33\x33\x37\x37\x31\x36\x35\x33"
              "\x31\x33\x2f\x32\x38\x38\x32\x33\x30\x33\x37\x36\x31\x35\x31\x37"
              "\x31\x31\x37\x34\x34",
              37},
          std::string_view{
              "\x39\x32\x32\x33\x33\x37\x32\x35\x39\x30\x39\x30\x35\x35\x37\x33"
              "\x36\x33\x33\x2f\x31\x31\x35\x32\x39\x32\x31\x35\x30\x34\x36\x30"
              "\x36\x38\x34\x36\x39\x37\x36",
              39}},
         {std::string_view{"\x32", 1},
          std::string_view{
              "\x33\x35\x31\x38\x34\x33\x38\x30\x34\x37\x37\x34\x34\x31\x2f\x31"
              "\x37\x35\x39\x32\x31\x38\x36\x30\x34\x34\x34\x31\x36",
              29},
          std::string_view{
              "\x31\x34\x30\x37\x33\x37\x35\x30\x35\x31\x33\x32\x35\x34\x35\x2f"
              "\x31\x37\x35\x39\x32\x31\x38\x36\x30\x34\x34\x34\x31\x36",
              30}},
         false},
    }};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase2OriginalCoincidences = {{}};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase2NewCoincidences = {{}};
inline constexpr std::array<WordTriple64, 3> kTriangleCase3SourceWords = {{
    {0x3fc0000000000000ULL, 0xbfd0000000000000ULL, 0x3fd8000000000000ULL},
    {0x3ff2000000000000ULL, 0x3fd0000000000000ULL, 0x3fc0000000000000ULL},
    {0xbfd8000000000000ULL, 0x3fe8000000000000ULL, 0x3ff2000000000000ULL},
}};
inline constexpr std::array<double, 9> kTriangleCase3Positions = {{
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fd0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fc0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbfd8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3fe8000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff2000000000000ULL}),
}};
inline constexpr std::array<WordTriple32, 3> kTriangleCase3ReferenceWords = {{
    {0x3e000000U, 0xbe800000U, 0x3ec00000U},
    {0x3f900000U, 0x3e800000U, 0x3e000000U},
    {0xbec00000U, 0x3f400000U, 0x3f900000U},
}};
inline constexpr std::array<IndexTriple, 1> kTriangleCase3Faces = {{
    {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
}};
inline constexpr std::array<geometry::index_t, 2> kTriangleCase3Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000003ULL,
}};
inline constexpr std::array<geometry::index_t, 3> kTriangleCase3Corners = {{
    0x0000000000000000ULL,
    0x0000000000000001ULL,
    0x0000000000000002ULL,
}};
inline constexpr std::array<TriangleFaceOracle, 1> kTriangleCase3FaceOracles = {
    {
        {{0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL},
         {std::string_view{"\x35\x2f\x38", 3},
          std::string_view{"\x2d\x35\x2f\x38", 4},
          std::string_view{"\x35\x2f\x34", 3}},
         {std::string_view{"\x35\x2f\x38", 3},
          std::string_view{"\x2d\x35\x2f\x38", 4},
          std::string_view{"\x35\x2f\x34", 3}},
         std::string_view{"\x37\x35\x2f\x33\x32", 5},
         {std::string_view{"\x32\x31\x2f\x31\x36", 5},
          std::string_view{"\x37\x2f\x32", 3},
          std::string_view{"\x32\x39\x2f\x31\x36", 5}},
         {std::string_view{"\x32\x31\x2f\x31\x36", 5},
          std::string_view{"\x37\x2f\x32", 3},
          std::string_view{"\x32\x39\x2f\x31\x36", 5}},
         false},
    }};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase3OriginalCoincidences = {{}};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase3NewCoincidences = {{}};
inline constexpr std::array<WordTriple64, 6> kTriangleCase4SourceWords = {{
    {0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL},
    {0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL},
    {0x3ff0000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL},
    {0x0000000000000000ULL, 0x3ff0000000000000ULL, 0x0000000000000000ULL},
    {0xbff0000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL},
    {0x0000000000000000ULL, 0xbff0000000000000ULL, 0x0000000000000000ULL},
}};
inline constexpr std::array<double, 18> kTriangleCase4Positions = {{
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0xbff0000000000000ULL}),
    std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}),
}};
inline constexpr std::array<WordTriple32, 6> kTriangleCase4ReferenceWords = {{
    {0x00000000U, 0x00000000U, 0x00000000U},
    {0x00000000U, 0x00000000U, 0x00000000U},
    {0x3f800000U, 0x00000000U, 0x00000000U},
    {0x00000000U, 0x3f800000U, 0x00000000U},
    {0xbf800000U, 0x00000000U, 0x00000000U},
    {0x00000000U, 0xbf800000U, 0x00000000U},
}};
inline constexpr std::array<IndexTriple, 2> kTriangleCase4Faces = {{
    {0x0000000000000000ULL, 0x0000000000000002ULL, 0x0000000000000003ULL},
    {0x0000000000000001ULL, 0x0000000000000004ULL, 0x0000000000000005ULL},
}};
inline constexpr std::array<geometry::index_t, 3> kTriangleCase4Offsets = {{
    0x0000000000000000ULL,
    0x0000000000000003ULL,
    0x0000000000000006ULL,
}};
inline constexpr std::array<geometry::index_t, 6> kTriangleCase4Corners = {{
    0x0000000000000000ULL,
    0x0000000000000002ULL,
    0x0000000000000003ULL,
    0x0000000000000001ULL,
    0x0000000000000004ULL,
    0x0000000000000005ULL,
}};
inline constexpr std::array<TriangleFaceOracle, 2> kTriangleCase4FaceOracles = {
    {
        {{0x0000000000000000ULL, 0x0000000000000002ULL, 0x0000000000000003ULL},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x31", 1}},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x31", 1}},
         std::string_view{"\x31", 1},
         {std::string_view{"\x31", 1}, std::string_view{"\x32", 1},
          std::string_view{"\x31", 1}},
         {std::string_view{"\x31", 1}, std::string_view{"\x32", 1},
          std::string_view{"\x31", 1}},
         false},
        {{0x0000000000000001ULL, 0x0000000000000004ULL, 0x0000000000000005ULL},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x31", 1}},
         {std::string_view{"\x30", 1}, std::string_view{"\x30", 1},
          std::string_view{"\x31", 1}},
         std::string_view{"\x31", 1},
         {std::string_view{"\x31", 1}, std::string_view{"\x32", 1},
          std::string_view{"\x31", 1}},
         {std::string_view{"\x31", 1}, std::string_view{"\x32", 1},
          std::string_view{"\x31", 1}},
         false},
    }};
inline constexpr std::array<std::array<std::uint64_t, 2>, 1>
    kTriangleCase4OriginalCoincidences = {{
        {0x0000000000000000ULL, 0x0000000000000001ULL},
    }};
inline constexpr std::array<std::array<std::uint64_t, 2>, 0>
    kTriangleCase4NewCoincidences = {{}};
inline constexpr std::array<TriangleBoundary, 5> kTriangleCases = {{
    {std::string_view{
         "\x6e\x65\x77\x5f\x63\x6f\x69\x6e\x63\x69\x64\x65\x6e\x63\x65", 15},
     kTriangleCase0SourceWords,
     kTriangleCase0ReferenceWords,
     kTriangleCase0Faces,
     0x3e20000000000000ULL,
     std::string_view{"\x31\x2f\x31\x30\x37\x33\x37\x34\x31\x38\x32\x34", 12},
     kTriangleCase0FaceOracles,
     kTriangleCase0OriginalCoincidences,
     kTriangleCase0NewCoincidences,
     {std::span<const double>{kTriangleCase0Positions},
      geometry::PolygonTopologyInput{kTriangleCase0Offsets,
                                     kTriangleCase0Corners},
      {}}},
    {std::string_view{"\x64\x69\x73\x74\x69\x6e\x63\x74\x5f\x72\x6f\x75\x6e\x64"
                      "\x65\x64\x5f\x76\x65\x72\x74\x69\x63\x65\x73\x5f\x63\x6f"
                      "\x6c\x6c\x69\x6e\x65\x61\x72",
                      35},
     kTriangleCase1SourceWords,
     kTriangleCase1ReferenceWords,
     kTriangleCase1Faces,
     0x3e20000000000000ULL,
     std::string_view{"\x31\x2f\x31\x30\x37\x33\x37\x34\x31\x38\x32\x34", 12},
     kTriangleCase1FaceOracles,
     kTriangleCase1OriginalCoincidences,
     kTriangleCase1NewCoincidences,
     {std::span<const double>{kTriangleCase1Positions},
      geometry::PolygonTopologyInput{kTriangleCase1Offsets,
                                     kTriangleCase1Corners},
      {}}},
    {std::string_view{"\x6f\x72\x69\x65\x6e\x74\x61\x74\x69\x6f\x6e\x5f\x72\x65"
                      "\x76\x65\x72\x73\x61\x6c",
                      20},
     kTriangleCase2SourceWords,
     kTriangleCase2ReferenceWords,
     kTriangleCase2Faces,
     0x3e90000000000000ULL,
     std::string_view{
         "\x31\x32\x37\x2f\x31\x30\x37\x33\x37\x34\x31\x38\x32\x34", 14},
     kTriangleCase2FaceOracles,
     kTriangleCase2OriginalCoincidences,
     kTriangleCase2NewCoincidences,
     {std::span<const double>{kTriangleCase2Positions},
      geometry::PolygonTopologyInput{kTriangleCase2Offsets,
                                     kTriangleCase2Corners},
      {}}},
    {std::string_view{"\x70\x6f\x73\x69\x74\x69\x76\x65\x5f\x6f\x72\x69\x67\x69"
                      "\x6e\x61\x6c\x5f\x64\x79\x61\x64\x69\x63",
                      24},
     kTriangleCase3SourceWords,
     kTriangleCase3ReferenceWords,
     kTriangleCase3Faces,
     0x3e20000000000000ULL,
     std::string_view{"\x30", 1},
     kTriangleCase3FaceOracles,
     kTriangleCase3OriginalCoincidences,
     kTriangleCase3NewCoincidences,
     {std::span<const double>{kTriangleCase3Positions},
      geometry::PolygonTopologyInput{kTriangleCase3Offsets,
                                     kTriangleCase3Corners},
      {}}},
    {std::string_view{"\x70\x72\x65\x65\x78\x69\x73\x74\x69\x6e\x67\x5f\x64\x69"
                      "\x73\x74\x69\x6e\x63\x74\x5f\x63\x6f\x69\x6e\x63\x69\x64"
                      "\x65\x6e\x74\x5f\x49\x44\x73",
                      35},
     kTriangleCase4SourceWords,
     kTriangleCase4ReferenceWords,
     kTriangleCase4Faces,
     0x3e20000000000000ULL,
     std::string_view{"\x30", 1},
     kTriangleCase4FaceOracles,
     kTriangleCase4OriginalCoincidences,
     kTriangleCase4NewCoincidences,
     {std::span<const double>{kTriangleCase4Positions},
      geometry::PolygonTopologyInput{kTriangleCase4Offsets,
                                     kTriangleCase4Corners},
      {}}},
}};
inline std::span<const TriangleBoundary> TriangleCases() noexcept {
  return kTriangleCases;
}
}  // namespace meshvale::interchange::surface_fixture
#endif  // MESHVALE_INTERCHANGE_TESTS_SURFACE_FIXTURE_H_
