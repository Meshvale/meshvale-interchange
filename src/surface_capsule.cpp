// SPDX-License-Identifier: Apache-2.0
#include "surface_capsule.h"

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/charged_triangulation.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/gltf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_allocation.h"
#include "gltf_json_snapshot.h"
#include "surface_numeric.h"
#include "surface_output_internal.h"

namespace meshvale::interchange::surface_detail {
namespace {
namespace capsule_key {
constexpr std::string_view kAccessor = "accessor";
constexpr std::string_view kAcknowledgeOrdinaryViewLoss =
    "acknowledge_ordinary_view_loss";
constexpr std::string_view kAcknowledgments = "acknowledgments";
constexpr std::string_view kAttributeBytes = "attribute_bytes";
constexpr std::string_view kAttributeOffsets = "attribute_offsets";
constexpr std::string_view kAttributeScalars = "attribute_scalars";
constexpr std::string_view kAttributes = "attributes";
constexpr std::string_view kBinBytes = "bin_bytes";
constexpr std::string_view kBinOffset = "bin_offset";
constexpr std::string_view kBindings = "bindings";
constexpr std::string_view kBound = "bound";
constexpr std::string_view kBytes = "bytes";
constexpr std::string_view kCatalog = "catalog";
constexpr std::string_view kCatalogSet = "catalog_set";
constexpr std::string_view kChangedValues = "changed_values";
constexpr std::string_view kChannels = "channels";
constexpr std::string_view kComplete = "complete";
constexpr std::string_view kComponent = "component";
constexpr std::string_view kComponents = "components";
constexpr std::string_view kCoordinates = "coordinates";
constexpr std::string_view kCornerCorners = "corner_corners";
constexpr std::string_view kCornerExpansion = "corner_expansion";
constexpr std::string_view kCornerSources = "corner_sources";
constexpr std::string_view kCornerVertices = "corner_vertices";
constexpr std::string_view kCorners = "corners";
constexpr std::string_view kCount = "count";
constexpr std::string_view kCounts = "counts";
constexpr std::string_view kDeclaredUnitHex = "declared_unit_hex";
constexpr std::string_view kDefinitions = "definitions";
constexpr std::string_view kDescriptorBytes = "descriptor_bytes";
constexpr std::string_view kDomain = "domain";
constexpr std::string_view kEncoding = "encoding";
constexpr std::string_view kExaminedValues = "examined_values";
constexpr std::string_view kExponent2 = "exponent2";
constexpr std::string_view kFaceOffsets = "face_offsets";
constexpr std::string_view kFaceOutputOffsets = "face_output_offsets";
constexpr std::string_view kFaceSources = "face_sources";
constexpr std::string_view kFaces = "faces";
constexpr std::string_view kField = "field";
constexpr std::string_view kFirstWireTriangle = "first_wire_triangle";
constexpr std::string_view kFirstWireVertex = "first_wire_vertex";
constexpr std::string_view kImages = "images";
constexpr std::string_view kIndexAccessor = "index_accessor";
constexpr std::string_view kInverseUvMapping = "inverse_uv_mapping";
constexpr std::string_view kKeyHex = "key_hex";
constexpr std::string_view kKind = "kind";
constexpr std::string_view kListedMemberBytes = "listed_member_bytes";
constexpr std::string_view kListedMembers = "listed_members";
constexpr std::string_view kMapEntries = "map_entries";
constexpr std::string_view kMaps = "maps";
constexpr std::string_view kMaterial = "material";
constexpr std::string_view kMaterials = "materials";
constexpr std::string_view kMaxAbsoluteError = "max_absolute_error";
constexpr std::string_view kMembers = "members";
constexpr std::string_view kMetadata = "metadata";
constexpr std::string_view kMetadataEntries = "metadata_entries";
constexpr std::string_view kMime = "mime";
constexpr std::string_view kMode = "mode";
constexpr std::string_view kNameHex = "name_hex";
constexpr std::string_view kNumerator = "numerator";
constexpr std::string_view kNumeric = "numeric";
constexpr std::string_view kObserved = "observed";
constexpr std::string_view kOffset = "offset";
constexpr std::string_view kOffsets = "offsets";
constexpr std::string_view kOrdinal = "ordinal";
constexpr std::string_view kOrdinaryMember = "ordinary_member";
constexpr std::string_view kOrigin = "origin";
constexpr std::string_view kOriginal = "original";
constexpr std::string_view kPath = "path";
constexpr std::string_view kPositionAccessor = "position_accessor";
constexpr std::string_view kPositions = "positions";
constexpr std::string_view kPresenceBytes = "presence_bytes";
constexpr std::string_view kPresent = "present";
constexpr std::string_view kPrimitives = "primitives";
constexpr std::string_view kRawJson = "raw_json";
constexpr std::string_view kRepresentation = "representation";
constexpr std::string_view kResourceKind = "resource_kind";
constexpr std::string_view kResourceTableIndex = "resource_table_index";
constexpr std::string_view kRole = "role";
constexpr std::string_view kSamplers = "samplers";
constexpr std::string_view kScalar = "scalar";
constexpr std::string_view kSchema = "schema";
constexpr std::string_view kSemanticHex = "semantic_hex";
constexpr std::string_view kSetIndex = "set_index";
constexpr std::string_view kSha256 = "sha256";
constexpr std::string_view kSourceAttribute = "source_attribute";
constexpr std::string_view kSourceBuffer = "source_buffer";
constexpr std::string_view kSourceBufferView = "source_buffer_view";
constexpr std::string_view kSourceCoreF32 = "source_core_f32";
constexpr std::string_view kSourceCornerMembers = "source_corner_members";
constexpr std::string_view kSourceCornerOffsets = "source_corner_offsets";
constexpr std::string_view kSourceFaceMembers = "source_face_members";
constexpr std::string_view kSourceFaceOffsets = "source_face_offsets";
constexpr std::string_view kSourceId = "source_id";
constexpr std::string_view kSourceImage = "source_image";
constexpr std::string_view kSourceJsonSha256 = "source_json_sha256";
constexpr std::string_view kSourceMaterial = "source_material";
constexpr std::string_view kSourceMaterialAttribute =
    "source_material_attribute";
constexpr std::string_view kSourceOrdinal = "source_ordinal";
constexpr std::string_view kSourceResource = "source_resource";
constexpr std::string_view kSourceRow = "source_row";
constexpr std::string_view kSourceSet = "source_set";
constexpr std::string_view kSourceVertexMembers = "source_vertex_members";
constexpr std::string_view kSourceVertexOffsets = "source_vertex_offsets";
constexpr std::string_view kTextures = "textures";
constexpr std::string_view kTriangleConversion = "triangle_conversion";
constexpr std::string_view kTriangleFaces = "triangle_faces";
constexpr std::string_view kTriangleSources = "triangle_sources";
constexpr std::string_view kTriangles = "triangles";
constexpr std::string_view kTriangulated = "triangulated";
constexpr std::string_view kTriangulation = "triangulation";
constexpr std::string_view kUnusedVertices = "unused_vertices";
constexpr std::string_view kUv = "uv";
constexpr std::string_view kUvAccessors = "uv_accessors";
constexpr std::string_view kUvMapping = "uv_mapping";
constexpr std::string_view kValueHex = "value_hex";
constexpr std::string_view kValues = "values";
constexpr std::string_view kVertexSources = "vertex_sources";
constexpr std::string_view kVertices = "vertices";
constexpr std::string_view kWire = "wire";
constexpr std::string_view kWireCoreF32 = "wire_core_f32";
constexpr std::string_view kWireImage = "wire_image";
constexpr std::string_view kWireMaterial = "wire_material";
constexpr std::string_view kWireOrdinal = "wire_ordinal";
constexpr std::string_view kWireSet = "wire_set";
constexpr std::string_view kWireTriangles = "wire_triangles";
constexpr std::string_view kWireVertices = "wire_vertices";
}  // namespace capsule_key
namespace capsule_tag {
constexpr std::string_view kGltfJson = "gltf-json";
constexpr std::string_view kGlb = "glb";
constexpr std::string_view kStlAscii = "stl-ascii";
constexpr std::string_view kStlBinary = "stl-binary";
constexpr std::string_view kGltf = "gltf";
constexpr std::string_view kStl = "stl";
constexpr std::string_view kGeometryBuffer = "geometry-buffer";
constexpr std::string_view kImage = "image";
constexpr std::string_view kCapsuleBinary = "capsule-binary";
constexpr std::string_view kExact32 = "exact32";
constexpr std::string_view kBoundedRne32 = "bounded-rne32";
constexpr std::string_view kExact64 = "exact64";
constexpr std::string_view kBundleOnly = "bundle-only";
constexpr std::string_view kStlCornerNormal = "stl-corner-normal";
constexpr std::string_view kMaterialId = "material-id";
constexpr std::string_view kVertex = "vertex";
constexpr std::string_view kFace = "face";
constexpr std::string_view kCorner = "corner";
constexpr std::string_view kF32 = "f32";
constexpr std::string_view kF64 = "f64";
constexpr std::string_view kI32 = "i32";
constexpr std::string_view kU8 = "u8";
constexpr std::string_view kU16 = "u16";
constexpr std::string_view kU32 = "u32";
constexpr std::string_view kU64 = "u64";
}  // namespace capsule_tag
constexpr std::uint64_t kRangeAlignment = 8;
constexpr std::size_t kShaBlockBytes = 64;
constexpr std::size_t kShaLengthBytes = 8;
constexpr std::size_t kByteBits = 8;
constexpr std::uint32_t kByteMask = 255;
constexpr std::size_t kHexPerByte = 2;
constexpr std::size_t kWord32Hex = 8;
constexpr std::size_t kWord64Bits = 64;
constexpr std::size_t kDyadicDigits = 656;
constexpr std::int64_t kDyadicExponentMinimum = -1074;
constexpr std::int64_t kDyadicExponentMaximum = 1025;
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr std::string_view kAnnotated = "annotated-bundle-v1";
constexpr std::string_view kDecoded = "decoded-wire";
// FIPS 180-4 SHA-256 initial words and round constants.
constexpr std::array<std::uint32_t, 8> kShaInitial{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
constexpr std::array<std::uint32_t, 64> kShaRounds{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

[[noreturn]] void Bad(Run& run, SurfaceCode code = SurfaceCode::kSchema) {
  Fail(SurfaceStatus::kVerificationFailed, run.stage, code);
}
void Require(Run& run, bool good, SurfaceCode code = SurfaceCode::kSchema) {
  if (!good) Bad(run, code);
}
std::uint64_t Sum(Run& run, std::uint64_t a, std::uint64_t b) {
  if (b > UINT64_MAX - a)
    Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kSizeOverflow);
  return AddCount(a, b);
}
std::uint64_t Times(Run& run, std::uint64_t a, std::uint64_t b) {
  if (a && b > UINT64_MAX / a)
    Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kSizeOverflow);
  return Product(a, b);
}
void Compress(std::array<std::uint32_t, 8>& hash,
              std::span<const std::byte, kShaBlockBytes> block) noexcept {
  std::array<std::uint32_t, kShaBlockBytes> words{};
  for (std::size_t i = 0; i < 16; ++i)
    for (std::size_t j = 0; j < 4; ++j)
      words[i] = (words[i] << kByteBits) |
                 std::to_integer<std::uint32_t>(block[i * 4 + j]);
  for (std::size_t i = 16; i < words.size(); ++i) {
    const auto a = words[i - 15], b = words[i - 2];
    words[i] = words[i - 16] + (std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3)) +
               words[i - 7] + (std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10));
  }
  auto a = hash[0], b = hash[1], c = hash[2], d = hash[3];
  auto e = hash[4], f = hash[5], g = hash[6], h = hash[7];
  for (std::size_t i = 0; i < words.size(); ++i) {
    const auto first = h +
                       (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                       ((e & f) ^ (~e & g)) + kShaRounds[i] + words[i];
    const auto second =
        (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
        ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  hash[0] += a;
  hash[1] += b;
  hash[2] += c;
  hash[3] += d;
  hash[4] += e;
  hash[5] += f;
  hash[6] += g;
  hash[7] += h;
}
std::string_view EncodingName(SurfaceEncoding encoding) {
  switch (encoding) {
    case SurfaceEncoding::kGltfJson:
      return capsule_tag::kGltfJson;
    case SurfaceEncoding::kGlb:
      return capsule_tag::kGlb;
    case SurfaceEncoding::kStlAscii:
      return capsule_tag::kStlAscii;
    case SurfaceEncoding::kStlBinary:
      return capsule_tag::kStlBinary;
    default:
      Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
           SurfaceCode::kUnknownEnum);
  }
}
std::string_view RoleName(SurfaceMemberKind kind) {
  switch (kind) {
    case SurfaceMemberKind::kGltf:
      return capsule_tag::kGltf;
    case SurfaceMemberKind::kGlb:
      return capsule_tag::kGlb;
    case SurfaceMemberKind::kStl:
      return capsule_tag::kStl;
    case SurfaceMemberKind::kGeometryBuffer:
      return capsule_tag::kGeometryBuffer;
    case SurfaceMemberKind::kImage:
      return capsule_tag::kImage;
    case SurfaceMemberKind::kCapsuleBinary:
      return capsule_tag::kCapsuleBinary;
    default:
      Fail(SurfaceStatus::kInvalidStorage, SurfaceStage::kEncode,
           SurfaceCode::kMemberInventory);
  }
}
std::string_view ModeName(SurfaceNarrowing mode) {
  switch (mode) {
    case SurfaceNarrowing::kExact32:
      return capsule_tag::kExact32;
    case SurfaceNarrowing::kBoundedRne32:
      return capsule_tag::kBoundedRne32;
    case SurfaceNarrowing::kExact64:
      return capsule_tag::kExact64;
    default:
      Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
           SurfaceCode::kUnknownEnum);
  }
}
std::string_view ChannelName(SurfaceChannelRole role) {
  switch (role) {
    case SurfaceChannelRole::kBundleOnly:
      return capsule_tag::kBundleOnly;
    case SurfaceChannelRole::kUv:
      return capsule_key::kUv;
    case SurfaceChannelRole::kStlCornerNormal:
      return capsule_tag::kStlCornerNormal;
    case SurfaceChannelRole::kMaterialId:
      return capsule_tag::kMaterialId;
    default:
      Fail(SurfaceStatus::kInvalidConfiguration, SurfaceStage::kConfiguration,
           SurfaceCode::kUnknownEnum);
  }
}
std::string_view DomainName(geometry::AttributeDomain domain) {
  switch (domain) {
    case geometry::AttributeDomain::vertex:
      return capsule_tag::kVertex;
    case geometry::AttributeDomain::face:
      return capsule_tag::kFace;
    case geometry::AttributeDomain::corner:
      return capsule_tag::kCorner;
  }
  Fail(SurfaceStatus::kInvalidStorage, SurfaceStage::kEncode,
       SurfaceCode::kChannelShape);
}
template <class T>
constexpr std::string_view ScalarName() {
  if constexpr (std::is_same_v<T, float>)
    return capsule_tag::kF32;
  else if constexpr (std::is_same_v<T, double>)
    return capsule_tag::kF64;
  else if constexpr (std::is_same_v<T, std::int32_t>)
    return capsule_tag::kI32;
  else if constexpr (std::is_same_v<T, std::uint8_t> ||
                     std::is_same_v<T, std::byte>)
    return capsule_tag::kU8;
  else if constexpr (std::is_same_v<T, std::uint16_t>)
    return capsule_tag::kU16;
  else if constexpr (std::is_same_v<T, std::uint32_t>)
    return capsule_tag::kU32;
  else {
    static_assert(std::is_same_v<T, std::uint64_t>);
    return capsule_tag::kU64;
  }
}
template <class T>
std::uint64_t ObjectWord(const T& value) noexcept {
  // Copy object bits, never promote floating attributes or quiet their
  // payloads.
  if constexpr (sizeof(T) == 8) {
    std::uint64_t word;
    std::memcpy(&word, &value, 8);
    return word;
  } else if constexpr (sizeof(T) == 4) {
    std::uint32_t word;
    std::memcpy(&word, &value, 4);
    return word;
  } else if constexpr (sizeof(T) == 2) {
    std::uint16_t word;
    std::memcpy(&word, &value, 2);
    return word;
  } else {
    static_assert(sizeof(T) == 1);
    std::uint8_t word;
    std::memcpy(&word, &value, 1);
    return word;
  }
}
unsigned HexDigit(char c, Run& run) {
  if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
  Bad(run);
}
using Node = std::size_t;
using Keys = std::initializer_list<std::string_view>;

class Json {
 public:
  explicit Json(Run& run, std::uint64_t byte_limit = UINT64_MAX)
      : run_(run),
        text_(gltf_detail::Allocator<char>(run.allocation)),
        frames_(gltf_detail::Allocator<Frame>(run.allocation)),
        byte_limit_(std::min(run.limits.max_capsule_json_bytes, byte_limit)) {}
  Json(Run& run, const gltf_detail::JsonSnapshot& snapshot) : Json(run) {
    snapshot_ = &snapshot;
  }
  bool Reading() const noexcept { return snapshot_ != nullptr; }
  const String& Text() const noexcept { return text_; }
  Node Root(Keys keys) { return Object(0, {}, keys); }
  Node Object(Node parent, std::string_view name, Keys keys) {
    if (!Reading()) {
      Prefix(name);
      Append("{");
      Push(false);
      return 0;
    }
    const auto node = name.empty() ? parent : Child(parent, name);
    Require(run_, snapshot_->Kind(node) == gltf_detail::JsonKind::kObject);
    std::uint64_t seen = 0;
    std::size_t count = 0;
    Require(run_, keys.size() < kWord64Bits);
    for (auto child = snapshot_->FirstChild(node); child;
         child = snapshot_->NextSibling(*child)) {
      run_.Check();
      const auto key = snapshot_->Key(*child);
      std::size_t index = 0;
      for (auto allowed : keys) {
        if (key == allowed) break;
        ++index;
      }
      Require(run_, index < keys.size());
      const auto bit = UINT64_C(1) << index;
      Require(run_, !(seen & bit));
      seen |= bit;
      ++count;
    }
    Require(run_, count == keys.size());
    return node;
  }
  void EndObject() {
    if (!Reading()) {
      frames_.pop_back();
      Append("}");
    }
  }
  template <class F>
  void Array(Node parent, std::string_view name, std::size_t count, F visit) {
    if (!Reading()) {
      Prefix(name);
      Append("[");
      Push(true);
      for (std::size_t i = 0; i < count; ++i) {
        run_.Check(i);
        visit(Node{0}, i);
      }
      frames_.pop_back();
      Append("]");
      return;
    }
    const auto node = Child(parent, name);
    Require(run_, snapshot_->Kind(node) == gltf_detail::JsonKind::kArray);
    std::size_t index = 0;
    for (auto child = snapshot_->FirstChild(node); child;
         child = snapshot_->NextSibling(*child)) {
      run_.Check(index);
      Require(run_, index < count);
      visit(*child, index++);
    }
    Require(run_, index == count);
  }
  Node Child(Node parent, std::string_view name) {
    if (name.empty()) return parent;
    for (auto child = snapshot_->FirstChild(parent); child;
         child = snapshot_->NextSibling(*child)) {
      run_.Check();
      if (snapshot_->Key(*child) == name) return *child;
    }
    Bad(run_);
  }
  bool Null(Node parent, std::string_view name) {
    return snapshot_->Kind(Child(parent, name)) == gltf_detail::JsonKind::kNull;
  }
  std::string_view ReadText(Node parent, std::string_view name) {
    const auto node = Child(parent, name);
    Require(run_, snapshot_->Kind(node) == gltf_detail::JsonKind::kString);
    return snapshot_->String(node);
  }
  void Text(Node parent, std::string_view name, std::string_view value) {
    if (Reading())
      Require(run_, ReadText(parent, name) == value);
    else {
      Prefix(name);
      Quoted(value);
    }
  }
  void NullValue(Node parent, std::string_view name) {
    if (Reading())
      Require(run_, Null(parent, name));
    else {
      Prefix(name);
      Append("null");
    }
  }
  void Boolean(Node parent, std::string_view name, bool value) {
    if (Reading()) {
      const auto node = Child(parent, name);
      Require(run_, snapshot_->Kind(node) == gltf_detail::JsonKind::kBool);
      Require(run_, snapshot_->Boolean(node) == value);
    } else {
      Prefix(name);
      Append(value ? "true" : "false");
    }
  }
  std::uint64_t ParseUnsigned(std::string_view token, std::uint64_t maximum) {
    run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
    Require(run_,
            !token.empty() && (token.size() == 1 || token.front() != '0'));
    std::uint64_t value = 0;
    for (char c : token) {
      run_.Check();
      Require(run_, c >= '0' && c <= '9');
      const auto digit = static_cast<unsigned>(c - '0');
      Require(run_, digit <= maximum && value <= (maximum - digit) / 10);
      value = value * 10 + digit;
    }
    return value;
  }
  std::uint64_t Read64(Node parent, std::string_view name) {
    return ParseUnsigned(ReadText(parent, name), UINT64_MAX);
  }
  std::uint32_t Read32(Node parent, std::string_view name) {
    const auto node = Child(parent, name);
    Require(run_, snapshot_->Kind(node) == gltf_detail::JsonKind::kNumber);
    return static_cast<std::uint32_t>(
        ParseUnsigned(snapshot_->Number(node), UINT32_MAX));
  }
  std::int64_t ReadSigned(Node parent, std::string_view name,
                          std::int64_t minimum, std::int64_t maximum) {
    const auto node = Child(parent, name);
    Require(run_, snapshot_->Kind(node) == gltf_detail::JsonKind::kNumber);
    auto token = snapshot_->Number(node);
    run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
    const bool negative = !token.empty() && token.front() == '-';
    if (negative) token.remove_prefix(1);
    const auto value = ParseUnsigned(
        token, static_cast<std::uint64_t>(negative ? -minimum : maximum));
    Require(run_, !negative || value != 0);
    return negative ? -static_cast<std::int64_t>(value)
                    : static_cast<std::int64_t>(value);
  }
  void Unsigned(Node parent, std::string_view name, std::uint64_t value,
                std::uint64_t cap = UINT64_MAX) {
    if (Reading()) {
      const auto actual = Read64(parent, name);
      run_.Bound(actual, cap);
      Require(run_, actual == value);
    } else {
      String token{gltf_detail::Allocator<char>(run_.allocation)};
      AppendUnsigned(token, value);
      run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
      Text(parent, name, token);
    }
  }
  void Small(Node parent, std::string_view name, std::uint32_t value) {
    if (Reading())
      Require(run_, Read32(parent, name) == value);
    else {
      Prefix(name);
      String token{gltf_detail::Allocator<char>(run_.allocation)};
      AppendUnsigned(token, value);
      run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
      Append(token);
    }
  }
  void Signed(Node parent, std::string_view name, std::int64_t value,
              std::int64_t minimum, std::int64_t maximum) {
    if (Reading())
      Require(run_, ReadSigned(parent, name, minimum, maximum) == value);
    else {
      Prefix(name);
      String token{gltf_detail::Allocator<char>(run_.allocation)};
      AppendSigned(token, value);
      run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
      Append(token);
    }
  }
  void Optional64(Node parent, std::string_view name,
                  std::optional<std::uint64_t> value) {
    if (value)
      Unsigned(parent, name, *value);
    else
      NullValue(parent, name);
  }
  void Optional32(Node parent, std::string_view name,
                  std::optional<std::uint32_t> value) {
    if (value)
      Small(parent, name, *value);
    else
      NullValue(parent, name);
  }
  void Hex(Node parent, std::string_view name, std::span<const std::byte> bytes,
           bool descriptor = false) {
    if (!Reading()) {
      Prefix(name);
      Append("\"");
      const auto first = text_.size();
      Grow(Times(run_, bytes.size(), kHexPerByte));
      struct State {
        std::span<const std::byte> bytes;
        char* target;
        Run* run;
      } state{bytes, text_.data() + first, &run_};
      Bulk(
          run_, bytes.size(), 1,
          [](void* opaque, std::uint64_t first, std::uint64_t last,
             JobObservation&) {
            auto& s = *static_cast<State*>(opaque);
            for (auto i = first; i < last; ++i) {
              if ((i - first) %
                      s.run->policy.surface.cancellation_block_scalars ==
                  0)
                s.run->Check(i);
              const auto byte = std::to_integer<unsigned>(s.bytes[Size(i)]);
              s.target[Size(i * 2)] = kHexDigits[byte >> 4];
              s.target[Size(i * 2 + 1)] = kHexDigits[byte & 15];
            }
          },
          &state);
      Append("\"");
      return;
    }
    const auto text = ReadText(parent, name);
    Require(run_, text.size() == Times(run_, bytes.size(), kHexPerByte));
    struct State {
      std::span<const std::byte> bytes;
      std::string_view text;
      Run* run;
      bool descriptor;
    } state{bytes, text, &run_, descriptor};
    Bulk(
        run_, bytes.size(), 1,
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation& job) {
          auto& s = *static_cast<State*>(opaque);
          for (auto i = first; i < last; ++i) {
            if ((i - first) %
                    s.run->policy.surface.cancellation_block_scalars ==
                0)
              s.run->Check(i);
            const auto actual = static_cast<std::byte>(
                (HexDigit(s.text[Size(i * 2)], *s.run) << 4) |
                HexDigit(s.text[Size(i * 2 + 1)], *s.run));
            const bool same = actual == s.bytes[Size(i)];
            if (s.descriptor) ++job.work.descriptor_byte_pairs;
            Require(*s.run, same, SurfaceCode::kWireValue);
          }
        },
        &state);
  }

