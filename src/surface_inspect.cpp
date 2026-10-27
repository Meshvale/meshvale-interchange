// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/surface_write.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_json_snapshot.h"
#include "gltf_metadata.h"
#include "surface_numeric.h"
#include "surface_output_internal.h"

namespace meshvale::interchange::surface_detail {
namespace {
constexpr std::size_t kOctetBits = 8;
constexpr std::uint32_t kMaximumFinite32 = UINT32_C(0x7f7fffff);
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr std::uint64_t kAlignment = 4;
constexpr std::uint32_t kGlbMagic = UINT32_C(0x46546c67), kGlbVersion = 2;
constexpr std::size_t kGlbHeaderWords = 3, kGlbChunkWords = 2;
constexpr std::size_t kGlbHeaderBytes = kGlbHeaderWords * kWordBytes;
constexpr std::size_t kGlbChunkHeaderBytes = kGlbChunkWords * kWordBytes;
constexpr std::uint64_t kGltfFloat = 5126, kGltfUint32 = 5125,
                        kTrianglesMode = 4;
constexpr std::uint64_t kBinaryStlHeaderBytes = 80, kBinaryStlCountBytes = 4;
constexpr std::uint64_t kBinaryStlFacetScalars = 12,
                        kBinaryStlAttributeBytes = 2;
constexpr std::uint64_t kBinaryStlFacetBytes =
    kBinaryStlFacetScalars * kWordBytes + kBinaryStlAttributeBytes;
constexpr unsigned char kFirstPathAscii = 0x21, kLastPathAscii = 0x7e;
constexpr std::string_view kPosition = "POSITION", kUvPrefix = "TEXCOORD_";
constexpr std::string_view kAsset = "asset", kVersion = "version",
                           kVersion2 = "2.0";
constexpr std::string_view kScenes = "scenes", kScene = "scene",
                           kNodes = "nodes", kMesh = "mesh", kMeshes = "meshes";
constexpr std::string_view kPrimitives = "primitives",
                           kAttributes = "attributes", kIndices = "indices";
constexpr std::string_view kMode = "mode", kMaterial = "material",
                           kAccessors = "accessors";
constexpr std::string_view kViews = "bufferViews", kBuffers = "buffers",
                           kBuffer = "buffer";
constexpr std::string_view kView = "bufferView", kOffset = "byteOffset",
                           kLength = "byteLength", kUri = "uri";
constexpr std::string_view kComponent = "componentType", kCount = "count",
                           kType = "type", kMin = "min", kMax = "max";
constexpr std::string_view kVec3 = "VEC3", kVec2 = "VEC2", kScalar = "SCALAR";
constexpr std::string_view kMaterials = "materials", kTextures = "textures",
                           kImages = "images", kSamplers = "samplers";
constexpr std::string_view kMaterialDefinitionKind = "material";
constexpr std::array<std::string_view, 6> kDefinitionKinds{
    kMaterialDefinitionKind, "texture", "image", "sampler",
    "buffer-view",           "buffer"};
constexpr std::array<std::string_view, 12> kRootKeys{
    kAsset, kScenes,  kScene,     kNodes,    kMeshes, kAccessors,
    kViews, kBuffers, kMaterials, kTextures, kImages, kSamplers};
constexpr std::array<std::string_view, 1> kAssetKeys{kVersion},
    kSceneKeys{kNodes}, kNodeKeys{kMesh}, kMeshKeys{kPrimitives};
constexpr std::array<std::string_view, 4> kPrimitiveKeys{kAttributes, kIndices,
                                                         kMode, kMaterial};
constexpr std::array<std::string_view, 2> kBufferKeys{kLength, kUri};
constexpr std::array<std::string_view, 3> kViewKeys{kBuffer, kOffset, kLength};
constexpr std::array<std::string_view, 7> kAccessorKeys{
    kView, kOffset, kComponent, kCount, kType, kMin, kMax};
using Json = gltf_detail::JsonSnapshot;
using Kind = gltf_detail::JsonKind;

[[noreturn]] void WireFail(Run& run, SurfaceCode code = SurfaceCode::kWireShape,
                           SurfaceSubject subject = SurfaceSubject::kNone,
                           std::optional<std::uint64_t> index = {},
                           std::optional<std::uint32_t> component = {}) {
  Fail(SurfaceStatus::kVerificationFailed, run.stage, code, subject, index, {},
       component);
}
void Relationship(Run& run) {
  const auto next = AddCount(run.facts.work.relationship_steps, 1);
  run.Bound(next, run.limits.max_relationship_steps);
  run.facts.work.relationship_steps = next;
}
std::uint64_t ReadLe(std::span<const std::byte> bytes, std::uint64_t offset,
                     std::size_t width, Run& run) {
  if (offset > bytes.size() || width > bytes.size() - offset ||
      width > sizeof(std::uint64_t))
    WireFail(run, SurfaceCode::kBinarySlice);
  std::uint64_t result = 0;
  for (std::size_t byte = 0; byte < width; ++byte) {
    run.Check(offset + byte);
    result |= static_cast<std::uint64_t>(
                  std::to_integer<unsigned char>(bytes[Size(offset) + byte]))
              << (byte * kOctetBits);
  }
  return result;
}
void ZeroPad(std::span<const std::byte> bytes, std::uint64_t first,
             std::uint64_t last, std::byte value, Run& run) {
  if (first > last || last > bytes.size())
    WireFail(run, SurfaceCode::kBinarySlice);
  for (auto i = first; i < last; ++i) {
    run.Check(i);
    if (bytes[Size(i)] != value) WireFail(run, SurfaceCode::kBinarySlice);
  }
}
std::uint64_t Aligned(std::uint64_t value) {
  const auto remainder = value % kAlignment;
  return remainder ? AddCount(value, kAlignment - remainder) : value;
}
std::optional<std::size_t> Field(const Json& doc, std::size_t node,
                                 std::string_view key, Run& run) {
  for (auto child = doc.FirstChild(node); child;
       child = doc.NextSibling(*child)) {
    run.Check();
    Relationship(run);
    if (doc.Key(*child) == key) return child;
  }
  return {};
}
std::size_t Required(const Json& doc, std::size_t node, std::string_view key,
                     Run& run) {
  const auto child = Field(doc, node, key, run);
  if (!child) WireFail(run);
  return *child;
}
void Keys(const Json& doc, std::size_t node,
          std::span<const std::string_view> allowed, Run& run) {
  if (doc.Kind(node) != Kind::kObject) WireFail(run);
  for (auto child = doc.FirstChild(node); child;
       child = doc.NextSibling(*child)) {
    run.Check();
    Relationship(run);
    if (std::find(allowed.begin(), allowed.end(), doc.Key(*child)) ==
        allowed.end())
      WireFail(run);
  }
}
std::uint64_t UInt(const Json& doc, std::size_t node, Run& run) {
  if (doc.Kind(node) != Kind::kNumber) WireFail(run);
  run.Bound(doc.Number(node).size(), run.limits.max_numeric_token_bytes);
  try {
    return gltf_detail::ExactUnsigned(doc.Number(node));
  } catch (const gltf_detail::GltfFailure&) {
    WireFail(run);
  }
}
std::uint64_t Count(const Json& doc, std::size_t array, Run& run) {
  if (doc.Kind(array) != Kind::kArray) WireFail(run);
  std::uint64_t count = 0;
  for (auto child = doc.FirstChild(array); child;
       child = doc.NextSibling(*child)) {
    run.Check();
    Relationship(run);
    ++count;
  }
  return count;
}
std::size_t At(const Json& doc, std::size_t array, std::uint64_t index,
               Run& run) {
  if (doc.Kind(array) != Kind::kArray) WireFail(run);
  auto child = doc.FirstChild(array);
  for (std::uint64_t i = 0; i < index && child; ++i) {
    run.Check(i);
    Relationship(run);
    child = doc.NextSibling(*child);
  }
  if (!child) WireFail(run);
  return *child;
}
std::string_view Text(const Json& doc, std::size_t node, Run& run) {
  if (doc.Kind(node) != Kind::kString) WireFail(run);
  return doc.String(node);
}
Json CaptureJson(std::span<const std::byte> bytes, Run& run) {
  run.Bound(bytes.size(), run.limits.max_json_bytes, SurfaceCode::kOutputLimit);
  const auto text = std::string_view{
      reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  auto result = gltf_detail::CaptureJsonSnapshot(
      text,
      {Size(run.limits.max_json_bytes), Size(run.limits.max_json_nodes),
       Size(run.limits.max_json_bytes),
       static_cast<int>(run.limits.max_json_depth)},
      run.allocation);
  switch (result.code) {
    case gltf_detail::JsonCaptureCode::kAccepted:
      return *result.snapshot;
    case gltf_detail::JsonCaptureCode::kBudgetExceeded:
      Fail(SurfaceStatus::kBudgetExceeded, run.stage,
           SurfaceCode::kRequestBudget);
    case gltf_detail::JsonCaptureCode::kCanceled:
      Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kStopRequested);
    case gltf_detail::JsonCaptureCode::kSizeLimit:
      Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kCountLimit);
    default:
      WireFail(run, SurfaceCode::kJsonSyntax);
  }
}
std::uint32_t Exact32Word(std::uint64_t actual, Run& run) {
  if (!Finite64(actual)) WireFail(run, SurfaceCode::kWireValue);
  const auto magnitude = actual & kBinary64MagnitudeMask;
  std::uint32_t low = 0, high = kMaximumFinite32;
  while (low < high) {
    run.Check();
    const auto middle = low + (high - low) / 2 + 1;
    if (Widen32(middle) <= magnitude)
      low = middle;
    else
      high = middle - 1;
  }
  if (Widen32(low) != magnitude) WireFail(run, SurfaceCode::kWireValue);
  return low | ((actual & kBinary64SignMask) ? kBinary32SignMask : 0);
}
std::uint64_t Attribute64(const geometry::AttributeReadView& attribute,
                          std::uint64_t scalar, Run& run) {
  if (const auto* f = std::get_if<std::span<const float>>(&attribute.values))
    return Widen32(std::bit_cast<std::uint32_t>((*f)[Size(scalar)]));
  if (const auto* d = std::get_if<std::span<const double>>(&attribute.values))
    return std::bit_cast<std::uint64_t>((*d)[Size(scalar)]);
  WireFail(run, SurfaceCode::kWireValue);
}
struct ActualCell {
  SurfaceDyadicError error;
  std::uint64_t bits = 0;
  bool changed = false, complete = false;
};
struct ActualField {
  explicit ActualField(const gltf_detail::AllocationContext& allocation)
      : cells(gltf_detail::Allocator<ActualCell>(allocation)) {}
  Vector<ActualCell> cells;
  std::optional<std::size_t> summary;
};
std::uint64_t SourceMapEntries(const OutputPayload& expected) {
  auto count = AddCount(AddCount(expected.triangles.FaceSources().size(),
                                 expected.triangles.CornerSources().size()),
                        expected.triangles.FaceOutputOffsets().size());
  for (auto size :
       {expected.triangle_sources.size(), expected.triangle_faces.size(),
        expected.corner_sources.size(), expected.corner_corners.size(),
        expected.vertex_sources.size(), expected.vertex_offsets.size(),
        expected.vertex_members.size(), expected.face_offsets.size(),
        expected.face_members.size(), expected.corner_offsets.size(),
        expected.corner_members.size()})
    count = AddCount(count, size);
  return count;
}
void AdmitValidationExtents(const OutputPayload& expected, Run& run) {
  // Necessary extents of the retained expected owner bound validation scratch.
  // They are not observations of the supplied wire or a substitute for reading
  // it.
  run.Bound(expected.primitives.size(), run.limits.max_wire_primitives);
  run.Bound(expected.vertex_sources.size(), run.limits.max_wire_vertices);
  run.Bound(expected.corner_sources.size(), run.limits.max_wire_corners);
  run.Bound(SourceMapEntries(expected), run.limits.max_map_entries,
            SurfaceCode::kMapLimit);
  run.Bound(expected.material_bindings.size(),
            run.limits.max_material_bindings);
  run.Bound(expected.catalog_uv.size(), run.limits.max_material_bindings);
  std::array<std::uint64_t, kDefinitionKinds.size()> tables{};
  for (const auto& definition : expected.definitions) {
    run.Check();
    for (std::size_t table = 0; table < tables.size(); ++table)
      if (definition.kind == kDefinitionKinds[table]) {
        tables[table] = AddCount(tables[table], 1);
        run.Bound(tables[table], run.limits.max_material_bindings);
        break;
      }
  }
  run.Bound(expected.images.size(), run.limits.max_resources);
  std::uint64_t image_bytes = 0;
  for (const auto& image : expected.images) {
    run.Check();
    image_bytes = AddCount(image_bytes, image.bytes.size());
    run.Bound(image_bytes, run.limits.max_resource_bytes);
  }
}
class Decoded {
 public:
  Decoded(const OutputPayload& expected, Run& run)
      : expected_(expected),
        run_(run),
        positions_(gltf_detail::Allocator<ActualCell>(run.allocation)),
        uv_(gltf_detail::Allocator<ActualField>(run.allocation)) {
    positions_.resize(
        Size(Product(expected.vertex_sources.size(), kPositionComponents)));
    for (std::size_t field = 0; field < expected.uv.size(); ++field) {
      uv_.emplace_back(run.allocation);
      uv_.back().cells.resize(
          Size(Product(expected.vertex_sources.size(), kUvComponents)));
    }
  }
  void BeginPosition() {
    if (!position_begun_) {
      run_.facts.positions.emplace();
      run_.facts.positions->kind = SurfaceNumericSummary::Kind::kDecodedWire;
      position_begun_ = true;
    }
  }
  void BeginUv(std::size_t field) {
    if (field >= uv_.size()) WireFail(run_);
    auto& target = uv_[field];
    if (target.summary) return;
    for (std::size_t i = 0; i < run_.facts.uv.size(); ++i)
      if (run_.facts.uv[i].source_attribute ==
          expected_.uv[field].binding.source_attribute) {
        target.summary = i;
        break;
      }
    if (!target.summary) {
      target.summary = run_.facts.uv.size();
      run_.facts.uv.emplace_back();
    }
    auto& summary = run_.facts.uv[*target.summary];
    summary = {};
    summary.kind = SurfaceNumericSummary::Kind::kDecodedWire;
    summary.source_attribute = expected_.uv[field].binding.source_attribute;
  }
  void Position(std::uint64_t first, std::span<const std::uint64_t> actual) {
    BeginPosition();
    if (actual.size() % kPositionComponents ||
        first > expected_.vertex_sources.size() ||
        actual.size() / kPositionComponents >
            expected_.vertex_sources.size() - first)
      WireFail(run_);
    const auto source = expected_.original.Source().View();
    struct State {
      Decoded* self;
      std::uint64_t first;
      std::span<const std::uint64_t> actual;
      geometry::MeshReadView source;
    } state{this, first, actual, source};
    Bulk(
        run_, actual.size(), sizeof(std::uint64_t),
        [](void* opaque, std::uint64_t begin, std::uint64_t end,
           JobObservation& observation) {
          auto& s = *static_cast<State*>(opaque);
          auto& self = *s.self;
          for (auto i = begin; i < end; ++i) {
            self.run_.Check(i);
            const auto wire = s.first + i / kPositionComponents,
                       component = i % kPositionComponents;
            const auto row = self.expected_.vertex_sources[Size(wire)];
            const auto original =
                std::bit_cast<std::uint64_t>(s.source.positions[Size(
                    Product(row, kPositionComponents) + component)]);
            const auto candidate = s.actual[Size(i)];
            ++observation.work.source_scalar_pairs;
            self.CheckValue(original, candidate,
                            self.expected_.position_bound_bits,
                            self.expected_.narrowing, SurfaceSubject::kVertex,
                            row, static_cast<std::uint32_t>(component));
            self.positions_[Size(Product(wire, kPositionComponents) +
                                 component)] = {
                AbsoluteError(original, candidate, self.run_.allocation,
                              self.run_.limits.max_numeric_integer_bits),
                candidate, original != candidate, true};
          }
        },
        &state);
  }
  void Uv(std::size_t field, std::uint64_t first,
          std::span<const std::uint64_t> actual) {
    if (field >= uv_.size()) WireFail(run_);
    auto& target = uv_[field];
    if (!target.summary) {
      for (std::size_t i = 0; i < run_.facts.uv.size(); ++i)
        if (run_.facts.uv[i].source_attribute ==
            expected_.uv[field].binding.source_attribute) {
          target.summary = i;
          break;
        }
      if (!target.summary) {
        target.summary = run_.facts.uv.size();
        run_.facts.uv.emplace_back();
      }
      auto& summary = run_.facts.uv[*target.summary];
      summary = {};
      summary.kind = SurfaceNumericSummary::Kind::kDecodedWire;
      summary.source_attribute = expected_.uv[field].binding.source_attribute;
    }
    if (actual.size() % kUvComponents ||
        first > expected_.vertex_sources.size() ||
        actual.size() / kUvComponents > expected_.vertex_sources.size() - first)
      WireFail(run_);
    const auto source = expected_.original.Source().View();
    struct State {
      Decoded* self;
      std::size_t field;
      std::uint64_t first;
      std::span<const std::uint64_t> actual;
      geometry::MeshReadView source;
    } state{this, field, first, actual, source};
    Bulk(
        run_, actual.size(), sizeof(std::uint64_t),
        [](void* opaque, std::uint64_t begin, std::uint64_t end,
           JobObservation& observation) {
          auto& s = *static_cast<State*>(opaque);
          auto& self = *s.self;
          const auto& uv = self.expected_.uv[s.field];
          const auto& a =
              s.source.attributes[Size(uv.binding.source_attribute)];
          for (auto i = begin; i < end; ++i) {
            self.run_.Check(i);
            const auto wire = s.first + i / kUvComponents,
                       component = i % kUvComponents;
            const auto row = a.domain == geometry::AttributeDomain::vertex
                                 ? self.expected_.vertex_sources[Size(wire)]
                                 : self.expected_.corner_corners[Size(wire)];
            const auto original = Attribute64(
                a, Product(row, kUvComponents) + component, self.run_);
            const auto candidate = s.actual[Size(i)];
            ++observation.work.source_scalar_pairs;
            self.CheckValue(
                original, candidate, self.expected_.uv_bound_bits[s.field],
                self.expected_.narrowing, SurfaceSubject::kAttribute,
                uv.binding.source_attribute,
                static_cast<std::uint32_t>(component));
            self.uv_[s.field]
                .cells[Size(Product(wire, kUvComponents) + component)] = {
                AbsoluteError(original, candidate, self.run_.allocation,
                              self.run_.limits.max_numeric_integer_bits),
                candidate, original != candidate, true};
          }
        },
        &state);
  }
  void Finish();