 private:
  struct Frame {
    bool array;
    bool first = true;
  };
  void Append(std::string_view text) {
    run_.Bound(Sum(run_, text_.size(), text.size()), byte_limit_,
               SurfaceCode::kOutputLimit);
    text_.append(text);
  }
  void Grow(std::uint64_t count) {
    const auto end = Sum(run_, text_.size(), count);
    run_.Bound(end, byte_limit_, SurfaceCode::kOutputLimit);
    text_.resize(Size(end));
  }
  void Push(bool array) {
    run_.Bound(frames_.size() + 1, run_.limits.max_json_depth);
    frames_.push_back({array, true});
  }
  void Prefix(std::string_view name) {
    if (frames_.empty()) return;
    auto& frame = frames_.back();
    if (!frame.first) Append(",");
    frame.first = false;
    if (!frame.array) {
      Quoted(name);
      Append(":");
    }
  }
  void Quoted(std::string_view text) {
    Append("\"");
    for (char c : text) {
      run_.Check();
      const auto byte = static_cast<unsigned char>(c);
      if (c == '"')
        Append("\\\"");
      else if (c == '\\')
        Append("\\\\");
      else if (byte < 32) {
        const std::array<char, 6> escape{
            '\\', 'u', '0', '0', kHexDigits[byte >> 4], kHexDigits[byte & 15]};
        Append({escape.data(), escape.size()});
      } else
        Append({&c, 1});
    }
    Append("\"");
  }
  Run& run_;
  String text_;
  Vector<Frame> frames_;
  std::uint64_t byte_limit_;
  const gltf_detail::JsonSnapshot* snapshot_ = nullptr;
};

struct Slice {
  std::uint64_t offset = 0, count = 0;
  std::size_t width = 0;
};
std::uint64_t Load(std::span<const std::byte> bytes, std::uint64_t offset,
                   std::size_t width) noexcept {
  std::uint64_t word = 0;
  for (std::size_t i = 0; i < width; ++i)
    word |= std::to_integer<std::uint64_t>(
                bytes[static_cast<std::size_t>(offset) + i])
            << (i * kByteBits);
  return word;
}
class Binary {
 public:
  Binary(Run& run, Vector<std::byte>& bytes) : run_(run), output_(&bytes) {}
  Binary(Run& run, std::span<const std::byte> bytes)
      : run_(run), input_(bytes) {}
  template <class T>
  Slice Values(Json& json, Node parent, std::string_view name,
               std::span<const T> values, bool scalar_pairs = false,
               bool relationship = false) {
    const auto aligned =
        Sum(run_, cursor_, kRangeAlignment - 1) & ~(kRangeAlignment - 1);
    const auto length = Times(run_, values.size(), sizeof(T));
    const auto end = Sum(run_, aligned, length);
    run_.Bound(end, run_.limits.max_capsule_binary_bytes,
               SurfaceCode::kOutputLimit);
    const auto node = json.Object(parent, name,
                                  {capsule_key::kOffset, capsule_key::kCount,
                                   capsule_key::kBytes, capsule_key::kScalar});
    json.Unsigned(node, capsule_key::kOffset, aligned,
                  run_.limits.max_capsule_binary_bytes);
    json.Unsigned(node, capsule_key::kCount, values.size());
    json.Unsigned(node, capsule_key::kBytes, length,
                  run_.limits.max_capsule_binary_bytes);
    json.Text(node, capsule_key::kScalar, ScalarName<T>());
    json.EndObject();
    if (output_)
      output_->resize(Size(end), std::byte{});
    else {
      Require(run_, end <= input_.size(), SurfaceCode::kBinarySlice);
      for (auto i = cursor_; i < aligned; ++i) {
        run_.Check(i);
        Require(run_, input_[Size(i)] == std::byte{},
                SurfaceCode::kBinarySlice);
      }
    }
    const auto remaining =
        run_.limits.max_relationship_steps - run_.facts.work.relationship_steps;
    std::atomic<std::uint64_t> permits{0};
    struct State {
      Run* run;
      std::span<const T> values;
      std::byte* output;
      std::span<const std::byte> input;
      std::uint64_t offset;
      bool pairs, relations;
      std::atomic<std::uint64_t>* permits;
      std::uint64_t remaining;
    } state{&run_,        values,   output_ ? output_->data() : nullptr,
            input_,       aligned,  scalar_pairs,
            relationship, &permits, remaining};
    Bulk(
        run_, values.size(), sizeof(T),
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation& job) {
          auto& s = *static_cast<State*>(opaque);
          for (auto i = first; i < last; ++i) {
            if ((i - first) %
                    s.run->policy.surface.cancellation_block_scalars ==
                0)
              s.run->Check(i);
            if (s.relations) {
              auto used = s.permits->load(std::memory_order_relaxed);
              for (;;) {
                if (used >= s.remaining)
                  Fail(SurfaceStatus::kLimitExceeded, s.run->stage,
                       SurfaceCode::kCountLimit);
                if (s.permits->compare_exchange_weak(used, used + 1,
                                                     std::memory_order_relaxed))
                  break;
              }
            }
            const auto expected = ObjectWord(s.values[Size(i)]);
            const auto offset = s.offset + i * sizeof(T);
            if (s.output) {
              for (std::size_t byte = 0; byte < sizeof(T); ++byte)
                s.output[Size(offset) + byte] = static_cast<std::byte>(
                    (expected >> (byte * kByteBits)) & kByteMask);
              if (s.relations) ++job.work.relationship_steps;
            } else {
              const auto actual = Load(s.input, offset, sizeof(T));
              if (s.pairs) ++job.work.source_scalar_pairs;
              if (s.relations) ++job.work.relationship_steps;
              Require(*s.run, actual == expected, SurfaceCode::kBinarySlice);
            }
          }
        },
        &state);
    cursor_ = end;
    return {aligned, static_cast<std::uint64_t>(values.size()), sizeof(T)};
  }
  void Finish() {
    if (!output_)
      Require(run_, cursor_ == input_.size(), SurfaceCode::kBinarySlice);
  }
  std::uint64_t At(Slice slice, std::uint64_t index) const {
    Require(run_, index < slice.count, SurfaceCode::kWireMap);
    return Load(input_, slice.offset + index * slice.width, slice.width);
  }

 private:
  Run& run_;
  Vector<std::byte>* output_ = nullptr;
  std::span<const std::byte> input_;
  std::uint64_t cursor_ = 0;
};
}  // namespace