 private:
  void CheckValue(std::uint64_t source, std::uint64_t actual,
                  std::uint64_t bound, SurfaceNarrowing mode,
                  SurfaceSubject subject, std::uint64_t row,
                  std::uint32_t component) {
    bool admitted;
    if (expected_.encoding == SurfaceEncoding::kStlAscii &&
        mode == SurfaceNarrowing::kExact64)
      admitted = Finite64(actual) && source == actual;
    else
      admitted =
          IsRequired32(source, Exact32Word(actual, run_), mode, run_.allocation,
                       run_.limits.max_numeric_integer_bits);
    if (!admitted)
      WireFail(run_, SurfaceCode::kWireValue, subject, row, component);
    const auto error = AbsoluteError(source, actual, run_.allocation,
                                     run_.limits.max_numeric_integer_bits);
    if (!WithinBound(error, bound, run_.allocation,
                     run_.limits.max_numeric_integer_bits))
      WireFail(run_, SurfaceCode::kWireValue, subject, row, component);
  }
  void Retire(SurfaceNumericSummary& summary, std::span<const ActualCell> cells,
              std::span<const std::uint64_t> rows, std::uint32_t components,
              std::optional<std::uint64_t> attribute, bool vertex_domain);
  const OutputPayload& expected_;
  Run& run_;
  Vector<ActualCell> positions_;
  Vector<ActualField> uv_;
  bool position_begun_ = false;
};
void Decoded::Retire(SurfaceNumericSummary& summary,
                     std::span<const ActualCell> cells,
                     std::span<const std::uint64_t> rows,
                     std::uint32_t components,
                     std::optional<std::uint64_t> attribute,
                     bool vertex_domain) {
  const auto& offsets =
      vertex_domain ? expected_.vertex_offsets : expected_.corner_offsets;
  const auto& members =
      vertex_domain ? expected_.vertex_members : expected_.corner_members;
  for (auto row : rows)
    for (std::uint32_t component = 0; component < components; ++component) {
      run_.Check(row);
      const auto first = offsets[Size(row)], last = offsets[Size(row + 1)];
      if (first >= last || last > members.size())
        WireFail(run_, SurfaceCode::kWireMap);
      const ActualCell* group = nullptr;
      for (auto i = first; i < last; ++i) {
        run_.Check(i);
        Relationship(run_);
        const auto wire = members[Size(i)],
                   at = AddCount(Product(wire, components), component);
        if (at >= cells.size() || !cells[Size(at)].complete)
          WireFail(run_, SurfaceCode::kWireValue);
        const auto& actual = cells[Size(at)];
        if (group &&
            (group->bits != actual.bits || group->changed != actual.changed ||
             CompareError(group->error, actual.error) != 0))
          WireFail(run_, SurfaceCode::kWireValue);
        group = &actual;
      }
      // No summary counter/witness is changed until this entire actual group is
      // checked. A stop/failure in the preceding loop leaves it unretired.
      ++summary.examined_values;
      summary.changed_values += group->changed;
      if (CompareError(group->error, summary.max_absolute_error) > 0) {
        summary.max_absolute_error = group->error;
        summary.source_attribute = attribute;
        summary.source_row = row;
        summary.component = component;
      }
    }
}
void Decoded::Finish() {
  if (!position_begun_) {
    if (!expected_.vertex_sources.empty())
      WireFail(run_, SurfaceCode::kWireValue);
    run_.facts.positions.emplace();
    run_.facts.positions->kind = SurfaceNumericSummary::Kind::kDecodedWire;
    position_begun_ = true;
  }
  const auto source = expected_.original.Source().View();
  Vector<std::uint64_t> by_source{
      gltf_detail::Allocator<std::uint64_t>(run_.allocation)};
  by_source.resize(source.positions.size(), 0);
  for (auto vertex : expected_.used_vertices)
    for (std::uint64_t c = 0; c < kPositionComponents; ++c) {
      const auto first = expected_.vertex_offsets[Size(vertex)],
                 last = expected_.vertex_offsets[Size(vertex + 1)];
      if (first >= last || last > expected_.vertex_members.size())
        WireFail(run_, SurfaceCode::kWireMap);
      std::optional<std::uint64_t> bits;
      for (auto i = first; i < last; ++i) {
        run_.Check(i);
        Relationship(run_);
        const auto wire = expected_.vertex_members[Size(i)],
                   at = Product(wire, kPositionComponents) + c;
        if (at >= positions_.size() || !positions_[Size(at)].complete)
          WireFail(run_, SurfaceCode::kWireValue);
        const auto actual = positions_[Size(at)].bits;
        if (bits && *bits != actual) WireFail(run_, SurfaceCode::kWireValue);
        bits = actual;
      }
      by_source[Size(Product(vertex, kPositionComponents) + c)] =
          expected_.encoding == SurfaceEncoding::kStlAscii
              ? *bits
              : Exact32Word(*bits, run_);
    }
  try {
    CheckSurfaceCoincidence(expected_.original, expected_.used_vertices,
                            by_source, expected_.encoding, run_);
  } catch (const Failure& failure) {
    if (failure.Status() != SurfaceStatus::kUnsupported) throw;
    const auto d = failure.Diagnostic();
    Fail(SurfaceStatus::kVerificationFailed, run_.stage,
         SurfaceCode::kWireValue, d.subject, d.index, d.secondary_index,
         d.component);
  }
  for (std::uint64_t triangle = 0; triangle < expected_.triangle_sources.size();
       ++triangle) {
    run_.Check(triangle);
    std::array<std::uint64_t, kTrianglePositionScalars> original{}, actual{};
    for (std::uint64_t c = 0; c < kTriangleCorners; ++c) {
      const auto wire = Product(triangle, kTriangleCorners) + c;
      const auto vertex = expected_.vertex_sources[Size(wire)];
      for (std::uint64_t component = 0; component < kPositionComponents;
           ++component) {
        const auto at = Product(wire, kPositionComponents) + component;
        if (at >= positions_.size() || !positions_[Size(at)].complete)
          WireFail(run_, SurfaceCode::kWireValue);
        original[Size(Product(c, kPositionComponents) + component)] =
            std::bit_cast<std::uint64_t>(source.positions[Size(
                Product(vertex, kPositionComponents) + component)]);
        actual[Size(Product(c, kPositionComponents) + component)] =
            positions_[Size(at)].bits;
        run_.Pair();
      }
    }
    if (TriangleIssue(original, actual, run_.allocation,
                      run_.limits.max_numeric_integer_bits) !=
        SurfaceCode::kNone)
      WireFail(run_, SurfaceCode::kWireValue, SurfaceSubject::kFace,
               expected_.triangle_faces[Size(triangle)]);
  }
  Retire(*run_.facts.positions, positions_, expected_.used_vertices,
         kPositionComponents, {}, true);
  run_.facts.positions->complete = true;
  for (std::size_t field = 0; field < uv_.size(); ++field) {
    auto& actual = uv_[field];
    const auto& planned = expected_.uv[field];
    if (!actual.summary) {
      if (!planned.source_rows.empty()) WireFail(run_, SurfaceCode::kWireValue);
      for (std::size_t i = 0; i < run_.facts.uv.size(); ++i)
        if (run_.facts.uv[i].source_attribute ==
            planned.binding.source_attribute) {
          actual.summary = i;
          break;
        }
      if (!actual.summary) {
        actual.summary = run_.facts.uv.size();
        run_.facts.uv.emplace_back();
      }
      auto& summary = run_.facts.uv[*actual.summary];
      summary = {};
      summary.kind = SurfaceNumericSummary::Kind::kDecodedWire;
      summary.source_attribute = planned.binding.source_attribute;
    }
    auto& summary = run_.facts.uv[*actual.summary];
    const auto domain =
        source.attributes[Size(planned.binding.source_attribute)].domain;
    Retire(summary, actual.cells, planned.source_rows, kUvComponents,
           planned.binding.source_attribute,
           domain == geometry::AttributeDomain::vertex);
    summary.complete = true;
  }
}

struct Accessor {
  std::uint64_t offset = 0, count = 0, view = 0;
  std::uint32_t components = 0;
  bool indices = false;
  std::optional<std::array<std::uint64_t, kPositionComponents>> minimum,
      maximum;
};
struct Wire {
  explicit Wire(const gltf_detail::AllocationContext& allocation)
      : accessors(gltf_detail::Allocator<Accessor>(allocation)) {}
  std::optional<Json> json;
  std::span<const std::byte> binary;
  std::span<const std::byte> json_bytes;
  Vector<Accessor> accessors;
};
const SurfaceMemberView& NamedMember(std::span<const SurfaceMemberView> members,
                                     std::string_view path, Run& run) {
  const SurfaceMemberView* found = nullptr;
  for (const auto& member : members) {
    run.Check();
    if (member.path != path) continue;
    if (found) WireFail(run, SurfaceCode::kMemberInventory);
    found = &member;
  }
  if (!found) WireFail(run, SurfaceCode::kMemberInventory);
  return *found;
}
Wire OpenWire(const OutputPayload& expected,
              std::span<const SurfaceMemberView> members, Run& run,
              bool capture_json = true) {
  Wire wire(run.allocation);
  if (expected.encoding == SurfaceEncoding::kGltfJson) {
    wire.json_bytes = NamedMember(members, kMainGltfPath, run).bytes;
    for (const auto& member : members)
      if (member.path == kGeometryBufferPath) wire.binary = member.bytes;
  } else {
    const auto container = NamedMember(members, kMainGlbPath, run).bytes;
    if (container.size() < kGlbHeaderBytes + kGlbChunkHeaderBytes ||
        ReadLe(container, 0, kWordBytes, run) != kGlbMagic ||
        ReadLe(container, kWordBytes, kWordBytes, run) != kGlbVersion ||
        ReadLe(container, kWordBytes * 2, kWordBytes, run) != container.size())
      WireFail(run);
    std::uint64_t cursor = kGlbHeaderBytes, ordinal = 0;
    while (cursor < container.size()) {
      run.Check(cursor);
      const auto bytes = ReadLe(container, cursor, kWordBytes, run);
      const auto type =
          ReadLe(container, AddCount(cursor, kWordBytes), kWordBytes, run);
      cursor = AddCount(cursor, kGlbChunkHeaderBytes);
      if (bytes % kAlignment || cursor > container.size() ||
          bytes > container.size() - cursor)
        WireFail(run);
      const auto chunk = container.subspan(Size(cursor), Size(bytes));
      if (!ordinal && type == kGlbJsonChunkType)
        wire.json_bytes = chunk;
      else if (ordinal == 1 && type == kGlbBinChunkType)
        wire.binary = chunk;
      else
        WireFail(run);
      cursor = AddCount(cursor, bytes);
      ++ordinal;
    }
    if (!ordinal || ordinal > 2) WireFail(run);
    // The JSON snapshot admits only JSON whitespace. Verify canonical GLB
    // final padding separately; an interior space remains actual JSON data.
    auto logical = wire.json_bytes.size();
    while (logical && wire.json_bytes[logical - 1] == std::byte{' '}) --logical;
    if (wire.json_bytes.size() - logical > kAlignment - 1) WireFail(run);
    ZeroPad(wire.json_bytes, logical, wire.json_bytes.size(), std::byte{' '},
            run);
  }
  run.Bound(wire.json_bytes.size(), run.limits.max_json_bytes,
            SurfaceCode::kOutputLimit);
  if (capture_json) wire.json.emplace(CaptureJson(wire.json_bytes, run));
  return wire;
}
void ValidateWire(const OutputPayload& expected, Wire& wire, Run& run,
                  Decoded* decoded = nullptr) {
  const auto& doc = *wire.json;
  Keys(doc, 0, kRootKeys, run);
  const auto asset = Required(doc, 0, kAsset, run);
  Keys(doc, asset, kAssetKeys, run);
  if (Text(doc, Required(doc, asset, kVersion, run), run) != kVersion2)
    WireFail(run);
  const auto scenes = Required(doc, 0, kScenes, run);
  if (Count(doc, scenes, run) != 1 ||
      UInt(doc, Required(doc, 0, kScene, run), run) != 0)
    WireFail(run);
  const auto scene = At(doc, scenes, 0, run);
  Keys(doc, scene, kSceneKeys, run);
  const auto groups = static_cast<std::uint64_t>(expected.primitives.size());
  const auto nodes = Field(doc, 0, kNodes, run),
             meshes = Field(doc, 0, kMeshes, run);
  const auto roots = Field(doc, scene, kNodes, run);
  if (!groups) {
    if (nodes || meshes || roots || Field(doc, 0, kAccessors, run) ||
        Field(doc, 0, kBuffers, run) || Field(doc, 0, kViews, run) ||
        !wire.binary.empty())
      WireFail(run);
    return;
  }
  if (!nodes || !meshes || !roots || Count(doc, *nodes, run) != groups ||
      Count(doc, *meshes, run) != groups || Count(doc, *roots, run) != groups)
    WireFail(run);
  const auto accessors = Required(doc, 0, kAccessors, run),
             views = Required(doc, 0, kViews, run);
  const auto buffers = Required(doc, 0, kBuffers, run);
  if (Count(doc, buffers, run) != 1) WireFail(run);
  const auto buffer = At(doc, buffers, 0, run);
  Keys(doc, buffer, kBufferKeys, run);
  const auto logical_bytes =
      UInt(doc, Required(doc, buffer, kLength, run), run);
  run.Bound(logical_bytes, run.limits.max_binary_bytes);
  if (!logical_bytes || logical_bytes > wire.binary.size())
    WireFail(run, SurfaceCode::kBinarySlice);
  const auto uri = Field(doc, buffer, kUri, run);
  if (expected.encoding == SurfaceEncoding::kGltfJson) {
    if (!uri || Text(doc, *uri, run) != kGeometryBufferPath ||
        logical_bytes != wire.binary.size())
      WireFail(run);
  } else {
    if (uri || wire.binary.size() - logical_bytes > kAlignment - 1)
      WireFail(run);
    ZeroPad(wire.binary, logical_bytes, wire.binary.size(), std::byte{0}, run);
  }
  std::uint64_t cursor = 0, next_accessor = 0;
  auto accessor = [&](std::uint64_t selected, std::uint64_t rows,
                      std::uint32_t components, bool indices, bool position) {
    if (selected != next_accessor++) WireFail(run);
    const auto node = At(doc, accessors, selected, run);
    Keys(doc, node, kAccessorKeys, run);
    const auto view_index = UInt(doc, Required(doc, node, kView, run), run);
    const auto offset = UInt(doc, Required(doc, node, kOffset, run), run);
    if (view_index != selected || offset || !rows ||
        UInt(doc, Required(doc, node, kCount, run), run) != rows ||
        UInt(doc, Required(doc, node, kComponent, run), run) !=
            (indices ? kGltfUint32 : kGltfFloat) ||
        Text(doc, Required(doc, node, kType, run), run) !=
            (indices                             ? kScalar
             : components == kPositionComponents ? kVec3
                                                 : kVec2))
      WireFail(run);
    const auto view = At(doc, views, view_index, run);
    Keys(doc, view, kViewKeys, run);
    const auto byte_offset = UInt(doc, Required(doc, view, kOffset, run), run);
    const auto byte_length = UInt(doc, Required(doc, view, kLength, run), run);
    const auto wanted = Product(Product(rows, components), kWordBytes),
               aligned = Aligned(cursor);
    if (UInt(doc, Required(doc, view, kBuffer, run), run) != 0 ||
        byte_offset != aligned || byte_length != wanted ||
        byte_offset > logical_bytes ||
        byte_length > logical_bytes - byte_offset)
      WireFail(run, SurfaceCode::kBinarySlice);
    ZeroPad(wire.binary, cursor, aligned, std::byte{0}, run);
    cursor = AddCount(byte_offset, byte_length);
    Accessor entry;
    entry.offset = byte_offset;
    entry.count = rows;
    entry.view = view_index;
    entry.components = components;
    entry.indices = indices;
    const auto minimum = Field(doc, node, kMin, run),
               maximum = Field(doc, node, kMax, run);
    if (position) {
      if (decoded) decoded->BeginPosition();
      if (!minimum || !maximum ||
          Count(doc, *minimum, run) != kPositionComponents ||
          Count(doc, *maximum, run) != kPositionComponents)
        WireFail(run);
      entry.minimum.emplace();
      entry.maximum.emplace();
      for (std::uint64_t component = 0; component < kPositionComponents;
           ++component) {
        for (bool upper : {false, true}) {
          const auto value =
              At(doc, upper ? *maximum : *minimum, component, run);
          if (doc.Kind(value) != Kind::kNumber) WireFail(run);
          run.Bound(doc.Number(value).size(),
                    run.limits.max_numeric_token_bytes);
          const auto bits = Decimal64(doc.Number(value), run.allocation,
                                      run.limits.max_numeric_integer_bits);
          (void)Exact32Word(bits, run);
          (upper ? *entry.maximum : *entry.minimum)[Size(component)] = bits;
        }
      }
    } else if (minimum || maximum)
      WireFail(run);
    wire.accessors.push_back(std::move(entry));
  };
  for (std::uint64_t p = 0; p < groups; ++p) {
    run.Check(p);
    const auto node = At(doc, *nodes, p, run);
    Keys(doc, node, kNodeKeys, run);
    if (UInt(doc, Required(doc, node, kMesh, run), run) != p ||
        UInt(doc, At(doc, *roots, p, run), run) != p)
      WireFail(run);
    const auto mesh = At(doc, *meshes, p, run);
    Keys(doc, mesh, kMeshKeys, run);
    const auto primitives = Required(doc, mesh, kPrimitives, run);
    if (Count(doc, primitives, run) != 1) WireFail(run);
    const auto primitive = At(doc, primitives, 0, run);
    Keys(doc, primitive, kPrimitiveKeys, run);
    const auto& planned = expected.primitives[Size(p)];
    if (UInt(doc, Required(doc, primitive, kMode, run), run) != kTrianglesMode)
      WireFail(run);
    const auto material = Field(doc, primitive, kMaterial, run);
    if (material.has_value() != planned.view.material.has_value() ||
        (material && UInt(doc, *material, run) != *planned.view.material))
      WireFail(run, SurfaceCode::kMaterialReference);
    const auto attributes = Required(doc, primitive, kAttributes, run);
    if (doc.Kind(attributes) != Kind::kObject) WireFail(run);
    std::uint64_t channel_count = 0;
    for (auto child = doc.FirstChild(attributes); child;
         child = doc.NextSibling(*child))
      ++channel_count;
    if (channel_count != AddCount(expected.uv.size(), 1)) WireFail(run);
    accessor(UInt(doc, Required(doc, attributes, kPosition, run), run),
             planned.view.wire_vertices, kPositionComponents, false, true);
    for (std::uint64_t uv = 0; uv < expected.uv.size(); ++uv) {
      String name{gltf_detail::Allocator<char>(run.allocation)};
      name.assign(kUvPrefix);
      AppendUnsigned(name, uv);
      accessor(UInt(doc, Required(doc, attributes, name, run), run),
               planned.view.wire_vertices, kUvComponents, false, false);
    }
    accessor(UInt(doc, Required(doc, primitive, kIndices, run), run),
             planned.view.wire_vertices, 1, true, false);
  }
  if (Count(doc, accessors, run) != next_accessor) WireFail(run);
  const auto image_views =
      expected.encoding == SurfaceEncoding::kGlb ? expected.images.size() : 0;
  if (Count(doc, views, run) != AddCount(next_accessor, image_views))
    WireFail(run);
  if (image_views) {
    const auto images = Required(doc, 0, kImages, run);
    if (Count(doc, images, run) != expected.images.size()) WireFail(run);
    for (std::size_t i = 0; i < expected.images.size(); ++i) {
      run.Check(i);
      const auto image = At(doc, images, i, run);
      const auto selected = UInt(doc, Required(doc, image, kView, run), run);
      if (selected != AddCount(next_accessor, i)) WireFail(run);
      const auto view = At(doc, views, selected, run);
      Keys(doc, view, kViewKeys, run);
      const auto offset = UInt(doc, Required(doc, view, kOffset, run), run);
      const auto bytes = UInt(doc, Required(doc, view, kLength, run), run),
                 aligned = Aligned(cursor);
      if (UInt(doc, Required(doc, view, kBuffer, run), run) != 0 ||
          offset != aligned || bytes != expected.images[i].bytes.size() ||
          offset > logical_bytes || bytes > logical_bytes - offset)
        WireFail(run, SurfaceCode::kResourceBytes, SurfaceSubject::kImage, i);
      ZeroPad(wire.binary, cursor, aligned, std::byte{0}, run);
      for (std::uint64_t j = 0; j < bytes; ++j) {
        run.Check(j);
        const bool equal =
            wire.binary[Size(offset + j)] == expected.images[i].bytes[Size(j)];
        run.Pair();
        if (!equal)
          WireFail(run, SurfaceCode::kResourceBytes, SurfaceSubject::kImage, i);
      }
      cursor = AddCount(offset, bytes);
    }
  }
  if (cursor != logical_bytes) WireFail(run, SurfaceCode::kBinarySlice);
}
bool LessFinite(std::uint64_t a, std::uint64_t b) noexcept {
  return ((a & kBinary64SignMask) ? ~a : (a | kBinary64SignMask)) <
         ((b & kBinary64SignMask) ? ~b : (b | kBinary64SignMask));
}
Vector<std::uint64_t> ReadBlock(const Wire& wire, std::uint64_t selected,
                                Run& run) {
  if (selected >= wire.accessors.size()) WireFail(run);
  const auto& accessor = wire.accessors[Size(selected)];
  Vector<std::uint64_t> values{
      gltf_detail::Allocator<std::uint64_t>(run.allocation)};
  values.resize(Size(Product(accessor.count, accessor.components)));
  for (std::uint64_t i = 0; i < values.size(); ++i) {
    run.Check(i);
    const auto word =
        ReadLe(wire.binary, AddCount(accessor.offset, Product(i, kWordBytes)),
               kWordBytes, run);
    if (accessor.indices) {
      if (word != i || word >= kUnsigned32WireMaximum)
        WireFail(run, SurfaceCode::kWireValue);
      values[Size(i)] = word;
    } else {
      if (!Finite32(static_cast<std::uint32_t>(word)))
        WireFail(run, SurfaceCode::kWireValue);
      values[Size(i)] = Widen32(static_cast<std::uint32_t>(word));
    }
  }
  if (accessor.minimum) {
    std::array<std::uint64_t, kPositionComponents> minimum{}, maximum{};
    for (std::uint64_t row = 0; row < accessor.count; ++row)
      for (std::uint64_t component = 0; component < kPositionComponents;
           ++component) {
        run.Check(row);
        const auto bits =
            values[Size(Product(row, kPositionComponents) + component)];
        if (!row || LessFinite(bits, minimum[Size(component)]))
          minimum[Size(component)] = bits;
        if (!row || LessFinite(maximum[Size(component)], bits))
          maximum[Size(component)] = bits;
      }
    if (minimum != *accessor.minimum || maximum != *accessor.maximum)
      WireFail(run, SurfaceCode::kWireValue);
  }
  return values;
}
void CompareImageMembers(const OutputPayload& expected,
                         std::span<const SurfaceMemberView> members, Run& run) {
  if (expected.encoding != SurfaceEncoding::kGltfJson) return;
  for (const auto& image : expected.images) {
    const auto& actual = NamedMember(members, image.ordinary_path, run);
    if (actual.bytes.size() != image.bytes.size())
      WireFail(run, SurfaceCode::kResourceBytes, SurfaceSubject::kImage,
               image.wire);
    for (std::size_t i = 0; i < image.bytes.size(); ++i) {
      run.Check(i);
      const bool equal = actual.bytes[i] == image.bytes[i];
      run.Pair();
      if (!equal)
        WireFail(run, SurfaceCode::kResourceBytes, SurfaceSubject::kImage,
                 image.wire);
    }
  }
}
Vector<SurfaceMaterialFactorView> WireFactors(const OutputPayload& expected,
                                              const Json& actual, Run& run) {
  try {
    return ObserveWireCatalog(expected, actual, run);
  } catch (const Failure& failure) {
    if (failure.Status() != SurfaceStatus::kUnsupported ||
        failure.Diagnostic().code == SurfaceCode::kNumericProfile)
      throw;
    WireFail(run, SurfaceCode::kMaterialReference);
  }
}
void PairNormals(const OutputPayload& expected, std::uint64_t triangle,
                 std::span<const std::uint64_t, kPositionComponents> actual,
                 Run& run) {
  const SurfaceChannelRule* selected = nullptr;
  for (const auto& rule : expected.channels)
    if (rule.role == SurfaceChannelRole::kStlCornerNormal) selected = &rule;
  if (!selected) WireFail(run, SurfaceCode::kWireValue);
  const auto source = expected.original.Source().View();
  const auto& normal = source.attributes[Size(selected->source_attribute)];
  const auto corner =
      expected.corner_corners[Size(Product(triangle, kTriangleCorners))];
  for (std::uint32_t component = 0; component < kPositionComponents;
       ++component) {
    run.Check(component);
    const auto original = Attribute64(
        normal, Product(corner, kPositionComponents) + component, run);
    const auto value = actual[component];
    run.Pair();
    if (original != value)
      WireFail(run, SurfaceCode::kWireValue, SurfaceSubject::kAttribute,
               selected->source_attribute, component);
  }
}
void NativeStlFacet(
    const StlDocument& native, std::uint64_t facet,
    std::array<std::uint64_t, kPositionComponents>& normal,
    std::array<std::uint64_t, kTrianglePositionScalars>& positions, Run& run) {
  const auto mesh = native.Mesh().View();
  if (mesh.attributes.size() != 1 ||
      mesh.attributes[0].domain != geometry::AttributeDomain::face ||
      mesh.attributes[0].components != kPositionComponents ||
      mesh.attributes[0].offsets || mesh.attributes[0].present)
    WireFail(run, SurfaceCode::kWireValue);
  for (std::uint64_t c = 0; c < kPositionComponents; ++c) {
    const auto actual = Attribute64(
        mesh.attributes[0], Product(facet, kPositionComponents) + c, run);
    const auto parsed = normal[Size(c)];
    run.Pair();
    if (actual != parsed) WireFail(run, SurfaceCode::kWireValue);
    normal[Size(c)] = actual;
  }
  for (std::uint64_t corner = 0; corner < kTriangleCorners; ++corner) {
    const auto row = Product(facet, kTriangleCorners) + corner;
    if (row >= mesh.corner_vertices.size() ||
        mesh.corner_vertices[Size(row)] != row)
      WireFail(run, SurfaceCode::kWireValue);
    for (std::uint64_t component = 0; component < kPositionComponents;
         ++component) {
      const auto at = Product(row, kPositionComponents) + component;
      if (at >= mesh.positions.size()) WireFail(run, SurfaceCode::kWireValue);
      const auto actual =
          std::bit_cast<std::uint64_t>(mesh.positions[Size(at)]);
      const auto index = Product(corner, kPositionComponents) + component;
      const auto parsed = positions[Size(index)];
      run.Pair();
      if (actual != parsed) WireFail(run, SurfaceCode::kWireValue);
      positions[Size(index)] = actual;
    }
  }
}
constexpr std::string_view kSolid = "solid", kEndSolid = "endsolid",
                           kFacet = "facet", kNormal = "normal";
constexpr std::string_view kOuter = "outer", kLoop = "loop", kVertex = "vertex",
                           kEndLoop = "endloop", kEndFacet = "endfacet";
class AsciiTokens {
 public:
  AsciiTokens(std::span<const std::byte> bytes, Run& run)
      : text_(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        run_(run) {}
  void Name(std::string_view keyword, std::string_view expected) {
    Space();
    const auto first = cursor_;
    while (cursor_ < text_.size() && text_[cursor_] != '\n' &&
           text_[cursor_] != '\r') {
      run_.Check(cursor_);
      ++cursor_;
    }
    const auto line = text_.substr(first, cursor_ - first);
    if (!line.starts_with(keyword) ||
        line.size() != AddCount(AddCount(keyword.size(), 1), expected.size()) ||
        line[keyword.size()] != ' ' ||
        line.substr(keyword.size() + 1) != expected)
      WireFail(run_, SurfaceCode::kWireValue);
  }
  void Word(std::string_view expected) {
    if (Token() != expected) WireFail(run_, SurfaceCode::kWireShape);
  }
  std::uint64_t Number() {
    const auto token = Token();
    run_.Bound(token.size(), run_.limits.max_numeric_token_bytes);
    try {
      return Decimal64(token, run_.allocation,
                       run_.limits.max_numeric_integer_bits);
    } catch (const Failure& failure) {
      if (failure.Status() != SurfaceStatus::kUnsupported) throw;
      WireFail(run_, SurfaceCode::kWireValue);
    }
  }
  void End() {
    Space();
    if (cursor_ != text_.size()) WireFail(run_);
  }

 private:
  bool Whitespace(char c) const noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }
  void Space() {
    while (cursor_ < text_.size() && Whitespace(text_[cursor_])) {
      run_.Check(cursor_);
      ++cursor_;
    }
  }
  std::string_view Token() {
    Space();
    const auto first = cursor_;
    while (cursor_ < text_.size() && !Whitespace(text_[cursor_])) {
      run_.Check(cursor_);
      ++cursor_;
    }
    if (cursor_ == first) WireFail(run_);
    return text_.substr(first, cursor_ - first);
  }
  std::string_view text_;
  Run& run_;
  std::size_t cursor_ = 0;
};
void ReadStlFields(const OutputPayload& expected,
                   std::span<const std::byte> bytes, Decoded& decoded, Run& run,
                   const StlDocument* native = nullptr) {
  const auto facets = expected.triangle_sources.size();
  if (expected.encoding == SurfaceEncoding::kStlBinary) {
    const auto base = AddCount(kBinaryStlHeaderBytes, kBinaryStlCountBytes);
    if (bytes.size() != AddCount(base, Product(facets, kBinaryStlFacetBytes)) ||
        ReadLe(bytes, kBinaryStlHeaderBytes, kBinaryStlCountBytes, run) !=
            facets ||
        expected.binary_header.size() != kBinaryStlHeaderBytes)
      WireFail(run);
    for (std::size_t i = 0; i < expected.binary_header.size(); ++i) {
      run.Check(i);
      if (bytes[i] != expected.binary_header[i])
        WireFail(run, SurfaceCode::kWireValue);
    }
    for (std::uint64_t facet = 0; facet < facets; ++facet) {
      const auto first = AddCount(base, Product(facet, kBinaryStlFacetBytes));
      std::array<std::uint64_t, kPositionComponents> normal{};
      std::array<std::uint64_t, kTrianglePositionScalars> positions{};
      for (std::uint64_t i = 0; i < kBinaryStlFacetScalars; ++i) {
        const auto bits = static_cast<std::uint32_t>(ReadLe(
            bytes, AddCount(first, Product(i, kWordBytes)), kWordBytes, run));
        if (!Finite32(bits)) WireFail(run, SurfaceCode::kWireValue);
        if (i < kPositionComponents)
          normal[Size(i)] = Widen32(bits);
        else
          positions[Size(i - kPositionComponents)] = Widen32(bits);
      }
      if (ReadLe(bytes,
                 AddCount(first, Product(kBinaryStlFacetScalars, kWordBytes)),
                 kBinaryStlAttributeBytes, run))
        WireFail(run);
      if (native) NativeStlFacet(*native, facet, normal, positions, run);
      PairNormals(expected, facet, normal, run);
      decoded.Position(Product(facet, kTriangleCorners), positions);
    }
  } else {
    AsciiTokens tokens(bytes, run);
    tokens.Name(kSolid, expected.ascii_name);
    for (std::uint64_t facet = 0; facet < facets; ++facet) {
      tokens.Word(kFacet);
      tokens.Word(kNormal);
      std::array<std::uint64_t, kPositionComponents> normal{};
      std::array<std::uint64_t, kTrianglePositionScalars> positions{};
      for (auto& component : normal) component = tokens.Number();
      tokens.Word(kOuter);
      tokens.Word(kLoop);
      for (std::uint64_t corner = 0; corner < kTriangleCorners; ++corner) {
        tokens.Word(kVertex);
        for (std::uint64_t component = 0; component < kPositionComponents;
             ++component)
          positions[Size(Product(corner, kPositionComponents) + component)] =
              tokens.Number();
      }
      tokens.Word(kEndLoop);
      tokens.Word(kEndFacet);
      if (native) NativeStlFacet(*native, facet, normal, positions, run);
      PairNormals(expected, facet, normal, run);
      decoded.Position(Product(facet, kTriangleCorners), positions);
    }
    tokens.Name(kEndSolid, expected.ascii_name);
    tokens.End();
  }
}
}  // namespace
void VerifyEncodedNumeric(OutputPayload& output, Run& run) {
  run.stage = SurfaceStage::kEncode;
  Decoded decoded(output, run);
  if (output.encoding == SurfaceEncoding::kStlAscii ||
      output.encoding == SurfaceEncoding::kStlBinary) {
    ReadStlFields(output,
                  MemberByKind(output.members, SurfaceMemberKind::kStl).bytes,
                  decoded, run);
  } else {
    auto wire = OpenWire(output, output.members, run);
    ValidateWire(output, wire, run, &decoded);
    for (std::size_t primitive = 0; primitive < output.primitives.size();
         ++primitive) {
      const auto& p = output.primitives[primitive];
      decoded.BeginPosition();
      const auto positions = ReadBlock(wire, *p.position_accessor, run);
      decoded.Position(p.view.first_wire_vertex, positions);
      for (std::size_t field = 0; field < p.uv_accessors.size(); ++field) {
        decoded.BeginUv(field);
        const auto uv = ReadBlock(wire, p.uv_accessors[field], run);
        decoded.Uv(field, p.view.first_wire_vertex, uv);
      }
      (void)ReadBlock(wire, *p.index_accessor, run);
    }
    CompareImageMembers(output, output.members, run);
    output.material_factors = WireFactors(output, *wire.json, run);
    for (auto& definition : output.definitions)
      if (definition.kind == kMaterialDefinitionKind && definition.wire)
        definition.wire_factors =
            output.material_factors[Size(*definition.wire)].wire_core_f32;
  }
  decoded.Finish();
}
namespace {
bool CanonicalPath(std::string_view path) noexcept {
  if (path.empty() || path.front() == '/' || path.back() == '/') return false;
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      const auto segment = path.substr(begin, i - begin);
      if (segment.empty() || segment == "." || segment == "..") return false;
      begin = i + 1;
    } else {
      const auto c = static_cast<unsigned char>(path[i]);
      if (c < kFirstPathAscii || c > kLastPathAscii || c == '\\' || c == ':' ||
          c == '%')
        return false;
    }
  }
  return true;
}
void CaptureMembers(const OutputPayload& expected,
                    std::span<const SuppliedSurfaceMember> supplied,
                    InspectionPayload& captured, Run& run) {
  run.stage = SurfaceStage::kCapture;
  run.Bound(supplied.size(), run.limits.max_members);
  if (supplied.size() != expected.members.size())
    WireFail(run, SurfaceCode::kMemberInventory);
  std::uint64_t bytes = 0, image_bytes = 0;
  for (std::size_t i = 0; i < supplied.size(); ++i) {
    run.Check(i);
    if (!CanonicalPath(supplied[i].path))
      WireFail(run, SurfaceCode::kMemberPath, SurfaceSubject::kMember, i);
    run.Bound(supplied[i].bytes.size(), run.limits.max_member_bytes,
              SurfaceCode::kOutputLimit);
    bytes = AddCount(bytes, supplied[i].bytes.size());
    run.Bound(bytes, run.limits.max_total_member_bytes,
              SurfaceCode::kOutputLimit);
    for (std::size_t j = 0; j < i; ++j)
      if (supplied[i].path == supplied[j].path)
        WireFail(run, SurfaceCode::kMemberInventory, SurfaceSubject::kMember,
                 i);
    const auto required = std::find_if(
        expected.members.begin(), expected.members.end(),
        [&](const auto& member) { return member.path == supplied[i].path; });
    if (required == expected.members.end())
      WireFail(run, SurfaceCode::kMemberInventory);
    if (required->kind == SurfaceMemberKind::kGltf)
      run.Bound(supplied[i].bytes.size(), run.limits.max_json_bytes,
                SurfaceCode::kOutputLimit);
    if (required->kind == SurfaceMemberKind::kImage) {
      image_bytes = AddCount(image_bytes, supplied[i].bytes.size());
      run.Bound(image_bytes, run.limits.max_resource_bytes);
    }
  }
  // Inventory and all whole physical byte extents are admitted before copying.
  // No expected digest or expected payload byte participates in this capture.
  for (const auto& required : expected.members) {
    const SuppliedSurfaceMember* input = nullptr;
    for (const auto& candidate : supplied)
      if (candidate.path == required.path) input = &candidate;
    if (!input) WireFail(run, SurfaceCode::kMemberInventory);
    Member member(run.allocation);
    member.path.assign(input->path);
    member.kind = required.kind;
    member.bytes.resize(input->bytes.size());
    struct Copy {
      Run* run;
      std::span<const std::byte> input;
      std::span<std::byte> output;
    } state{&run, input->bytes, member.bytes};
    Bulk(
        run, input->bytes.size(), 1,
        [](void* opaque, std::uint64_t first, std::uint64_t last,
           JobObservation&) {
          auto& s = *static_cast<Copy*>(opaque);
          for (auto i = first; i < last; ++i) {
            s.run->Check(i);
            s.output[Size(i)] = s.input[Size(i)];
          }
        },
        &state);
    captured.storage.push_back(std::move(member));
  }
  RefreshViews(captured, run);
}
[[noreturn]] void GltfFailure(const GltfDiagnostic& diagnostic, Run& run) {
  if (diagnostic.status == GltfStatus::kBudgetExceeded)
    Fail(SurfaceStatus::kBudgetExceeded, run.stage, SurfaceCode::kGltfDelegate);
  if (diagnostic.status == GltfStatus::kCanceled)
    Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kGltfDelegate);
  Fail(SurfaceStatus::kVerificationFailed, run.stage,
       SurfaceCode::kGltfDelegate);
}
Vector<std::uint64_t> NativeValues(const GltfChannelView& channel,
                                   const Accessor& accessor, const Wire& wire,
                                   Run& run) {
  if (channel.component_type != GltfComponentType::kFloat32 ||
      channel.normalized || channel.sparse_source ||
      !channel.sparse_destinations.empty() ||
      !channel.normalized_values.empty() ||
      channel.element_count != accessor.count ||
      channel.base_buffer_view != accessor.view || channel.base_byte_offset ||
      channel.base_byte_stride != Product(accessor.components, kWordBytes) ||
      channel.shape != (accessor.components == kPositionComponents
                            ? GltfAccessorShape::kVec3
                            : GltfAccessorShape::kVec2))
    WireFail(run);
  const auto* values = std::get_if<std::span<const float>>(&channel.raw);
  if (!values || values->size() != Product(accessor.count, accessor.components))
    WireFail(run);
  Vector<std::uint64_t> actual{
      gltf_detail::Allocator<std::uint64_t>(run.allocation)};
  actual.resize(values->size());
  const auto bytes = ReadBlock(wire, channel.accessor_index, run);
  for (std::size_t i = 0; i < actual.size(); ++i) {
    run.Check(i);
    const auto native = Widen32(std::bit_cast<std::uint32_t>((*values)[i]));
    const auto parsed = bytes[i];
    run.Pair();
    if (native != parsed) WireFail(run, SurfaceCode::kWireValue);
    actual[i] = native;
  }
  for (bool upper : {false, true}) {
    const auto& bound = upper ? channel.exact_max : channel.exact_min;
    const auto& declared = upper ? accessor.maximum : accessor.minimum;
    if (bound.has_value() != declared.has_value()) WireFail(run);
    if (bound) {
      const auto* f = std::get_if<std::span<const float>>(&*bound);
      if (!f || f->size() != kPositionComponents) WireFail(run);
      for (std::size_t i = 0; i < kPositionComponents; ++i) {
        const auto native = Widen32(std::bit_cast<std::uint32_t>((*f)[i]));
        const auto parsed = (*declared)[i];
        run.Pair();
        if (native != parsed) WireFail(run, SurfaceCode::kWireValue);
      }
    }
  }
  return actual;
}
}  // namespace
void CompareNativeBytes(std::span<const std::byte> native,
                        std::span<const std::byte> captured, Run& run) {
  if (native.size() != captured.size()) WireFail(run);
  struct State {
    Run* run;
    std::span<const std::byte> native, captured;
  } state{&run, native, captured};
  Bulk(
      run, native.size(), sizeof(std::byte),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation&) {
        auto& state = *static_cast<State*>(opaque);
        for (auto i = first; i < last; ++i) {
          state.run->Check(i);
          const bool equal = state.native[Size(i)] == state.captured[Size(i)];
          if (!equal) WireFail(*state.run);
        }
      },
      &state);
}
void CompareNativeIdentity(std::span<const std::uint32_t> indices, Run& run) {
  struct State {
    Run* run;
    std::span<const std::uint32_t> indices;
  } state{&run, indices};
  Bulk(
      run, indices.size(), sizeof(std::uint32_t),
      [](void* opaque, std::uint64_t first, std::uint64_t last,
         JobObservation& observation) {
        auto& state = *static_cast<State*>(opaque);
        for (auto i = first; i < last; ++i) {
          state.run->Check(i);
          const bool equal = state.indices[Size(i)] == i;
          ++observation.work.source_scalar_pairs;
          if (!equal) WireFail(*state.run, SurfaceCode::kWireValue);
        }
      },
      &state);
}
namespace {
Json NativeGltf(const OutputPayload& expected,
                std::span<const SurfaceMemberView> members,
                const SurfaceInspectLimits& limits, Decoded& decoded,
                SurfaceWireCounts& counts, Run& run) {
  const auto& caps = std::get<GltfSurfaceInspectLimits>(limits.delegates);
  std::optional<GlbContainer> native_container;
  if (expected.encoding == SurfaceEncoding::kGlb) {
    run.stage = SurfaceStage::kContainerRead;
    const auto ordinal = run.Enter(SurfaceDelegateKind::kGlbRead, {}, false);
    auto result = ReadGlbContainer(
        NamedMember(members, kMainGlbPath, run).bytes, caps.container,
        run.policy.container, run.execution, run.stop);
    run.facts.delegates.push_back(
        {run.stage,
         SurfaceDelegateKind::kGlbRead,
         ordinal,
         {},
         SurfaceGlbFacts{result.status, result.error_byte_offset,
                         result.workers_used}});
    if (result.status != GlbStatus::kSuccess || !result.container) {
      if (result.status == GlbStatus::kCanceled)
        Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kGlbDelegate);
      if (result.status == GlbStatus::kBudgetExceeded)
        Fail(SurfaceStatus::kBudgetExceeded, run.stage,
             SurfaceCode::kGlbDelegate);
      if (result.status == GlbStatus::kLimitExceeded)
        Fail(SurfaceStatus::kLimitExceeded, run.stage,
             SurfaceCode::kGlbDelegate);
      Fail(SurfaceStatus::kVerificationFailed, run.stage,
           SurfaceCode::kGlbDelegate);
    }
    native_container.emplace(std::move(result.container));
    run.Check();
  }
  run.stage = SurfaceStage::kAssetRead;
  auto wire = OpenWire(expected, members, run, false);
  // The actual native GLB parser's chunk views feed the actual glTF reader.
  if (native_container) {
    if (native_container->ChunkCount() != (wire.binary.empty() ? 1 : 2))
      WireFail(run);
    const auto json = native_container->Chunk(0);
    if (json.type != kGlbJsonChunkType ||
        json.bytes.size() != wire.json_bytes.size())
      WireFail(run);
    CompareNativeBytes(json.bytes, wire.json_bytes, run);
    wire.json_bytes = json.bytes;
    if (!wire.binary.empty()) {
      const auto binary = native_container->Chunk(1);
      if (binary.type != kGlbBinChunkType ||
          binary.bytes.size() != wire.binary.size())
        WireFail(run);
      CompareNativeBytes(binary.bytes, wire.binary, run);
      wire.binary = binary.bytes;
    }
  }
  Vector<SuppliedGltfResource> resources{
      gltf_detail::Allocator<SuppliedGltfResource>(run.allocation)};
  if (!wire.binary.empty())
    resources.push_back({GltfResourceKind::kBuffer, 0, wire.binary});
  if (expected.encoding == SurfaceEncoding::kGltfJson)
    for (const auto& image : expected.images)
      resources.push_back(
          {GltfResourceKind::kImage, image.wire,
           NamedMember(members, image.ordinary_path, run).bytes});
  const auto ordinal = run.Enter(SurfaceDelegateKind::kGltfRead, {}, false);
  (void)run.Profile(SurfaceDelegateKind::kGltfRead, ordinal, {}, {});
  auto read = ReadGltfAssetCapped(wire.json_bytes, resources, caps.read,
                                  run.policy.gltf, run.execution, run.stop);
  run.facts.delegates.push_back(
      {run.stage,
       SurfaceDelegateKind::kGltfRead,
       ordinal,
       {},
       SurfaceGltfFacts{read.diagnostic, read.asset
                                             ? read.asset->NewRequestedBytes()
                                             : std::nullopt}});
  if (read.diagnostic.status != GltfStatus::kAccepted || !read.asset)
    GltfFailure(read.diagnostic, run);
  run.Check();
  counts.primitives = read.asset->PrimitiveCount();
  run.Bound(counts.primitives, run.limits.max_wire_primitives);
  const auto& native_document =
      gltf_detail::Access::State(*read.asset).document;
  for (auto entry : {std::pair{kMaterials, &counts.materials},
                     std::pair{kTextures, &counts.textures},
                     std::pair{kImages, &counts.images},
                     std::pair{kSamplers, &counts.samplers}})
    if (const auto table = Field(native_document, 0, entry.first, run))
      *entry.second = Count(native_document, *table, run);
  for (const auto count :
       {counts.materials, counts.textures, counts.images, counts.samplers})
    run.Bound(count, run.limits.max_material_bindings);
  run.Bound(counts.images, run.limits.max_resources);
  std::uint64_t image_bytes = 0;
  for (const auto& resource : read.asset->Resources()) {
    run.Check();
    if (resource.kind == GltfResourceKind::kImage) {
      image_bytes = AddCount(image_bytes, resource.bytes.size());
      run.Bound(image_bytes, run.limits.max_resource_bytes);
    }
  }
  wire.json.emplace(native_document);
  ValidateWire(expected, wire, run, &decoded);
  if (counts.primitives != expected.primitives.size() ||
      read.asset->InstanceCount() != expected.primitives.size())
    WireFail(run);
  for (std::uint64_t p = 0; p < expected.primitives.size(); ++p) {
    run.stage = SurfaceStage::kPrimitiveRead;
    run.Check(p);
    const auto primitive = read.asset->Primitive(p);
    if (!primitive || primitive->MeshIndex() != p ||
        primitive->PrimitiveIndex() != 0)
      WireFail(run);
    const auto call = run.Enter(SurfaceDelegateKind::kGltfExtract, p, false);
    (void)run.Profile(SurfaceDelegateKind::kGltfExtract, call, p, {});
    auto extraction = ExtractGltfPrimitiveCapped(
        *read.asset, *primitive,
        {GltfTriangleRequest::kRequireTriangles, GltfValueRequest::kRaw,
         GltfProcessingProfile::kStaticLocalTrianglesV1},
        caps.extraction, run.stop);
    run.facts.delegates.push_back(
        {run.stage, SurfaceDelegateKind::kGltfExtract, call, p,
         SurfaceGltfFacts{
             extraction.diagnostic,
             extraction.extraction
                 ? std::optional<std::uint64_t>{extraction.extraction
                                                    ->NewRequestedBytes()}
                 : std::nullopt}});
    if (extraction.diagnostic.status != GltfStatus::kAccepted ||
        !extraction.extraction)
      GltfFailure(extraction.diagnostic, run);
    const auto& native = *extraction.extraction;
    const auto& planned = expected.primitives[Size(p)];
    if (native.ProcessingProfile() !=
            GltfProcessingProfile::kStaticLocalTrianglesV1 ||
        native.SourceMode() != kTrianglesMode ||
        native.MaterialIndex() != planned.view.material ||
        native.Channels().size() != AddCount(expected.uv.size(), 1) ||
        native.TriangleIndices().size() != planned.view.wire_vertices ||
        native.SourceCorners().size() != planned.view.wire_vertices)
      WireFail(run);
    counts.corners = AddCount(counts.corners, native.SourceCorners().size());
    run.Bound(counts.corners, run.limits.max_wire_corners);
    counts.triangles = AddCount(
        counts.triangles, native.TriangleIndices().size() / kTriangleCorners);
    for (std::uint64_t i = 0; i < planned.view.wire_vertices; ++i) {
      run.Check(i);
      Relationship(run);
      if (native.TriangleIndices()[Size(i)] != i ||
          native.SourceCorners()[Size(i)].accessor_row != i ||
          native.SourceCorners()[Size(i)].primitive_stream_corner != i)
        WireFail(run, SurfaceCode::kWireMap);
    }
    const auto index = native.SourceIndices();
    if (!index || index->component_type != GltfComponentType::kUint32 ||
        index->shape != GltfAccessorShape::kScalar || index->normalized ||
        index->sparse_source ||
        index->accessor_index != *planned.index_accessor)
      WireFail(run);
    const auto* indices =
        std::get_if<std::span<const std::uint32_t>>(&index->raw);
    if (!indices || indices->size() != planned.view.wire_vertices)
      WireFail(run);
    (void)ReadBlock(wire, *planned.index_accessor, run);
    CompareNativeIdentity(*indices, run);
    bool position_seen = false;
    Vector<std::uint8_t> uv_seen{
        gltf_detail::Allocator<std::uint8_t>(run.allocation)};
    uv_seen.resize(expected.uv.size(), 0);
    for (const auto& channel : native.Channels()) {
      if (channel.semantic == kPosition) {
        if (position_seen || channel.set_index ||
            channel.accessor_index != *planned.position_accessor)
          WireFail(run);
        position_seen = true;
        counts.vertices = AddCount(counts.vertices, channel.element_count);
        run.Bound(counts.vertices, run.limits.max_wire_vertices);
        decoded.BeginPosition();
        const auto actual = NativeValues(
            channel, wire.accessors[Size(channel.accessor_index)], wire, run);
        decoded.Position(planned.view.first_wire_vertex, actual);
      } else {
        if (!channel.set_index || *channel.set_index >= expected.uv.size())
          WireFail(run);
        const auto field = Size(*channel.set_index);
        String name{gltf_detail::Allocator<char>(run.allocation)};
        name.assign(kUvPrefix);
        AppendUnsigned(name, field);
        if (channel.semantic != name || uv_seen[field] ||
            channel.accessor_index != planned.uv_accessors[field])
          WireFail(run);
        uv_seen[field] = 1;
        decoded.BeginUv(field);
        const auto actual = NativeValues(
            channel, wire.accessors[Size(channel.accessor_index)], wire, run);
        decoded.Uv(field, planned.view.first_wire_vertex, actual);
      }
    }
    if (!position_seen || std::find(uv_seen.begin(), uv_seen.end(),
                                    std::uint8_t{0}) != uv_seen.end())
      WireFail(run);
    // This real extraction owner is released here, before the next capped call.
  }
  run.stage = SurfaceStage::kAssetRead;
  CompareImageMembers(expected, members, run);
  for (const auto& image : expected.images) {
    const GltfResourceView* selected = nullptr;
    for (const auto& resource : read.asset->Resources())
      if (resource.kind == GltfResourceKind::kImage &&
          resource.table_index == image.wire) {
        if (selected) WireFail(run, SurfaceCode::kResourceBytes);
        selected = &resource;
      }
    if (!selected || selected->bytes.size() != image.bytes.size())
      WireFail(run, SurfaceCode::kResourceBytes);
    for (std::size_t i = 0; i < image.bytes.size(); ++i) {
      run.Check(i);
      const bool equal = selected->bytes[i] == image.bytes[i];
      run.Pair();
      if (!equal)
        WireFail(run, SurfaceCode::kResourceBytes, SurfaceSubject::kImage,
                 image.wire);
    }
  }
  // This const shared snapshot retains the actual native parsed backing and
  // its delegated charged allocation context until Compare. No factor collector
  // has run yet, and native read/extraction return observations stay unchanged.
  return gltf_detail::Access::State(*read.asset).document;
}
void NativeStl(const OutputPayload& expected,
               std::span<const SurfaceMemberView> members,
               const SurfaceInspectLimits& limits, Decoded& decoded,
               SurfaceWireCounts& counts, Run& run) {
  run.stage = SurfaceStage::kAssetRead;
  const auto& member = NamedMember(members, kMainStlPath, run);
  const auto ordinal = run.Enter(SurfaceDelegateKind::kStlRead, {}, false);
  if (expected.encoding == SurfaceEncoding::kStlAscii)
    (void)run.Profile(SurfaceDelegateKind::kStlRead, ordinal, {}, {});
  auto read = ReadStl(member.bytes,
                      expected.encoding == SurfaceEncoding::kStlAscii
                          ? StlEncoding::kAscii
                          : StlEncoding::kBinary,
                      std::get<StlSurfaceInspectLimits>(limits.delegates).codec,
                      run.policy.stl, run.execution, run.stop);
  SurfaceStlFacts facts{read.status, read.diagnostic, read.execution, {}, {}};
  if (read.document)
    facts.requested_bytes_at_return = read.document->NewRequestedBytes();
  run.facts.delegates.push_back(
      {run.stage, SurfaceDelegateKind::kStlRead, ordinal, {}, facts});
  if (read.status != StlStatus::kSuccess || !read.document) {
    if (read.status == StlStatus::kCanceled)
      Fail(SurfaceStatus::kCanceled, run.stage, SurfaceCode::kStlDelegate);
    if (read.status == StlStatus::kBudgetExceeded)
      Fail(SurfaceStatus::kBudgetExceeded, run.stage,
           SurfaceCode::kStlDelegate);
    if (read.status == StlStatus::kLimitExceeded)
      Fail(SurfaceStatus::kLimitExceeded, run.stage, SurfaceCode::kStlDelegate);
    Fail(SurfaceStatus::kVerificationFailed, run.stage,
         SurfaceCode::kStlDelegate);
  }
  run.Check();
  const auto mesh = read.document->Mesh().View();
  if (!read.document->Inspection().IsStorageValid()) WireFail(run);
  counts.vertices = mesh.positions.size() / kPositionComponents;
  counts.corners = mesh.corner_vertices.size();
  counts.triangles = mesh.face_offsets.size() - 1;
  counts.primitives = counts.triangles ? 1 : 0;
  run.Bound(counts.primitives, run.limits.max_wire_primitives);
  run.Bound(counts.vertices, run.limits.max_wire_vertices);
  run.Bound(counts.corners, run.limits.max_wire_corners);
  if (mesh.positions.size() !=
          Product(expected.vertex_sources.size(), kPositionComponents) ||
      mesh.corner_vertices.size() != expected.vertex_sources.size() ||
      mesh.face_offsets.size() != AddCount(expected.triangle_sources.size(), 1))
    WireFail(run);
  for (std::uint64_t i = 0; i < mesh.face_offsets.size(); ++i) {
    run.Check(i);
    if (mesh.face_offsets[Size(i)] != Product(i, kTriangleCorners))
      WireFail(run, SurfaceCode::kWireMap);
  }
  if (expected.encoding == SurfaceEncoding::kStlAscii) {
    if (read.document->AsciiName() != expected.ascii_name ||
        !read.document->BinaryHeader().empty())
      WireFail(run, SurfaceCode::kWireValue);
  } else {
    const auto header = read.document->BinaryHeader();
    if (header.size() != expected.binary_header.size() ||
        !read.document->AsciiName().empty())
      WireFail(run);
    for (std::size_t i = 0; i < header.size(); ++i) {
      run.Check(i);
      if (header[i] != expected.binary_header[i])
        WireFail(run, SurfaceCode::kWireValue);
    }
  }
  ReadStlFields(expected, member.bytes, decoded, run, &*read.document);
}
bool SameSummary(const SurfaceNumericSummary& a,
                 const SurfaceNumericSummary& b) {
  return a.kind == b.kind && a.complete == b.complete &&
         a.examined_values == b.examined_values &&
         a.changed_values == b.changed_values &&
         CompareError(a.max_absolute_error, b.max_absolute_error) == 0 &&
         a.source_attribute == b.source_attribute &&
         a.source_row == b.source_row && a.component == b.component;
}
void CompareNumericClaims(const CapsuleNumericClaims& claims, Run& run) {
  if (claims.positions.has_value() != run.facts.positions.has_value() ||
      (claims.positions &&
       !SameSummary(*claims.positions, *run.facts.positions)) ||
      claims.uv.size() != run.facts.uv.size())
    WireFail(run, SurfaceCode::kCapsuleSource);
  for (std::size_t i = 0; i < claims.uv.size(); ++i) {
    run.Check(i);
    const SurfaceNumericSummary* actual = nullptr;
    for (const auto& field : run.facts.uv)
      if (field.source_attribute == claims.uv[i].source_attribute) {
        if (actual) WireFail(run, SurfaceCode::kCapsuleSource);
        actual = &field;
      }
    if (!actual || !SameSummary(claims.uv[i], *actual))
      WireFail(run, SurfaceCode::kCapsuleSource);
  }
}
SurfaceWireCounts ActualWireCounts(const OutputPayload& expected,
                                   std::span<const SurfaceMemberView> actual,
                                   SurfaceWireCounts counts) {
  // These real typed source-map extents have just been matched exhaustively
  // to actual captured capsule ranges (or the bare projection's actual rows).
  counts.map_entries = SourceMapEntries(expected);
  counts.members = actual.size();
  for (const auto& member : actual)
    counts.member_bytes = AddCount(counts.member_bytes, member.bytes.size());
  return counts;
}
SurfaceDiagnostic OwnAdmissionDiagnostic(gltf_detail::AdmissionCode code,
                                         SurfaceStage stage) {
  return {stage,
          code == gltf_detail::AdmissionCode::kBudgetExceeded
              ? SurfaceCode::kRequestBudget
          : code == gltf_detail::AdmissionCode::kCanceled
              ? SurfaceCode::kStopRequested
          : code == gltf_detail::AdmissionCode::kSizeLimit
              ? SurfaceCode::kSizeOverflow
              : SurfaceCode::kInvalidSourceStorage,
          SurfaceSubject::kNone,
          {},
          {},
          {}};
}
SurfaceStatus OwnAdmissionStatus(gltf_detail::AdmissionCode code) {
  switch (code) {
    case gltf_detail::AdmissionCode::kBudgetExceeded:
      return SurfaceStatus::kBudgetExceeded;
    case gltf_detail::AdmissionCode::kCanceled:
      return SurfaceStatus::kCanceled;
    case gltf_detail::AdmissionCode::kSizeLimit:
      return SurfaceStatus::kLimitExceeded;
    case gltf_detail::AdmissionCode::kInvalidObject:
      return SurfaceStatus::kInvalidStorage;
  }
  return SurfaceStatus::kInvalidStorage;
}
}  // namespace
SurfaceInspectionResult InspectForTest(
    const SurfaceOutput& expected,
    std::span<const SuppliedSurfaceMember> members,
    const SurfaceInspectLimits& limits, const SurfaceWorkPolicy& work,
    std::stop_token stop, const TestOptions& test) {
  SurfaceInspectionResult result;
  std::shared_ptr<InspectionState> state;
  auto reached = SurfaceStage::kConfiguration;
  try {
    ValidateInspectConfiguration(expected.Encoding(), limits, work);
    reached = SurfaceStage::kSource;
    if (SurfaceNumericSupport() != SurfaceNumericProfile::kIeeeBinary32Binary64)
      Fail(SurfaceStatus::kUnsupported, reached, SurfaceCode::kNumericProfile);
    const auto context = expected.Original().Source().ShareExecutionContext();
    if (!expected.Original().IsStorageValid() ||
        !expected.Triangles().Inspection().IsStorageValid())
      Fail(SurfaceStatus::kInvalidStorage, reached,
           SurfaceCode::kInvalidSourceStorage);
    if (!geometry::SharesExecutionBudget(
            context, expected.Triangles().ShareExecutionContext()))
      Fail(SurfaceStatus::kInvalidStorage, reached,
           SurfaceCode::kForeignLedger);
    if (stop.stop_requested())
      Fail(SurfaceStatus::kCanceled, reached, SurfaceCode::kStopRequested);
    const gltf_detail::AllocationContext root(context, stop, test.allocation);
    const auto allocation =
        root.WithRequestLimit(Size(limits.max_new_requested_bytes));
    state = std::allocate_shared<InspectionState>(
        gltf_detail::Allocator<InspectionState>(allocation), allocation);
    state->payload.emplace(allocation, expected);
    Run run(context, allocation, limits.counts, work, state->facts, stop, test);
    try {
      const auto& output = *Access::State(expected).payload;
      state->facts.original =
          MeshCounts(expected.Original().Source().View(), run);
      state->facts.triangles = MeshCounts(expected.Triangles().Mesh().View(),
                                          run, MeshRole::kTriangulated);
      AdmitValidationExtents(output, run);
      CaptureMembers(output, members, *state->payload, run);
      Decoded decoded(output, run);
      SurfaceWireCounts actual_wire;
      std::optional<Json> actual_document;
      if (output.encoding == SurfaceEncoding::kGltfJson ||
          output.encoding == SurfaceEncoding::kGlb)
        actual_document.emplace(NativeGltf(output, state->payload->members,
                                           limits, decoded, actual_wire, run));
      else
        NativeStl(output, state->payload->members, limits, decoded, actual_wire,
                  run);
      std::optional<CapsuleNumericClaims> claims;
      if (output.representation == SurfaceRepresentation::kAnnotatedBundleV1) {
        run.stage = SurfaceStage::kCapsuleRead;
        claims.emplace(InspectCapsule(output, state->payload->members, run));
      }
      run.stage = SurfaceStage::kCompare;
      if (actual_document) (void)WireFactors(output, *actual_document, run);
      decoded.Finish();
      if (claims) CompareNumericClaims(*claims, run);
      for (std::size_t i = 0; i < output.members.size(); ++i) {
        run.Check(i);
        const auto& actual = state->payload->members[i];
        const auto& original = output.members[i];
        if (actual.path != original.path ||
            actual.bytes.size() != original.bytes.size() ||
            actual.sha256 != original.sha256)
          WireFail(run, SurfaceCode::kMemberDigest, SurfaceSubject::kMember, i);
      }
      actual_wire =
          ActualWireCounts(output, state->payload->members, actual_wire);
      run.Bound(actual_wire.map_entries, run.limits.max_map_entries,
                SurfaceCode::kMapLimit);
      state->facts.wire = actual_wire;
      run.Check();
      result.status = SurfaceStatus::kSuccess;
      result.diagnostic = {SurfaceStage::kComplete,
                           SurfaceCode::kNone,
                           SurfaceSubject::kNone,
                           {},
                           {},
                           {}};
      result.inspection = Access::Inspection(state);
      result.facts = Access::Facts(state);
      return result;
    } catch (...) {
      reached = run.stage;
      throw;
    }
  } catch (const Failure& failure) {
    result.status = failure.Status();
    result.diagnostic = failure.Diagnostic();
    if (result.status == SurfaceStatus::kLimitExceeded &&
        result.diagnostic.code == SurfaceCode::kCountLimit &&
        result.diagnostic.stage == SurfaceStage::kNumeric)
      result.diagnostic.stage = reached;
  } catch (const gltf_detail::AdmissionFailure& failure) {
    const auto code = SurfaceAdmissionCode(failure);
    result.status = OwnAdmissionStatus(code);
    result.diagnostic = OwnAdmissionDiagnostic(code, reached);
  }
  if (state) {
    state->payload.reset();
    result.facts = Access::Facts(state);
  }
  return result;
}
}  // namespace meshvale::interchange::surface_detail