SurfaceDigest Digest(std::span<const std::byte> bytes, Run& run) {
  if (bytes.size() > UINT64_MAX / kByteBits)
    Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kSizeOverflow);
  auto hash = kShaInitial;
  std::size_t offset = 0;
  // SHA's compression chain is dependent; bounded serial blocks are genuine.
  while (bytes.size() - offset >= kShaBlockBytes) {
    run.Check(offset);
    Compress(hash, std::span<const std::byte, kShaBlockBytes>(
                       bytes.subspan(offset, kShaBlockBytes)));
    offset += kShaBlockBytes;
  }
  std::array<std::byte, kShaBlockBytes * 2> tail{};
  const auto rest = bytes.size() - offset;
  for (std::size_t i = 0; i < rest; ++i) tail[i] = bytes[offset + i];
  tail[rest] = std::byte{0x80};
  const auto length = rest + kShaLengthBytes + 1 > kShaBlockBytes
                          ? kShaBlockBytes * 2
                          : kShaBlockBytes;
  const auto bits = static_cast<std::uint64_t>(bytes.size()) * kByteBits;
  for (std::size_t i = 0; i < kShaLengthBytes; ++i)
    tail[length - 1 - i] =
        static_cast<std::byte>((bits >> (i * kByteBits)) & kByteMask);
  for (std::size_t i = 0; i < length; i += kShaBlockBytes) {
    run.Check(offset + i);
    Compress(hash, std::span<const std::byte, kShaBlockBytes>(tail.data() + i,
                                                              kShaBlockBytes));
  }
  SurfaceDigest result{};
  for (std::size_t i = 0; i < hash.size(); ++i)
    for (std::size_t j = 0; j < 4; ++j)
      result[i * 4 + j] = static_cast<std::byte>(
          (hash[i] >> ((3 - j) * kByteBits)) & kByteMask);
  return result;
}

namespace {

template <class Range>
auto Span(const Range& values) {
  return std::span<const typename Range::value_type>{values.data(),
                                                     values.size()};
}
std::span<const std::byte> Bytes(std::string_view value) {
  return std::as_bytes(std::span<const char>{value.data(), value.size()});
}
void Edge(Run& run) {
  const auto next = Sum(run, run.facts.work.relationship_steps, 1);
  run.Bound(next, run.limits.max_relationship_steps);
  run.facts.work.relationship_steps = next;
}
// Per-disjoint-range permits cap actual relationship predicates before entry.
// Job observations are reduced only by the existing Bulk join owner.
class Relations {
 public:
  explicit Relations(Run& run)
      : run_(run),
        remaining_(run.limits.max_relationship_steps -
                   run.facts.work.relationship_steps) {}
  template <class F>
  void Check(JobObservation& job, F predicate) {
    auto used = used_.load(std::memory_order_relaxed);
    for (;;) {
      if (used >= remaining_)
        Fail(SurfaceStatus::kLimitExceeded, run_.stage,
             SurfaceCode::kCountLimit);
      if (used_.compare_exchange_weak(used, used + 1,
                                      std::memory_order_relaxed))
        break;
    }
    const bool valid = predicate();
    ++job.work.relationship_steps;
    Require(run_, valid, SurfaceCode::kWireMap);
  }

 private:
  Run& run_;
  const std::uint64_t remaining_;
  std::atomic<std::uint64_t> used_{0};
};
constexpr std::array<std::string_view, 11> kMapNames{
    capsule_key::kTriangleSources,     capsule_key::kTriangleFaces,
    capsule_key::kCornerSources,       capsule_key::kCornerCorners,
    capsule_key::kVertexSources,       capsule_key::kSourceVertexOffsets,
    capsule_key::kSourceVertexMembers, capsule_key::kSourceFaceOffsets,
    capsule_key::kSourceFaceMembers,   capsule_key::kSourceCornerOffsets,
    capsule_key::kSourceCornerMembers};
constexpr std::array<std::string_view, 6> kDefinitionKinds{
    capsule_key::kMaterial, "texture", "image", "sampler",
    "buffer-view",          "buffer"};
constexpr Keys kRootKeys{
    capsule_key::kSchema,           capsule_key::kEncoding,
    capsule_key::kRepresentation,   capsule_key::kCoordinates,
    capsule_key::kAcknowledgments,  capsule_key::kMembers,
    capsule_key::kOriginal,         capsule_key::kTriangulated,
    capsule_key::kTriangulation,    capsule_key::kWire,
    capsule_key::kChannels,         capsule_key::kUvMapping,
    capsule_key::kInverseUvMapping, capsule_key::kCatalog,
    capsule_key::kNumeric};

Natural ErrorNatural(const SurfaceDyadicError& error, Run& run) {
  Require(run, error.used_words <= error.magnitude.size(),
          SurfaceCode::kWireValue);
  Natural value(run.allocation, run.limits.max_numeric_integer_bits);
  for (std::size_t i = error.used_words; i > 0; --i) {
    run.Check(i);
    value.ShiftLeft(32);
    value.AddSmall(static_cast<std::uint32_t>(error.magnitude[i - 1] >> 32));
    value.ShiftLeft(32);
    value.AddSmall(static_cast<std::uint32_t>(error.magnitude[i - 1]));
  }
  return value;
}
String Decimal(Natural value, Run& run) {
  String text{gltf_detail::Allocator<char>(run.allocation)};
  if (value.Zero()) text.push_back('0');
  while (!value.Zero()) {
    run.Check();
    run.Bound(text.size() + 1,
              std::min<std::uint64_t>(run.limits.max_numeric_token_bytes,
                                      kDyadicDigits));
    text.push_back(static_cast<char>('0' + value.DivideSmall(10)));
  }
  std::reverse(text.begin(), text.end());
  return text;
}
SurfaceDyadicError ParseDyadic(Json& json, Node parent, std::string_view name,
                               Run& run) {
  const auto node = json.Object(
      parent, name, {capsule_key::kNumerator, capsule_key::kExponent2});
  const auto token = json.ReadText(node, capsule_key::kNumerator);
  run.Bound(token.size(), run.limits.max_numeric_token_bytes);
  Require(run, !token.empty() && token.size() <= kDyadicDigits &&
                   (token.size() == 1 || token.front() != '0'));
  Natural numerator(run.allocation, run.limits.max_numeric_integer_bits);
  for (char c : token) {
    run.Check();
    Require(run, c >= '0' && c <= '9');
    numerator.MultiplySmall(10);
    numerator.AddSmall(static_cast<std::uint32_t>(c - '0'));
  }
  const auto exponent =
      json.ReadSigned(node, capsule_key::kExponent2, kDyadicExponentMinimum,
                      kDyadicExponentMaximum);
  Require(run, numerator.Zero() ? exponent == 0 : numerator.Odd());
  Require(run, numerator.Bits() <= kSurfaceErrorMagnitudeWords * kWord64Bits);
  SurfaceDyadicError error;
  error.exponent2 = static_cast<std::int32_t>(exponent);
  const auto words = numerator.Words();
  for (std::size_t i = 0; i < words.size(); ++i)
    error.magnitude[i / 2] |= static_cast<std::uint64_t>(words[i])
                              << ((i % 2) * 32);
  error.used_words = static_cast<std::uint32_t>((words.size() + 1) / 2);
  json.EndObject();
  return error;
}
void DyadicValue(Json& json, Node parent, std::string_view name,
                 const SurfaceDyadicError& value, Run& run) {
  const auto node = json.Object(
      parent, name, {capsule_key::kNumerator, capsule_key::kExponent2});
  const auto numerator = Decimal(ErrorNatural(value, run), run);
  json.Text(node, capsule_key::kNumerator, numerator);
  json.Signed(node, capsule_key::kExponent2, value.exponent2,
              kDyadicExponentMinimum, kDyadicExponentMaximum);
  json.EndObject();
}
SurfaceDyadicError BoundValue(std::uint64_t bits, Run& run) {
  auto value =
      Decode64(bits, run.allocation, run.limits.max_numeric_integer_bits);
  Require(run, !value.negative, SurfaceCode::kInvalidBound);
  SurfaceDyadicError result;
  const auto words = value.magnitude.Words();
  Require(run, words.size() <= result.magnitude.size() * 2);
  for (std::size_t i = 0; i < words.size(); ++i)
    result.magnitude[i / 2] |= static_cast<std::uint64_t>(words[i])
                               << ((i % 2) * 32);
  result.used_words = static_cast<std::uint32_t>((words.size() + 1) / 2);
  result.exponent2 = value.exponent2;
  return result;
}
void HexWord(Json& json, Node parent, std::string_view name,
             std::uint32_t word) {
  std::array<char, kWord32Hex> text{};
  for (std::size_t i = 0; i < text.size(); ++i)
    text[i] = kHexDigits[(word >> ((text.size() - i - 1) * 4)) & 15];
  json.Text(parent, name, {text.data(), text.size()});
}
SurfaceNumericSummary Summary(Json& json, Node parent,
                              const SurfaceNumericSummary* written,
                              std::optional<std::uint64_t> attribute,
                              std::uint64_t rows, std::uint32_t components,
                              Run& run) {
  const auto node = json.Object(
      parent, capsule_key::kObserved,
      {capsule_key::kKind, capsule_key::kComplete, capsule_key::kExaminedValues,
       capsule_key::kChangedValues, capsule_key::kMaxAbsoluteError,
       capsule_key::kSourceAttribute, capsule_key::kSourceRow,
       capsule_key::kComponent});
  json.Text(node, capsule_key::kKind, kDecoded);
  json.Boolean(node, capsule_key::kComplete, true);
  SurfaceNumericSummary result;
  if (json.Reading()) {
    result.kind = SurfaceNumericSummary::Kind::kDecodedWire;
    result.complete = true;
    result.examined_values = json.Read64(node, capsule_key::kExaminedValues);
    result.changed_values = json.Read64(node, capsule_key::kChangedValues);
    Require(run, result.changed_values <= result.examined_values);
    Require(run, result.examined_values <= Times(run, rows, components));
    result.max_absolute_error =
        ParseDyadic(json, node, capsule_key::kMaxAbsoluteError, run);
    if (!json.Null(node, capsule_key::kSourceAttribute))
      result.source_attribute =
          json.Read64(node, capsule_key::kSourceAttribute);
    if (!json.Null(node, capsule_key::kSourceRow))
      result.source_row = json.Read64(node, capsule_key::kSourceRow);
    if (!json.Null(node, capsule_key::kComponent))
      result.component = json.Read32(node, capsule_key::kComponent);
    if (result.max_absolute_error.used_words == 0) {
      Require(run, result.changed_values == 0 && !result.source_attribute &&
                       !result.source_row && !result.component);
    } else {
      Require(run, result.changed_values != 0 &&
                       result.source_attribute == attribute &&
                       result.source_row && result.component);
      Require(run, *result.source_row < rows && *result.component < components);
    }
  } else {
    Require(run,
            written && written->complete &&
                written->kind == SurfaceNumericSummary::Kind::kDecodedWire,
            SurfaceCode::kWireValue);
    result = *written;
    json.Unsigned(node, capsule_key::kExaminedValues, result.examined_values);
    json.Unsigned(node, capsule_key::kChangedValues, result.changed_values);
    DyadicValue(json, node, capsule_key::kMaxAbsoluteError,
                result.max_absolute_error, run);
    // Public UV source_attribute identifies its field even for zero error.
    // Only the wire's nested maximum witness is null in that case; preserve
    // the actual E summary unchanged.
    const bool witness = result.max_absolute_error.used_words != 0;
    json.Optional64(node, capsule_key::kSourceAttribute,
                    witness ? result.source_attribute : std::nullopt);
    json.Optional64(node, capsule_key::kSourceRow,
                    witness ? result.source_row : std::nullopt);
    json.Optional32(node, capsule_key::kComponent,
                    witness ? result.component : std::nullopt);
  }
  json.EndObject();
  return result;
}
std::uint64_t DomainRows(geometry::MeshReadView mesh,
                         geometry::AttributeDomain domain) {
  if (domain == geometry::AttributeDomain::vertex)
    return mesh.positions.size() / kPositionComponents;
  if (domain == geometry::AttributeDomain::face)
    return mesh.face_offsets.size() - 1;
  return mesh.corner_vertices.size();
}
void Mesh(Json& json, Node parent, std::string_view name,
          geometry::MeshReadView mesh, Binary& binary, Run& run,
          MeshRole role = MeshRole::kOriginal) {
  const auto node = json.Object(
      parent, name,
      {capsule_key::kCounts, capsule_key::kPositions, capsule_key::kFaceOffsets,
       capsule_key::kCornerVertices, capsule_key::kAttributes});
  const auto counts = MeshCounts(mesh, run, role);
  const auto count = json.Object(
      node, capsule_key::kCounts,
      {capsule_key::kVertices, capsule_key::kFaces, capsule_key::kCorners,
       capsule_key::kFaceOffsets, capsule_key::kAttributes,
       capsule_key::kAttributeScalars, capsule_key::kAttributeBytes,
       capsule_key::kAttributeOffsets, capsule_key::kPresenceBytes,
       capsule_key::kMetadataEntries, capsule_key::kDescriptorBytes});
  json.Unsigned(count, capsule_key::kVertices, counts.vertices,
                run.limits.max_vertices);
  json.Unsigned(count, capsule_key::kFaces, counts.faces, run.limits.max_faces);
  json.Unsigned(count, capsule_key::kCorners, counts.corners,
                run.limits.max_corners);
  json.Unsigned(count, capsule_key::kFaceOffsets, counts.face_offsets,
                run.limits.max_face_offsets);
  json.Unsigned(count, capsule_key::kAttributes, counts.attributes,
                run.limits.max_attributes);
  json.Unsigned(count, capsule_key::kAttributeScalars, counts.attribute_scalars,
                run.limits.max_attribute_scalars);
  json.Unsigned(count, capsule_key::kAttributeBytes, counts.attribute_bytes,
                run.limits.max_attribute_bytes);
  json.Unsigned(count, capsule_key::kAttributeOffsets, counts.attribute_offsets,
                run.limits.max_attribute_offsets);
  json.Unsigned(count, capsule_key::kPresenceBytes, counts.presence_bytes,
                run.limits.max_presence_bytes);
  json.Unsigned(count, capsule_key::kMetadataEntries, counts.metadata_entries,
                run.limits.max_metadata_entries);
  json.Unsigned(count, capsule_key::kDescriptorBytes, counts.descriptor_bytes,
                run.limits.max_descriptor_bytes);
  json.EndObject();
  binary.Values(json, node, capsule_key::kPositions, mesh.positions, true);
  binary.Values(json, node, capsule_key::kFaceOffsets, mesh.face_offsets, false,
                true);
  binary.Values(json, node, capsule_key::kCornerVertices, mesh.corner_vertices,
                false, true);
  json.Array(
      node, capsule_key::kAttributes, mesh.attributes.size(),
      [&](Node element, std::size_t ordinal) {
        const auto& a = mesh.attributes[ordinal];
        const auto object =
            json.Object(element, {},
                        {capsule_key::kOrdinal, capsule_key::kNameHex,
                         capsule_key::kSemanticHex, capsule_key::kDomain,
                         capsule_key::kScalar, capsule_key::kComponents,
                         capsule_key::kSetIndex, capsule_key::kValues,
                         capsule_key::kOffsets, capsule_key::kPresent,
                         capsule_key::kMetadata});
        json.Unsigned(object, capsule_key::kOrdinal, ordinal);
        json.Hex(object, capsule_key::kNameHex, Bytes(a.name), true);
        json.Hex(object, capsule_key::kSemanticHex, Bytes(a.semantic), true);
        json.Text(object, capsule_key::kDomain, DomainName(a.domain));
        std::visit(
            [&](auto values) {
              using T = typename decltype(values)::value_type;
              json.Text(object, capsule_key::kScalar, ScalarName<T>());
            },
            a.values);
        json.Small(object, capsule_key::kComponents, a.components);
        json.Optional32(object, capsule_key::kSetIndex, a.set_index);
        std::visit(
            [&](auto values) {
              binary.Values(json, object, capsule_key::kValues, values, true);
            },
            a.values);
        if (a.offsets)
          binary.Values(json, object, capsule_key::kOffsets, *a.offsets, false,
                        true);
        else
          json.NullValue(object, capsule_key::kOffsets);
        if (a.present)
          binary.Values(json, object, capsule_key::kPresent, *a.present, true);
        else
          json.NullValue(object, capsule_key::kPresent);
        json.Array(object, capsule_key::kMetadata, a.metadata.size(),
                   [&](Node item, std::size_t index) {
                     const auto entry = json.Object(
                         item, {},
                         {capsule_key::kKeyHex, capsule_key::kValueHex});
                     json.Hex(entry, capsule_key::kKeyHex,
                              Bytes(a.metadata[index].key), true);
                     json.Hex(entry, capsule_key::kValueHex,
                              Bytes(a.metadata[index].value), true);
                     json.EndObject();
                   });
        json.EndObject();
      });
  json.EndObject();
}
const SurfaceMemberView& Find(std::span<const SurfaceMemberView> members,
                              std::string_view path, Run& run) {
  const SurfaceMemberView* found = nullptr;
  for (const auto& member : members) {
    run.Check();
    Edge(run);
    if (member.path == path) {
      Require(run, !found, SurfaceCode::kMemberInventory);
      found = &member;
    }
  }
  Require(run, found != nullptr, SurfaceCode::kMemberInventory);
  return *found;
}
void MapsAndRelations(Json& json, Node parent, const OutputPayload& o,
                      Binary& binary, std::uint64_t binary_bytes,
                      std::span<const SurfaceMemberView> captured, Run& run) {
  // Fresh V owns its map admission. Only retained span extents are inspected
  // here; no Geometry-map scalar is traversed before the aggregate ceiling.
  const std::array<std::span<const std::uint64_t>, 11> maps{
      Span(o.triangle_sources), Span(o.triangle_faces), Span(o.corner_sources),
      Span(o.corner_corners),   Span(o.vertex_sources), Span(o.vertex_offsets),
      Span(o.vertex_members),   Span(o.face_offsets),   Span(o.face_members),
      Span(o.corner_offsets),   Span(o.corner_members)};
  std::uint64_t entries = Sum(run, o.triangles.FaceSources().size(),
                              o.triangles.CornerSources().size());
  entries = Sum(run, entries, o.triangles.FaceOutputOffsets().size());
  for (auto map : maps) entries = Sum(run, entries, map.size());
  run.Bound(entries, run.limits.max_map_entries, SurfaceCode::kMapLimit);
  const auto triangulation =
      json.Object(parent, capsule_key::kTriangulation,
                  {capsule_key::kFaceSources, capsule_key::kCornerSources,
                   capsule_key::kFaceOutputOffsets});
  const auto face_sources =
      binary.Values(json, triangulation, capsule_key::kFaceSources,
                    o.triangles.FaceSources(), false, true);
  const auto corner_sources =
      binary.Values(json, triangulation, capsule_key::kCornerSources,
                    o.triangles.CornerSources(), false, true);
  binary.Values(json, triangulation, capsule_key::kFaceOutputOffsets,
                o.triangles.FaceOutputOffsets(), false, true);
  json.EndObject();
  const auto wire = json.Object(
      parent, capsule_key::kWire,
      {capsule_key::kCounts, capsule_key::kPrimitives, capsule_key::kMaps});
  std::uint64_t listed = 0, listed_bytes = 0, textures = 0, samplers = 0;
  for (const auto& member : o.storage) {
    run.Check();
    if (member.kind != SurfaceMemberKind::kCapsuleJson) {
      listed = Sum(run, listed, 1);
      listed_bytes = Sum(run, listed_bytes,
                         member.kind == SurfaceMemberKind::kCapsuleBinary
                             ? binary_bytes
                             : member.bytes.size());
    }
  }
  if (json.Reading()) {
    listed = 0;
    listed_bytes = 0;
    for (const auto& member : captured) {
      run.Check();
      if (member.kind == SurfaceMemberKind::kCapsuleJson) continue;
      listed = Sum(run, listed, 1);
      listed_bytes = Sum(run, listed_bytes, member.bytes.size());
    }
  }
  for (const auto& definition : o.definitions) {
    run.Check();
    if (definition.wire && definition.kind == "texture") ++textures;
    if (definition.wire && definition.kind == "sampler") ++samplers;
  }
  const auto counts = json.Object(
      wire, capsule_key::kCounts,
      {capsule_key::kPrimitives, capsule_key::kVertices,
       capsule_key::kTriangles, capsule_key::kCorners, capsule_key::kMapEntries,
       capsule_key::kMaterials, capsule_key::kTextures, capsule_key::kImages,
       capsule_key::kSamplers, capsule_key::kListedMembers,
       capsule_key::kListedMemberBytes});
  json.Unsigned(counts, capsule_key::kPrimitives, o.primitives.size(),
                run.limits.max_wire_primitives);
  json.Unsigned(counts, capsule_key::kVertices, o.vertex_sources.size(),
                run.limits.max_wire_vertices);
  json.Unsigned(counts, capsule_key::kTriangles, o.triangle_sources.size(),
                run.limits.max_wire_corners / kTriangleCorners);
  json.Unsigned(counts, capsule_key::kCorners, o.corner_sources.size(),
                run.limits.max_wire_corners);
  json.Unsigned(counts, capsule_key::kMapEntries, entries,
                run.limits.max_map_entries);
  json.Unsigned(counts, capsule_key::kMaterials, o.material_bindings.size(),
                run.limits.max_material_bindings);
  json.Unsigned(counts, capsule_key::kTextures, textures,
                run.limits.max_material_bindings);
  json.Unsigned(counts, capsule_key::kImages, o.images.size(),
                run.limits.max_resources);
  json.Unsigned(counts, capsule_key::kSamplers, samplers,
                run.limits.max_material_bindings);
  json.Unsigned(counts, capsule_key::kListedMembers, listed,
                run.limits.max_members);
  json.Unsigned(counts, capsule_key::kListedMemberBytes, listed_bytes,
                run.limits.max_total_member_bytes);
  json.EndObject();
  std::uint64_t vertex_end = 0, triangle_end = 0;
  json.Array(
      wire, capsule_key::kPrimitives, o.primitives.size(),
      [&](Node item, std::size_t index) {
        const auto& p = o.primitives[index];
        const auto& v = p.view;
        const auto primitive = json.Object(
            item, {},
            {capsule_key::kOrdinal, capsule_key::kFirstWireVertex,
             capsule_key::kWireVertices, capsule_key::kFirstWireTriangle,
             capsule_key::kWireTriangles, capsule_key::kMaterial,
             capsule_key::kPositionAccessor, capsule_key::kIndexAccessor,
             capsule_key::kUvAccessors});
        json.Unsigned(primitive, capsule_key::kOrdinal, v.ordinal);
        json.Unsigned(primitive, capsule_key::kFirstWireVertex,
                      v.first_wire_vertex);
        json.Unsigned(primitive, capsule_key::kWireVertices, v.wire_vertices,
                      run.limits.max_wire_vertices);
        json.Unsigned(primitive, capsule_key::kFirstWireTriangle,
                      v.first_wire_triangle);
        json.Unsigned(primitive, capsule_key::kWireTriangles, v.wire_triangles);
        json.Optional64(primitive, capsule_key::kMaterial, v.material);
        json.Optional64(primitive, capsule_key::kPositionAccessor,
                        p.position_accessor);
        json.Optional64(primitive, capsule_key::kIndexAccessor,
                        p.index_accessor);
        json.Array(
            primitive, capsule_key::kUvAccessors, p.uv_accessors.size(),
            [&](Node entry, std::size_t uv) {
              Require(run, uv < o.uv_bindings.size(), SurfaceCode::kUvMap);
              const auto binding = json.Object(
                  entry, {}, {capsule_key::kWireSet, capsule_key::kAccessor});
              json.Small(binding, capsule_key::kWireSet,
                         o.uv_bindings[uv].wire_set);
              json.Unsigned(binding, capsule_key::kAccessor,
                            p.uv_accessors[uv]);
              json.EndObject();
            });
        json.EndObject();
        Edge(run);
        Require(run,
                v.ordinal == index && v.first_wire_vertex == vertex_end &&
                    v.first_wire_triangle == triangle_end,
                SurfaceCode::kWireMap);
        Require(
            run,
            v.wire_vertices == Times(run, v.wire_triangles, kTriangleCorners),
            SurfaceCode::kWireMap);
        vertex_end = Sum(run, vertex_end, v.wire_vertices);
        triangle_end = Sum(run, triangle_end, v.wire_triangles);
      });
  Require(run,
          vertex_end == o.vertex_sources.size() &&
              triangle_end == o.triangle_sources.size(),
          SurfaceCode::kWireMap);
  const auto map_node = json.Object(
      wire, capsule_key::kMaps,
      {capsule_key::kTriangleSources, capsule_key::kTriangleFaces,
       capsule_key::kCornerSources, capsule_key::kCornerCorners,
       capsule_key::kVertexSources, capsule_key::kSourceVertexOffsets,
       capsule_key::kSourceVertexMembers, capsule_key::kSourceFaceOffsets,
       capsule_key::kSourceFaceMembers, capsule_key::kSourceCornerOffsets,
       capsule_key::kSourceCornerMembers});
  std::array<Slice, 11> slices{};
  for (std::size_t i = 0; i < maps.size(); ++i)
    slices[i] =
        binary.Values(json, map_node, kMapNames[i], maps[i], false, true);
  json.EndObject();
  json.EndObject();
  if (!json.Reading()) return;
  const auto original = o.original.Source().View();
  const auto tri = o.triangles.Mesh().View();
  Require(run,
          slices[2].count == Times(run, slices[0].count, kTriangleCorners) &&
              slices[4].count == slices[2].count,
          SurfaceCode::kWireMap);
  // Actual parsed ranges are reread for composed map checks, not reconstructed
  // from positional equality or an expected digest.
  Relations composition(run);
  struct Composition {
    Run* run;
    Binary* binary;
    std::span<const Slice> slices;
    Slice faces, corners;
    std::span<const geometry::index_t> vertices;
    Relations* permits;
  } composed{&run,         &binary,        slices,
             face_sources, corner_sources, tri.corner_vertices,
             &composition};
  Bulk(
      run, slices[0].count, sizeof(std::uint64_t),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation& job) {
        auto& s = *static_cast<Composition*>(opaque);
        for (auto t = first; t < last; ++t) {
          if ((t - first) % s.run->policy.surface.cancellation_block_scalars ==
              0)
            s.run->Check(t);
          const auto source = s.binary->At(s.slices[0], t);
          s.permits->Check(job, [&] {
            return s.binary->At(s.slices[1], t) ==
                   s.binary->At(s.faces, source);
          });
          for (std::uint64_t c = 0; c < kTriangleCorners; ++c) {
            const auto wire_corner = Times(*s.run, t, kTriangleCorners) + c;
            const auto source_corner =
                Times(*s.run, source, kTriangleCorners) + c;
            s.permits->Check(job, [&] {
              return s.binary->At(s.slices[2], wire_corner) == source_corner;
            });
            s.permits->Check(job, [&] {
              return s.binary->At(s.slices[3], wire_corner) ==
                     s.binary->At(s.corners, source_corner);
            });
            Require(*s.run, source_corner < s.vertices.size(),
                    SurfaceCode::kWireMap);
            s.permits->Check(job, [&] {
              return s.binary->At(s.slices[4], wire_corner) ==
                     s.vertices[Size(source_corner)];
            });
          }
        }
      },
      &composed);
  const std::array<std::uint64_t, 3> domains{
      original.positions.size() / kPositionComponents,
      original.face_offsets.size() - 1, original.corner_vertices.size()};
  const std::array<std::size_t, 3> forward{4, 1, 3};
  for (std::size_t domain = 0; domain < domains.size(); ++domain) {
    const auto offsets = slices[5 + domain * 2],
               members = slices[6 + domain * 2],
               mapping = slices[forward[domain]];
    Require(run,
            offsets.count == Sum(run, domains[domain], 1) &&
                members.count == mapping.count,
            SurfaceCode::kWireMap);
    Require(run,
            binary.At(offsets, 0) == 0 &&
                binary.At(offsets, domains[domain]) == members.count,
            SurfaceCode::kWireMap);
    struct Csr {
      Run* run;
      Binary* binary;
      Slice offsets, members, mapping;
      Relations* permits;
    };
    Relations row_permits(run);
    Csr row_state{&run, &binary, offsets, members, mapping, &row_permits};
    Bulk(
        run, domains[domain], sizeof(std::uint64_t),
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation& job) {
          auto& s = *static_cast<Csr*>(opaque);
          for (auto row = first; row < last; ++row) {
            if ((row - first) %
                    s.run->policy.surface.cancellation_block_scalars ==
                0)
              s.run->Check(row);
            s.permits->Check(job, [&] {
              const auto a = s.binary->At(s.offsets, row),
                         b = s.binary->At(s.offsets, row + 1);
              return a <= b && b <= s.members.count;
            });
          }
        },
        &row_state);
    Relations member_permits(run);
    Csr member_state{&run, &binary, offsets, members, mapping, &member_permits};
    Bulk(
        run, members.count, sizeof(std::uint64_t),
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation& job) {
          auto& s = *static_cast<Csr*>(opaque);
          for (auto index = first; index < last; ++index) {
            if ((index - first) %
                    s.run->policy.surface.cancellation_block_scalars ==
                0)
              s.run->Check(index);
            const auto member = s.binary->At(s.members, index);
            s.permits->Check(job, [&] { return member < s.mapping.count; });
            const auto row = s.binary->At(s.mapping, member);
            s.permits->Check(job, [&] { return row < s.offsets.count - 1; });
            const auto a = s.binary->At(s.offsets, row),
                       b = s.binary->At(s.offsets, row + 1);
            s.permits->Check(job, [&] { return a <= index && index < b; });
            if (index > a)
              s.permits->Check(job, [&] {
                return s.binary->At(s.members, index - 1) < member;
              });
          }
        },
        &member_state);
  }
}

std::uint64_t BinaryLength(const OutputPayload& o, Run& run) {
  std::uint64_t cursor = 0;
  const auto range = [&](std::uint64_t count, std::uint64_t width) {
    run.Check();
    cursor = Sum(run, cursor, kRangeAlignment - 1) & ~(kRangeAlignment - 1);
    cursor = Sum(run, cursor, Times(run, count, width));
    run.Bound(cursor, run.limits.max_capsule_binary_bytes,
              SurfaceCode::kOutputLimit);
  };
  for (const auto mesh :
       {o.original.Source().View(), o.triangles.Mesh().View()}) {
    range(mesh.positions.size(), sizeof(double));
    range(mesh.face_offsets.size(), sizeof(std::uint64_t));
    range(mesh.corner_vertices.size(), sizeof(std::uint64_t));
    for (const auto& a : mesh.attributes) {
      std::visit(
          [&](auto values) {
            range(values.size(), sizeof(typename decltype(values)::value_type));
          },
          a.values);
      if (a.offsets) range(a.offsets->size(), sizeof(std::uint64_t));
      if (a.present) range(a.present->size(), 1);
    }
  }
  range(o.triangles.FaceSources().size(), sizeof(std::uint64_t));
  range(o.triangles.CornerSources().size(), sizeof(std::uint64_t));
  range(o.triangles.FaceOutputOffsets().size(), sizeof(std::uint64_t));
  for (const auto values :
       {Span(o.triangle_sources), Span(o.triangle_faces),
        Span(o.corner_sources), Span(o.corner_corners), Span(o.vertex_sources),
        Span(o.vertex_offsets), Span(o.vertex_members), Span(o.face_offsets),
        Span(o.face_members), Span(o.corner_offsets), Span(o.corner_members)})
    range(values.size(), sizeof(std::uint64_t));
  for (const auto& definition : o.definitions)
    range(definition.raw_json.size(), 1);
  for (const auto& image : o.images) range(image.bytes.size(), 1);
  return cursor;
}
void Catalog(Json& json, Node parent, const OutputPayload& o, Binary& binary,
             Run& run) {
  const auto catalog = json.Object(
      parent, capsule_key::kCatalog,
      {capsule_key::kSourceJsonSha256, capsule_key::kSourceMaterialAttribute,
       capsule_key::kBindings, capsule_key::kUv, capsule_key::kDefinitions,
       capsule_key::kImages});
  if (o.catalog_digest)
    json.Hex(catalog, capsule_key::kSourceJsonSha256, *o.catalog_digest);
  else
    json.NullValue(catalog, capsule_key::kSourceJsonSha256);
  json.Optional64(catalog, capsule_key::kSourceMaterialAttribute,
                  o.material_attribute);
  json.Array(
      catalog, capsule_key::kBindings, o.material_bindings.size(),
      [&](Node element, std::size_t index) {
        Edge(run);
        const auto object =
            json.Object(element, {},
                        {capsule_key::kSourceId, capsule_key::kSourceMaterial,
                         capsule_key::kWireMaterial});
        json.Signed(object, capsule_key::kSourceId,
                    o.material_bindings[index].source_id, INT32_MIN, INT32_MAX);
        json.Unsigned(object, capsule_key::kSourceMaterial,
                      o.material_bindings[index].catalog_material);
        json.Unsigned(object, capsule_key::kWireMaterial, index);
        json.EndObject();
      });
  json.Array(catalog, capsule_key::kUv, o.catalog_uv.size(),
             [&](Node element, std::size_t index) {
               Edge(run);
               const auto object = json.Object(
                   element, {},
                   {capsule_key::kCatalogSet, capsule_key::kSourceSet});
               json.Small(object, capsule_key::kCatalogSet,
                          o.catalog_uv[index].catalog_set);
               json.Small(object, capsule_key::kSourceSet,
                          o.catalog_uv[index].source_set);
               json.EndObject();
             });
  std::array<std::uint64_t, kDefinitionKinds.size()> tables{};
  json.Array(
      catalog, capsule_key::kDefinitions, o.definitions.size(),
      [&](Node element, std::size_t index) {
        const auto& d = o.definitions[index];
        const auto kind =
            std::find(kDefinitionKinds.begin(), kDefinitionKinds.end(),
                      std::string_view(d.kind));
        Require(run, kind != kDefinitionKinds.end());
        const auto table =
            static_cast<std::size_t>(kind - kDefinitionKinds.begin());
        tables[table] = Sum(run, tables[table], 1);
        run.Bound(tables[table], run.limits.max_material_bindings);
        Edge(run);
        const auto object = json.Object(
            element, {},
            {capsule_key::kKind, capsule_key::kSourceOrdinal,
             capsule_key::kWireOrdinal, capsule_key::kRawJson,
             capsule_key::kSourceCoreF32, capsule_key::kWireCoreF32});
        json.Text(object, capsule_key::kKind, d.kind);
        json.Unsigned(object, capsule_key::kSourceOrdinal, d.source);
        json.Optional64(object, capsule_key::kWireOrdinal, d.wire);
        binary.Values(json, object, capsule_key::kRawJson, Bytes(d.raw_json));
        const auto factors = [&](std::string_view name, const auto& value) {
          if (value)
            json.Array(object, name, value->size(),
                       [&](Node item, std::size_t component) {
                         HexWord(json, item, {}, (*value)[component]);
                       });
          else
            json.NullValue(object, name);
        };
        Require(run,
                (d.kind == capsule_key::kMaterial) ==
                        d.source_factors.has_value() &&
                    d.source_factors.has_value() == d.wire_factors.has_value());
        factors(capsule_key::kSourceCoreF32, d.source_factors);
        factors(capsule_key::kWireCoreF32, d.wire_factors);
        json.EndObject();
      });
  std::uint64_t resource_bytes = 0;
  json.Array(
      catalog, capsule_key::kImages, o.images.size(),
      [&](Node element, std::size_t index) {
        const auto& image = o.images[index];
        resource_bytes = Sum(run, resource_bytes, image.bytes.size());
        run.Bound(index + 1, run.limits.max_resources);
        run.Bound(resource_bytes, run.limits.max_resource_bytes);
        const auto object = json.Object(
            element, {},
            {capsule_key::kSourceImage, capsule_key::kSourceResource,
             capsule_key::kResourceKind, capsule_key::kResourceTableIndex,
             capsule_key::kOrigin, capsule_key::kSourceBuffer,
             capsule_key::kSourceBufferView, capsule_key::kWireImage,
             capsule_key::kMime, capsule_key::kBytes,
             capsule_key::kOrdinaryMember, capsule_key::kBinOffset,
             capsule_key::kBinBytes});
        Edge(run);
        json.Unsigned(object, capsule_key::kSourceImage, image.source);
        json.Unsigned(object, capsule_key::kSourceResource, image.resource);
        Require(run, image.kind == GltfResourceKind::kBuffer ||
                         image.kind == GltfResourceKind::kImage);
        json.Text(object, capsule_key::kResourceKind,
                  image.kind == GltfResourceKind::kBuffer ? "buffer" : "image");
        json.Unsigned(object, capsule_key::kResourceTableIndex, image.table);
        std::string_view origin;
        switch (image.origin) {
          case GltfResourceOrigin::kCallerBinding:
            origin = "caller-binding";
            break;
          case GltfResourceOrigin::kDecodedDataUri:
            origin = "decoded-data-uri";
            break;
          case GltfResourceOrigin::kBufferView:
            origin = "buffer-view";
            break;
        }
        Require(run, !origin.empty());
        json.Text(object, capsule_key::kOrigin, origin);
        json.Optional64(object, capsule_key::kSourceBuffer, image.buffer);
        json.Optional64(object, capsule_key::kSourceBufferView,
                        image.buffer_view);
        json.Unsigned(object, capsule_key::kWireImage, image.wire);
        Require(run, image.mime == "image/png" || image.mime == "image/jpeg");
        json.Text(object, capsule_key::kMime, image.mime);
        binary.Values(json, object, capsule_key::kBytes, Span(image.bytes));
        if (image.ordinary_path.empty())
          json.NullValue(object, capsule_key::kOrdinaryMember);
        else
          json.Text(object, capsule_key::kOrdinaryMember, image.ordinary_path);
        json.Optional64(object, capsule_key::kBinOffset, image.bin_offset);
        if (image.bin_offset)
          json.Unsigned(object, capsule_key::kBinBytes, image.bytes.size());
        else
          json.NullValue(object, capsule_key::kBinBytes);
        json.EndObject();
      });
  json.EndObject();
}
void Numeric(Json& json, Node parent, const OutputPayload& o,
             CapsuleNumericClaims& claims, Run& run) {
  const auto numeric = json.Object(parent, capsule_key::kNumeric,
                                   {capsule_key::kPositions, capsule_key::kUv});
  const auto mesh = o.original.Source().View();
  const auto field = [&](Node owner, std::string_view name,
                         SurfaceNarrowing mode, std::uint64_t bound,
                         const SurfaceNumericSummary* written,
                         std::optional<std::uint64_t> attribute,
                         std::uint64_t rows, std::uint32_t components) {
    const auto node = json.Object(
        owner, name,
        {capsule_key::kMode, capsule_key::kBound, capsule_key::kObserved});
    json.Text(node, capsule_key::kMode, ModeName(mode));
    const auto expected_bound = BoundValue(bound, run);
    if (json.Reading()) {
      const auto parsed = ParseDyadic(json, node, capsule_key::kBound, run);
      Require(run, CompareError(parsed, expected_bound) == 0,
              SurfaceCode::kErrorBound);
    } else
      DyadicValue(json, node, capsule_key::kBound, expected_bound, run);
    const auto summary =
        Summary(json, node, written, attribute, rows, components, run);
    if (json.Reading())
      Require(run,
              WithinBound(summary.max_absolute_error, bound, run.allocation,
                          run.limits.max_numeric_integer_bits),
              SurfaceCode::kErrorBound);
    json.EndObject();
    return summary;
  };
  // Only E is allowed to use E facts here. V parses an entirely separate claim.
  const auto* positions =
      json.Reading() ? nullptr
                     : (run.facts.positions ? &*run.facts.positions : nullptr);
  claims.positions =
      field(numeric, capsule_key::kPositions, o.narrowing,
            o.position_bound_bits, positions, {},
            mesh.positions.size() / kPositionComponents, kPositionComponents);
  claims.uv.reserve(o.uv_bindings.size());
  Require(run, o.uv_bound_bits.size() == o.uv_bindings.size());
  if (!json.Reading())
    Require(run, run.facts.uv.size() == o.uv_bindings.size(),
            SurfaceCode::kWireValue);
  json.Array(
      numeric, capsule_key::kUv, o.uv_bindings.size(),
      [&](Node element, std::size_t index) {
        const auto& binding = o.uv_bindings[index];
        Require(run, binding.source_attribute < mesh.attributes.size(),
                SurfaceCode::kUvMap);
        const auto object =
            json.Object(element, {},
                        {capsule_key::kSourceAttribute, capsule_key::kSourceSet,
                         capsule_key::kWireSet, capsule_key::kField});
        std::uint64_t source_attribute;
        if (json.Reading()) {
          source_attribute = json.Read64(object, capsule_key::kSourceAttribute);
          Require(run, source_attribute == binding.source_attribute,
                  SurfaceCode::kUvMap);
        } else {
          source_attribute = binding.source_attribute;
          json.Unsigned(object, capsule_key::kSourceAttribute,
                        source_attribute);
        }
        json.Small(object, capsule_key::kSourceSet, binding.source_set);
        json.Small(object, capsule_key::kWireSet, binding.wire_set);
        const auto* written = json.Reading() ? nullptr : &run.facts.uv[index];
        auto summary = field(
            object, capsule_key::kField,
            o.narrowing == SurfaceNarrowing::kExact32
                ? SurfaceNarrowing::kExact32
                : SurfaceNarrowing::kBoundedRne32,
            o.uv_bound_bits[index], written, source_attribute,
            DomainRows(mesh, mesh.attributes[Size(source_attribute)].domain),
            kUvComponents);
        // Summary validated the canonical nested witness first. This separate
        // private claim identity comes from the actual parsed outer wrapper,
        // never from an unparsed expectation or a synthetic V observation.
        if (json.Reading()) summary.source_attribute = source_attribute;
        claims.uv.push_back(summary);
        json.EndObject();
      });
  json.EndObject();
}
void MemberTable(Json& json, Node parent, OutputPayload const& o,
                 std::span<const SurfaceMemberView> captured, Run& run) {
  std::size_t count = 0;
  for (const auto& member : o.storage) {
    run.Check();
    if (member.kind != SurfaceMemberKind::kCapsuleJson) ++count;
  }
  run.Bound(count + 1, run.limits.max_members);
  if (json.Reading())
    Require(run, captured.size() == count + 1, SurfaceCode::kMemberInventory);
  json.Array(parent, capsule_key::kMembers, count,
             [&](Node element, std::size_t index) {
               // Core appends the sole JSON member last; table includes every
               // other member.
               Require(run, index < o.storage.size());
               const auto& expected = o.storage[index];
               Require(run, expected.kind != SurfaceMemberKind::kCapsuleJson);
               const auto actual =
                   json.Reading()
                       ? Find(captured, expected.path, run)
                       : SurfaceMemberView{expected.path, expected.kind,
                                           expected.bytes, expected.digest};
               Require(run, actual.kind == expected.kind,
                       SurfaceCode::kMemberInventory);
               const auto digest = Digest(actual.bytes, run);
               const auto node =
                   json.Object(element, {},
                               {capsule_key::kPath, capsule_key::kRole,
                                capsule_key::kBytes, capsule_key::kSha256});
               json.Text(node, capsule_key::kPath, expected.path);
               json.Text(node, capsule_key::kRole, RoleName(actual.kind));
               json.Unsigned(node, capsule_key::kBytes, actual.bytes.size(),
                             run.limits.max_member_bytes);
               json.Hex(node, capsule_key::kSha256, digest);
               json.EndObject();
             });
}
void Walk(Json& json, Binary& binary, const OutputPayload& o,
          std::uint64_t binary_bytes,
          std::span<const SurfaceMemberView> captured,
          CapsuleNumericClaims& claims, Run& run) {
  const auto root = json.Root(kRootKeys);
  json.Text(root, capsule_key::kSchema, kSurfaceBundleSchema);
  json.Text(root, capsule_key::kEncoding, EncodingName(o.encoding));
  json.Text(root, capsule_key::kRepresentation, kAnnotated);
  const auto coordinates =
      json.Object(root, capsule_key::kCoordinates,
                  {capsule_key::kKind, capsule_key::kDeclaredUnitHex});
  json.Text(coordinates, capsule_key::kKind,
            o.coordinates == SurfaceCoordinates::kStlUnitsUnspecified
                ? "stl-untransformed"
                : "declared-meters-rh-y-up");
  json.Hex(coordinates, capsule_key::kDeclaredUnitHex, Bytes(o.unit), true);
  json.EndObject();
  const auto acknowledgments = json.Object(
      root, capsule_key::kAcknowledgments,
      {capsule_key::kTriangleConversion, capsule_key::kCornerExpansion,
       capsule_key::kUnusedVertices});
  json.Boolean(acknowledgments, capsule_key::kTriangleConversion, true);
  json.Boolean(acknowledgments, capsule_key::kCornerExpansion, true);
  json.Boolean(acknowledgments, capsule_key::kUnusedVertices,
               o.unused_acknowledged);
  json.EndObject();
  Mesh(json, root, capsule_key::kOriginal, o.original.Source().View(), binary,
       run);
  Mesh(json, root, capsule_key::kTriangulated, o.triangles.Mesh().View(),
       binary, run, MeshRole::kTriangulated);
  MapsAndRelations(json, root, o, binary, binary_bytes, captured, run);
  json.Array(root, capsule_key::kChannels, o.channels.size(),
             [&](Node element, std::size_t index) {
               const auto& channel = o.channels[index];
               Require(run, channel.source_attribute == index,
                       SurfaceCode::kChannelCoverage);
               const auto node = json.Object(
                   element, {},
                   {capsule_key::kSourceAttribute, capsule_key::kRole,
                    capsule_key::kAcknowledgeOrdinaryViewLoss});
               json.Unsigned(node, capsule_key::kSourceAttribute,
                             channel.source_attribute);
               json.Text(node, capsule_key::kRole, ChannelName(channel.role));
               json.Boolean(node, capsule_key::kAcknowledgeOrdinaryViewLoss,
                            channel.acknowledge_ordinary_view_loss);
               json.EndObject();
             });
  Vector<std::uint8_t> seen{
      gltf_detail::Allocator<std::uint8_t>(run.allocation)};
  seen.resize(o.uv_bindings.size());
  std::optional<std::uint32_t> previous_set;
  std::uint64_t wire_ordinal = 0;
  for (const auto& binding : o.uv_bindings) {
    run.Check();
    Edge(run);
    Require(run, binding.wire_set < seen.size() && !seen[binding.wire_set],
            SurfaceCode::kUvMap);
    seen[binding.wire_set] = 1;
    Require(run,
            (!previous_set || binding.source_set > *previous_set) &&
                binding.wire_set == wire_ordinal,
            SurfaceCode::kUvMap);
    previous_set = binding.source_set;
    ++wire_ordinal;
  }
  for (const auto name : {std::string_view(capsule_key::kUvMapping),
                          std::string_view(capsule_key::kInverseUvMapping)})
    json.Array(
        root, name, o.uv_bindings.size(), [&](Node element, std::size_t index) {
          const auto& binding = o.uv_bindings[index];
          const auto node =
              json.Object(element, {},
                          {capsule_key::kSourceAttribute,
                           capsule_key::kSourceSet, capsule_key::kWireSet});
          Edge(run);
          json.Unsigned(node, capsule_key::kSourceAttribute,
                        binding.source_attribute);
          json.Small(node, capsule_key::kSourceSet, binding.source_set);
          json.Small(node, capsule_key::kWireSet, binding.wire_set);
          json.EndObject();
        });
  Catalog(json, root, o, binary, run);
  binary.Finish();
  Numeric(json, root, o, claims, run);
  // JSON field order is immaterial. Delaying this table until real binary
  // completion yields an acyclic actual-byte digest, without a predicted hash.
  MemberTable(json, root, o, captured, run);
  json.EndObject();
}
void VisitCapsule(Json& json, Binary& binary, const OutputPayload& output,
                  std::uint64_t binary_bytes,
                  std::span<const SurfaceMemberView> captured,
                  CapsuleNumericClaims& claims, Run& run) {
  try {
    Walk(json, binary, output, binary_bytes, captured, claims, run);
  } catch (const Failure& failure) {
    // Natural's logical limb ceiling is shared with the numeric kernel, whose
    // intrinsic diagnostic names Numeric. Here only that exact ceiling error
    // belongs to the active capsule phase. No other status/code/fact changes.
    const auto& original = failure.Diagnostic();
    if (failure.Status() == SurfaceStatus::kLimitExceeded &&
        original.code == SurfaceCode::kCountLimit &&
        original.stage == SurfaceStage::kNumeric) {
      auto diagnostic = original;
      diagnostic.stage = run.stage;
      throw Failure(failure.Status(), diagnostic);
    }
    throw;
  }
}
}  // namespace

void CopyCapsuleJson(std::span<const std::byte> source, Member& destination,
                     Run& run) {
  Require(run,
          destination.kind == SurfaceMemberKind::kCapsuleJson &&
              destination.bytes.empty(),
          SurfaceCode::kMemberInventory);
  run.Bound(source.size(), run.limits.max_capsule_json_bytes,
            SurfaceCode::kOutputLimit);
  run.Bound(source.size(), run.limits.max_member_bytes,
            SurfaceCode::kOutputLimit);
  run.Check();
  destination.bytes.resize(source.size());
  struct State {
    Run* run;
    std::span<const std::byte> source;
    std::byte* destination;
  } state{&run, source, destination.bytes.data()};
  Bulk(
      run, source.size(), sizeof(std::byte),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation&) {
        auto& s = *static_cast<State*>(opaque);
        for (auto i = first; i < last; ++i) {
          if ((i - first) % s.run->policy.surface.cancellation_block_scalars ==
              0)
            s.run->Check(i);
          s.destination[Size(i)] = s.source[Size(i)];
        }
      },
      &state);
}

void BuildCapsule(OutputPayload& output, Run& run) {
  Require(run,
          output.representation == SurfaceRepresentation::kAnnotatedBundleV1);
  for (const auto& member : output.storage) {
    run.Check();
    Require(run,
            member.kind != SurfaceMemberKind::kCapsuleBinary &&
                member.kind != SurfaceMemberKind::kCapsuleJson,
            SurfaceCode::kMemberInventory);
  }
  run.Bound(Sum(run, output.storage.size(), 2), run.limits.max_members);
  const auto length = BinaryLength(output, run);
  run.Bound(length, run.limits.max_member_bytes, SurfaceCode::kOutputLimit);
  std::uint64_t listed_bytes = length;
  for (const auto& member : output.storage) {
    run.Check();
    listed_bytes = Sum(run, listed_bytes, member.bytes.size());
  }
  run.Bound(listed_bytes, run.limits.max_total_member_bytes,
            SurfaceCode::kOutputLimit);
  Member binary_member(run.allocation);
  binary_member.path.assign(kCapsuleBinaryPath);
  binary_member.kind = SurfaceMemberKind::kCapsuleBinary;
  output.storage.push_back(std::move(binary_member));
  auto& binary_storage = output.storage.back();
  Json json(run, std::min(run.limits.max_member_bytes,
                          run.limits.max_total_member_bytes - listed_bytes));
  Binary binary(run, binary_storage.bytes);
  CapsuleNumericClaims ignored(run.allocation);
  VisitCapsule(json, binary, output, length, {}, ignored, run);
  Require(run, binary_storage.bytes.size() == length,
          SurfaceCode::kBinarySlice);
  binary_storage.digest = Digest(binary_storage.bytes, run);
  Member json_member(run.allocation);
  json_member.path.assign(kCapsuleJsonPath);
  json_member.kind = SurfaceMemberKind::kCapsuleJson;
  run.Bound(json.Text().size(), run.limits.max_member_bytes,
            SurfaceCode::kOutputLimit);
  CopyCapsuleJson({reinterpret_cast<const std::byte*>(json.Text().data()),
                   json.Text().size()},
                  json_member, run);
  json_member.digest = Digest(json_member.bytes, run);
  output.storage.push_back(std::move(json_member));
  std::uint64_t total = 0;
  for (const auto& member : output.storage) {
    run.Check();
    total = Sum(run, total, member.bytes.size());
  }
  run.Bound(total, run.limits.max_total_member_bytes,
            SurfaceCode::kOutputLimit);
}

CapsuleNumericClaims InspectCapsule(const OutputPayload& expected,
                                    std::span<const SurfaceMemberView> captured,
                                    Run& run) {
  run.Bound(captured.size(), run.limits.max_members);
  std::uint64_t total_bytes = 0;
  for (std::size_t index = 0; index < captured.size(); ++index) {
    run.Check(index);
    const auto& member = captured[index];
    run.Bound(member.bytes.size(), run.limits.max_member_bytes,
              SurfaceCode::kOutputLimit);
    total_bytes = Sum(run, total_bytes, member.bytes.size());
    run.Bound(total_bytes, run.limits.max_total_member_bytes,
              SurfaceCode::kOutputLimit);
  }
  const auto& json_member = Find(captured, kCapsuleJsonPath, run);
  const auto& binary_member = Find(captured, kCapsuleBinaryPath, run);
  Require(run,
          json_member.kind == SurfaceMemberKind::kCapsuleJson &&
              binary_member.kind == SurfaceMemberKind::kCapsuleBinary,
          SurfaceCode::kMemberInventory);
  run.Bound(json_member.bytes.size(), run.limits.max_capsule_json_bytes,
            SurfaceCode::kOutputLimit);
  run.Bound(binary_member.bytes.size(), run.limits.max_capsule_binary_bytes,
            SurfaceCode::kOutputLimit);
  const auto json_bytes =
      std::string_view(reinterpret_cast<const char*>(json_member.bytes.data()),
                       json_member.bytes.size());
  run.Bound(run.limits.max_json_depth,
            static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
  const auto capture = gltf_detail::CaptureJsonSnapshot(
      json_bytes,
      {Size(run.limits.max_capsule_json_bytes), Size(run.limits.max_json_nodes),
       Size(run.limits.max_capsule_json_bytes),
       static_cast<int>(run.limits.max_json_depth)},
      run.allocation);
  using Code = gltf_detail::JsonCaptureCode;
  if (capture.code != Code::kAccepted) {
    if (capture.code == Code::kCanceled)
      Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kStopRequested);
    if (capture.code == Code::kBudgetExceeded)
      Fail(SurfaceStatus::kBudgetExceeded, run.stage,
           SurfaceCode::kRequestBudget);
    if (capture.code == Code::kSizeLimit)
      Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kCountLimit);
    Bad(run, SurfaceCode::kJsonSyntax);
  }
  Require(run, capture.snapshot.has_value(), SurfaceCode::kJsonSyntax);
  Json json(run, *capture.snapshot);
  Binary binary(run, binary_member.bytes);
  CapsuleNumericClaims claims(run.allocation);
  VisitCapsule(json, binary, expected, binary_member.bytes.size(), captured,
               claims, run);
  return claims;
}
}  // namespace meshvale::interchange::surface_detail
