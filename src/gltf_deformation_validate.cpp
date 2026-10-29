// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "gltf_accessor.h"
#include "gltf_accessor_source.h"
#include "gltf_allocation.h"
#include "gltf_asset_state.h"
#include "gltf_deformation_read.h"
#include "gltf_deformation_state.h"
#include "gltf_metadata.h"
#include "gltf_numerical_profile.h"
#include "gltf_operation_workers.h"
#include "gltf_quaternion.h"
#include "meshvale/geometry/execution.h"

namespace meshvale::interchange::gltf_deformation_detail {
namespace {
namespace gd = gltf_detail;
using Role = GltfDeformationAccessorRole;
using Shape = GltfAccessorShape;
using Type = GltfComponentType;
using Path = GltfDeformationAnimationPath;
using Interpolation = GltfDeformationInterpolation;
constexpr std::size_t kLanes = 4;
constexpr std::size_t kMatrixComponents = 16;
constexpr std::size_t kCubicGroups = 3;
constexpr std::size_t kDirectionComponents = 3;
constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kHexNibbleBits = 4;
constexpr unsigned kHexNibbleMask = 15;
constexpr std::uint64_t kDecimalRadix = 10;
constexpr std::uint64_t kMaximumRoleSourcePasses = 3;
constexpr std::uint64_t kMaximumRolePredicatePasses = 4;
constexpr std::uint32_t kExponent = 0x7f800000U;
constexpr std::uint32_t kMagnitude = 0x7fffffffU;
constexpr std::uint32_t kSign = 0x80000000U;
constexpr std::uint32_t kFloatOne = 0x3f800000U;
constexpr std::uint64_t kQuantizedOne = 65535;
constexpr std::uint64_t kByteToShort = 257;
// Influence bounds count real accessor lookup visits and source-byte reads.
// At most four linear lookups per lane plus two lookups per preceding pair;
// 16 bytes/pair and32 bytes/lane conservatively cover all uint32 wire reads.
constexpr std::uint64_t kInfluenceLinearLookupPasses = 4;
constexpr std::uint64_t kInfluencePairSourceBytes = 16;
constexpr std::uint64_t kInfluenceLaneSourceBytes = 32;
constexpr double kWeightEpsilon = 2e-7;
constexpr std::uint32_t kRoleRule = 311;
constexpr std::uint32_t kBoundsRule = 312;
constexpr std::uint32_t kLimitRule = 314;
constexpr std::string_view kMeshesField = "meshes";
constexpr std::string_view kPrimitivesField = "primitives";
constexpr std::string_view kAttributesField = "attributes";
constexpr std::string_view kTargetsField = "targets";
constexpr std::string_view kWeightsField = "weights";
constexpr std::string_view kJointsField = "joints";
constexpr std::string_view kSkinsField = "skins";
constexpr std::string_view kSkeletonField = "skeleton";
constexpr std::string_view kInverseBindMatricesField = "inverseBindMatrices";
constexpr std::string_view kNodesField = "nodes";
constexpr std::string_view kAnimationsField = "animations";
constexpr std::string_view kSamplersField = "samplers";
constexpr std::string_view kChannelsField = "channels";
constexpr std::string_view kSamplerField = "sampler";
constexpr std::string_view kTargetField = "target";
constexpr std::string_view kNodeField = "node";
constexpr std::string_view kPathField = "path";
constexpr std::string_view kTranslationPath = "translation";
constexpr std::string_view kRotationPath = "rotation";
constexpr std::string_view kScalePath = "scale";
constexpr std::string_view kInputField = "input";
constexpr std::string_view kOutputField = "output";
constexpr std::string_view kInterpolationField = "interpolation";
constexpr std::string_view kStepInterpolation = "STEP";
constexpr std::string_view kCubicInterpolation = "CUBICSPLINE";
constexpr std::string_view kLinearInterpolation = "LINEAR";
constexpr std::string_view kExtensionsField = "extensions";
constexpr std::string_view kExtensionsPathSuffix = "/extensions";
constexpr std::string_view kAccessorsField = "accessors";
constexpr std::string_view kMinimumField = "min";
constexpr std::string_view kMaximumField = "max";
constexpr std::string_view kNameRoot = "gltf/";
constexpr std::string_view kMorphNameSegment = "/morph/";
constexpr std::string_view kBaseNameSegment = "/base/";
constexpr std::string_view kNamePathSeparator = "/";
constexpr std::string_view kNameLengthSeparator = ":";
constexpr std::string_view kBaseNameKind = "base/";
constexpr std::string_view kMorphNameKind = "morph/";
constexpr std::string_view kDecimalMarkers = ".eE";
constexpr std::string_view kPosition = "POSITION";
constexpr std::string_view kNormal = "NORMAL";
constexpr std::string_view kTangent = "TANGENT";
constexpr std::string_view kJoints = "JOINTS_";
constexpr std::string_view kWeights = "WEIGHTS_";
constexpr std::string_view kTexcoord = "TEXCOORD_";
constexpr std::string_view kColor = "COLOR_";
constexpr std::string_view kIndices = "indices";
constexpr std::string_view kHex = "0123456789abcdef";
constexpr std::string_view kImagesField = "images";
constexpr std::string_view kSparseField = "sparse";
constexpr std::string_view kSparseValuesField = "values";
constexpr std::string_view kBufferViewField = "bufferView";
GltfDeformationPhase UsePhase(const GltfDeformationAccessorUseView& v) {
  if (v.role == Role::kInverseBindMatrices || v.role == Role::kJoints)
    return GltfDeformationPhase::kBindings;
  if (v.role == Role::kMorphDelta) return GltfDeformationPhase::kMorphs;
  return v.animation ? GltfDeformationPhase::kAnimations
                     : GltfDeformationPhase::kRoles;
}

std::uint64_t Add(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    gd::Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow,
             {}, kLimitRule);
  return a + b;
}
std::uint64_t Mul(std::uint64_t a, std::uint64_t b) {
  if (a && b > std::numeric_limits<std::uint64_t>::max() / a)
    gd::Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow,
             {}, kLimitRule);
  return a * b;
}
std::size_t Host(std::uint64_t a) {
  if (a >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    gd::Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow,
             {}, kLimitRule);
  return static_cast<std::size_t>(a);
}
void Limit(std::uint64_t value, std::uint64_t cap) {
  if (value > cap)
    gd::Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
}
void Malformed(GltfDiagnosticCode code = GltfDiagnosticCode::kBinaryValue) {
  gd::Fail(GltfStatus::kMalformed, code, {}, kRoleRule);
}
void Require(bool value) {
  if (!value) Malformed(GltfDiagnosticCode::kIndexRole);
}
std::optional<std::uint64_t> Set(std::string_view name,
                                 std::string_view prefix) {
  if (!name.starts_with(prefix)) return {};
  auto digits = name.substr(prefix.size());
  if (digits.empty() || (digits.size() != 1 && digits.front() == '0'))
    Malformed(GltfDiagnosticCode::kIndexRole);
  std::uint64_t result = 0;
  for (char digit : digits) {
    if (digit < '0' || digit > '9') Malformed(GltfDiagnosticCode::kIndexRole);
    result =
        Add(Mul(result, kDecimalRadix), static_cast<unsigned>(digit - '0'));
  }
  constexpr std::uint64_t kMaximumCoreSet = 999999999;
  Require(result <= kMaximumCoreSet);
  return result;
}
bool Floating(const gd::AccessorSource& s) {
  return s.component_type == Type::kFloat32 && !s.normalized;
}
bool UnsignedNormalized(const gd::AccessorSource& s) {
  return s.normalized && (s.component_type == Type::kUint8 ||
                          s.component_type == Type::kUint16);
}
bool Normalized(const gd::AccessorSource& s) {
  return UnsignedNormalized(s) ||
         (s.normalized && (s.component_type == Type::kInt8 ||
                           s.component_type == Type::kInt16));
}
std::size_t Width(const gd::AccessorSource& s) {
  auto l = gd::DescribeAccessorLayout(s.component_type, s.shape);
  return l.rows * l.columns;
}
std::size_t ScalarBytes(Type type) {
  switch (type) {
    case Type::kInt8:
    case Type::kUint8:
      return 1;
    case Type::kInt16:
    case Type::kUint16:
      return 2;
    case Type::kUint32:
    case Type::kFloat32:
      return 4;
  }
  throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
}
GltfDeformationRawValues Raw(const gd::OwnedRawValues& raw, bool words) {
  if (words) {
    auto data = std::span(std::get<gd::ChargedValues<std::uint32_t>>(raw));
    return GltfFloat32BitWords{std::as_bytes(data)};
  }
  return std::visit(
      [](const auto& values) -> GltfDeformationRawValues {
        using Value =
            typename std::remove_cvref_t<decltype(values)>::value_type;
        if constexpr (std::is_same_v<Value, float>)
          return GltfFloat32BitWords{std::as_bytes(std::span(values))};
        else
          return std::span<const Value>(values);
      },
      raw);
}
std::uint32_t Word(const AccessorStorage& storage, std::size_t scalar) {
  return std::visit(
      [&](const auto& values) -> std::uint32_t {
        using Value =
            typename std::remove_cvref_t<decltype(values)>::value_type;
        if constexpr (std::is_same_v<Value, float>)
          return std::bit_cast<std::uint32_t>(values[scalar]);
        else if constexpr (std::is_signed_v<Value>)
          return static_cast<std::make_unsigned_t<Value>>(values[scalar]);
        else
          return values[scalar];
      },
      storage.decoded.raw);
}
std::uint32_t ReadWord(const std::byte* data, std::size_t bytes) {
  std::uint32_t word = 0;
  for (std::size_t i = 0; i < bytes; ++i)
    word |= std::uint32_t(std::to_integer<unsigned char>(data[i]))
            << (i * kBitsPerByte);
  return word;
}
std::uint32_t Load(const AccessorStorage& storage, std::size_t scalar,
                   gd::QuaternionWork& work) {
  for (std::size_t b = 0; b < ScalarBytes(storage.source.component_type); ++b)
    work.ByteVisit();
  return Word(storage, scalar);
}
bool Finite(std::uint32_t word) { return (word & kExponent) != kExponent; }
double Value(std::uint32_t word, Type type, bool normalized) {
  switch (type) {
    case Type::kFloat32:
      return static_cast<double>(std::bit_cast<float>(word));
    case Type::kUint8:
      return normalized ? static_cast<double>(word) /
                              std::numeric_limits<std::uint8_t>::max()
                        : word;
    case Type::kUint16:
      return normalized ? static_cast<double>(word) /
                              std::numeric_limits<std::uint16_t>::max()
                        : word;
    case Type::kUint32:
      return word;
    case Type::kInt8: {
      auto v = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(word));
      return normalized
                 ? std::max(-1.0, static_cast<double>(v) /
                                      std::numeric_limits<std::int8_t>::max())
                 : v;
    }
    case Type::kInt16: {
      auto v = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word));
      return normalized
                 ? std::max(-1.0, static_cast<double>(v) /
                                      std::numeric_limits<std::int16_t>::max())
                 : v;
    }
  }
  throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
}
struct Primitive {
  std::uint64_t mesh, ordinal, rows, targets;
  std::size_t first_set, sets;
};
struct Use {
  GltfDeformationAccessorUseView view;
  std::size_t storage;
  // IBM constrains associated rows only; all other roles constrain every row.
  std::uint64_t constrained_rows;
};
enum class TaskKind { kRole, kInfluence, kBounds, kNode, kDerived, kDecode };
struct Task {
  TaskKind kind;
  std::size_t index;
  std::uint64_t first, count, scalar_bound, byte_bound;
};
struct Partial {
  std::shared_ptr<gd::QuaternionWorkspace> work_space;
  GltfDeformationObservation facts;
  std::exception_ptr error;
  std::optional<std::uint64_t> failed_unit;
  std::uint64_t copy_scalar_visits = 0;
  std::uint64_t integer_steps = 0, byte_visits = 0;
  bool entered = false, completed = false;
};
void ValidateDeclarations(const GltfExtractionRequest& request) {
  std::uint64_t bytes = 0;
  for (const auto& declaration : request.deformation_row_local)
    bytes = Add(bytes, declaration.name.size());
  Limit(bytes, request.deformation.limits.max_descriptor_bytes);
  for (std::size_t i = 0; i < request.deformation_row_local.size(); ++i) {
    auto name = request.deformation_row_local[i].name;
    const auto invalid = [] {
      gd::Fail(GltfStatus::kInvalidRequest,
               GltfDiagnosticCode::kInvalidRequest);
    };
    for (std::size_t j = 0; j < i; ++j)
      if (name == request.deformation_row_local[j].name) invalid();
    auto text = [&](std::string_view prefix) {
      if (!name.starts_with(prefix)) invalid();
      name.remove_prefix(prefix.size());
    };
    auto number = [&](char separator) {
      auto end = name.find(separator);
      if (end == std::string_view::npos || end == 0 ||
          (end > 1 && name[0] == '0'))
        invalid();
      std::uint64_t value = 0;
      for (auto c : name.substr(0, end)) {
        if (c < '0' || c > '9') invalid();
        value = Add(Mul(value, kDecimalRadix), static_cast<unsigned>(c - '0'));
      }
      name.remove_prefix(end + 1);
      return value;
    };
    text(kNameRoot);
    (void)number(kNamePathSeparator.front());
    (void)number(kNamePathSeparator.front());
    if (name.starts_with(kBaseNameKind))
      text(kBaseNameKind);
    else {
      text(kMorphNameKind);
      (void)number(kNamePathSeparator.front());
    }
    auto length = number(kNameLengthSeparator.front());
    if (Mul(length, 2) != name.size()) invalid();
    for (auto c : name)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) invalid();
  }
}

class Catalog {
 public:
  Catalog(State& state, const GltfExtractionRequest& request,
          const GltfExtractionLimits& limits,
          const gd::NativeExecutionOptions* execution)
      : s_(state),
        source_(gd::Access::State(state.asset)),
        d_(source_.document),
        request_(request),
        limits_(limits),
        execution_(execution),
        primitives_(gd::Allocator<Primitive>(s_.descriptors)),
        uses_(gd::Allocator<Use>(s_.descriptors)),
        tasks_(gd::Allocator<Task>(s_.descriptors)),
        role_tasks_(gd::Allocator<Task>(s_.descriptors)),
        workspaces_(gd::Allocator<std::shared_ptr<gd::QuaternionWorkspace>>(
            s_.descriptors)),
        selected_nodes_(gd::Allocator<std::uint8_t>(s_.descriptors)),
        mesh_targets_(gd::Allocator<std::uint64_t>(s_.descriptors)) {}
  void Build();
  void Decode();
  void Execute();
  void Freeze();

 private:
  void Relationship() {
    auto& n = s_.facts.counts.relationship_steps;
    n = Add(n, 1);
    Limit(n, s_.policy.limits.max_relationship_steps);
    if (n % s_.policy.work.max_relationship_block == 0)
      s_.allocation.Checkpoint();
  }
  std::uint64_t Count(std::string_view table) {
    auto rows = Member(d_, 0, table);
    return rows ? CountChildren(d_, *rows) : 0;
  }
  std::optional<std::size_t> Member(const gd::JsonSnapshot& d,
                                    std::size_t object, std::string_view key) {
    if (d.Kind(object) != gd::JsonKind::kObject) return {};
    for (auto n = d.FirstChild(object); n; n = d.NextSibling(*n)) {
      Relationship();
      auto actual = d.Key(*n);
      bool same = actual.size() == key.size();
      if (same)
        for (std::size_t i = 0; i < key.size(); ++i) {
          MetadataByte();
          if (actual[i] != key[i]) {
            same = false;
            break;
          }
        }
      if (same) return n;
    }
    return {};
  }
  std::uint64_t CountChildren(const gd::JsonSnapshot& d, std::size_t array) {
    std::uint64_t count = 0;
    for (auto n = d.FirstChild(array); n; n = d.NextSibling(*n)) {
      Relationship();
      count = Add(count, 1);
    }
    return count;
  }
  std::optional<std::size_t> Child(const gd::JsonSnapshot& d, std::size_t array,
                                   std::uint64_t ordinal) {
    for (auto n = d.FirstChild(array); n; n = d.NextSibling(*n)) {
      Relationship();
      if (ordinal == 0) return n;
      --ordinal;
    }
    return {};
  }
  std::size_t Required(const gd::JsonSnapshot& d, std::size_t object,
                       std::string_view key) {
    auto n = Member(d, object, key);
    if (!n) Malformed(GltfDiagnosticCode::kCoreFieldRequired);
    return *n;
  }
  std::size_t Row(const gd::JsonSnapshot& d, std::string_view table,
                  std::uint64_t index) {
    auto n = Child(d, Required(d, 0, table), index);
    if (!n) Malformed(GltfDiagnosticCode::kCoreReference);
    return *n;
  }
  std::optional<std::uint64_t> Index(const gd::JsonSnapshot& d, std::size_t row,
                                     std::string_view key) {
    auto n = Member(d, row, key);
    return n ? std::optional(gd::ReadExactUnsigned(d, *n)) : std::nullopt;
  }
  void MetadataByte() {
    metadata_bytes_ = Add(metadata_bytes_, 1);
    if (metadata_bytes_ % s_.policy.work.cancellation_block_bytes == 0)
      s_.allocation.Checkpoint();
  }
  gd::AccessorSource Resolve(std::uint64_t index, gd::AccessorRole role) {
    const gd::AccessorResolveWork work{
        [](void* state) { static_cast<Catalog*>(state)->Relationship(); },
        [](void* state) { static_cast<Catalog*>(state)->MetadataByte(); },
        this};
    return gd::ResolveAccessorSource(source_, index, role, &work);
  }
  std::size_t Storage(std::uint64_t accessor, gd::AccessorRole role);
  std::size_t AddUse(
      GltfDeformationAccessorUseView view,
      std::uint64_t constrained = std::numeric_limits<std::uint64_t>::max());
  void Meshes();
  void Skins();
  void Users();
  void Defaults(std::size_t row, GltfMorphWeightOwner owner,
                std::uint64_t index, std::uint64_t count);
  void Animations();
  void CheckSharedViews();
  void PlanRoles();
  bool Ancestor(std::uint64_t ancestor, std::uint64_t node);
  void MarkAncestors(std::uint64_t node);
  gd::OwnedVector<std::uint64_t> Ancestors(std::uint64_t node);
  void Declaration(const GltfDeformationAccessorUseView& view);
  void CheckShape(const Use& use);
  void RoleRow(const Use& use, std::uint64_t row, Partial& partial,
               gd::QuaternionWork& work);
  void InfluenceRow(const Primitive& primitive, std::uint64_t row,
                    Partial& partial, gd::QuaternionWork& work);
  void Bound(const AccessorStorage& storage, std::uint64_t component,
             Partial& partial, gd::QuaternionWork& work);
  void Node(std::size_t index, Partial& partial, gd::QuaternionWork& work);
  void Derived(std::size_t index, std::uint64_t row, Partial& partial,
               gd::QuaternionWork& work);
  void Scalar(Partial& partial, gd::QuaternionWork& work);
  void Reference(Partial& partial, gd::QuaternionWork& work) {
    auto global = relationships_.load(std::memory_order_relaxed);
    for (;;) {
      if (global >= s_.policy.limits.max_relationship_steps)
        gd::Fail(GltfStatus::kUnsupportedProfile,
                 GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
      if (relationships_.compare_exchange_weak(global, global + 1,
                                               std::memory_order_relaxed))
        break;
      work.Checkpoint();
    }
    auto& count = partial.facts.counts.relationship_steps;
    count = Add(count, 1);
    if (count % s_.policy.work.max_relationship_block == 0) work.Checkpoint();
  }
  void Gate(Partial& partial, std::uint64_t partition, bool caller);
  void RunTasks(std::uint64_t scalar_plan, std::uint64_t byte_plan,
                bool decode_phase);
  void CopyRow(std::size_t index, std::uint64_t row, Partial& partial,
               gd::QuaternionWork& work);
  void RunJob(std::size_t ordinal);
  static void Job(void* state, std::size_t ordinal) noexcept;
  State& s_;
  const gd::AssetState& source_;
  const gd::JsonSnapshot& d_;
  const GltfExtractionRequest& request_;
  const GltfExtractionLimits& limits_;
  const gd::NativeExecutionOptions* execution_;
  gd::OwnedVector<Primitive> primitives_;
  gd::OwnedVector<Use> uses_;
  gd::OwnedVector<Task> tasks_;
  gd::OwnedVector<Task> role_tasks_;
  gd::OwnedVector<std::shared_ptr<gd::QuaternionWorkspace>> workspaces_;
  gd::OwnedVector<std::uint8_t> selected_nodes_;
  gd::OwnedVector<std::uint64_t> mesh_targets_;
  std::span<Partial> partials_;
  std::uint64_t total_units_ = 0;
  bool caller_ = true;
  bool decode_phase_ = false;
  std::atomic<std::uint64_t> relationships_{0};
  std::uint64_t metadata_bytes_ = 0;
  std::uint64_t planned_keys_ = 0;
  std::uint64_t preparation_scalars_ = 0;
  std::uint64_t role_units_ = 0, role_scalar_plan_ = 0, role_byte_plan_ = 0;
};

std::size_t Catalog::Storage(std::uint64_t index, gd::AccessorRole role) {
  Relationship();
  for (std::size_t i = 0; i < s_.accessor_storage.size(); ++i) {
    Relationship();
    if (s_.accessor_storage[i].source.index != index) continue;
    auto& existing = s_.accessor_storage[i].source;
    if (existing.role != role) {
      if (existing.role == gd::AccessorRole::kIndices ||
          role == gd::AccessorRole::kIndices)
        Malformed(GltfDiagnosticCode::kIndexRole);
      if (role == gd::AccessorRole::kDeformationVertex) existing.role = role;
    }
    return i;
  }
  auto descriptor = Resolve(index, role);
  descriptor.retain_float_words = descriptor.component_type == Type::kFloat32;
  auto width = Width(descriptor);
  Limit(Mul(descriptor.count, width), s_.policy.limits.max_scalar_values);
  s_.accessor_storage.emplace_back(std::move(descriptor), s_.decoded);
  return s_.accessor_storage.size() - 1;
}
std::size_t Catalog::AddUse(GltfDeformationAccessorUseView view,
                            std::uint64_t constrained) {
  Relationship();
  Limit(Add(uses_.size(), 1), s_.policy.limits.max_role_edges);
  const bool vertex =
      view.role == Role::kBaseAttribute || view.role == Role::kJoints ||
      view.role == Role::kWeights || view.role == Role::kMorphDelta;
  auto role = vertex ? gd::AccessorRole::kDeformationVertex
                     : gd::AccessorRole::kGeneric;
  if (view.semantic == kIndices) role = gd::AccessorRole::kIndices;
  auto storage = Storage(view.accessor, role);
  Use use{view, storage,
          std::min(constrained,
                   std::uint64_t(s_.accessor_storage[storage].source.count))};
  CheckShape(use);
  uses_.push_back(use);
  s_.uses.push_back(view);
  return storage;
}
void Catalog::CheckShape(const Use& use) {
  const auto& a = s_.accessor_storage[use.storage].source;
  const auto& v = use.view;
  const bool generic = v.role == Role::kInverseBindMatrices || v.animation;
  if (generic && a.base)
    Require(a.base->stride == 0 && a.base->target == gd::ViewTarget::kAbsent);
  bool valid = true;
  switch (v.role) {
    case Role::kJoints:
      valid = !a.normalized &&
              (a.component_type == Type::kUint8 ||
               a.component_type == Type::kUint16) &&
              a.shape == Shape::kVec4;
      break;
    case Role::kWeights:
      valid = (Floating(a) || UnsignedNormalized(a)) && a.shape == Shape::kVec4;
      break;
    case Role::kInverseBindMatrices:
      valid = Floating(a) && a.shape == Shape::kMat4 &&
              a.count >= use.constrained_rows;
      break;
    case Role::kAnimationTimes:
      valid = Floating(a) && a.shape == Shape::kScalar;
      break;
    case Role::kTranslationValues:
    case Role::kScaleValues:
      valid = Floating(a) && a.shape == Shape::kVec3;
      break;
    case Role::kRotationValues:
      valid = (Floating(a) || Normalized(a)) && a.shape == Shape::kVec4;
      break;
    case Role::kMorphWeightValues:
      valid = (Floating(a) || Normalized(a)) && a.shape == Shape::kScalar;
      break;
    case Role::kBaseAttribute:
    case Role::kMorphDelta: {
      const bool morph = v.role == Role::kMorphDelta;
      if (v.semantic == kIndices)
        valid = !a.normalized && a.shape == Shape::kScalar &&
                (a.component_type == Type::kUint8 ||
                 a.component_type == Type::kUint16 ||
                 a.component_type == Type::kUint32);
      else if (v.semantic == kPosition || v.semantic == kNormal)
        valid = Floating(a) && a.shape == Shape::kVec3;
      else if (v.semantic == kTangent)
        valid = Floating(a) && a.shape == (morph ? Shape::kVec3 : Shape::kVec4);
      else if (Set(v.semantic, kTexcoord))
        valid =
            (Floating(a) || (morph ? Normalized(a) : UnsignedNormalized(a))) &&
            a.shape == Shape::kVec2;
      else if (Set(v.semantic, kColor))
        valid =
            (Floating(a) || (morph ? Normalized(a) : UnsignedNormalized(a))) &&
            (a.shape == Shape::kVec3 || a.shape == Shape::kVec4);
      else if (v.semantic.starts_with('_')) {
        valid = !(a.component_type == Type::kFloat32 && a.normalized) &&
                !(a.component_type == Type::kUint32 && a.normalized);
        Declaration(v);
      } else
        valid = false;
      break;
    }
  }
  Require(valid);
  if (v.semantic == kPosition || v.role == Role::kAnimationTimes) {
    auto row = Row(d_, kAccessorsField, a.index);
    Require(Member(d_, row, kMinimumField).has_value() &&
            Member(d_, row, kMaximumField).has_value());
  }
  if (a.sparse) Limit(a.sparse->count, s_.policy.limits.max_sparse_writes);
}

void Append(gd::OwnedVector<char>& out, std::string_view text) {
  out.insert(out.end(), text.begin(), text.end());
}
void AppendIndex(gd::OwnedVector<char>& out, std::uint64_t index) {
  std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1> digits{};
  auto result =
      std::to_chars(digits.data(), digits.data() + digits.size(), index);
  Append(out, std::string_view(digits.data(), static_cast<std::size_t>(
                                                  result.ptr - digits.data())));
}
std::uint64_t DecimalWidth(std::uint64_t value) noexcept {
  std::uint64_t digits = 1;
  while (value >= kDecimalRadix) {
    value /= kDecimalRadix;
    ++digits;
  }
  return digits;
}
void Catalog::Declaration(const GltfDeformationAccessorUseView& v) {
  auto extent = Add(kNameRoot.size(), DecimalWidth(*v.mesh));
  extent =
      Add(extent, Add(kNamePathSeparator.size(), DecimalWidth(*v.primitive)));
  extent = Add(extent, v.target ? Add(kMorphNameSegment.size(),
                                      Add(DecimalWidth(*v.target),
                                          kNamePathSeparator.size()))
                                : kBaseNameSegment.size());
  extent = Add(extent, Add(DecimalWidth(v.semantic.size()),
                           kNameLengthSeparator.size()));
  extent = Add(extent, Mul(v.semantic.size(), 2));
  const auto host_extent =
      Host(extent);  // Complete name checked before allocation.
  gd::OwnedVector<char> name{gd::Allocator<char>(s_.descriptors)};
  if (host_extent > name.max_size())
    gd::Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kSizeOverflow,
             {}, kLimitRule);
  name.reserve(host_extent);
  Append(name, kNameRoot);
  AppendIndex(name, *v.mesh);
  Append(name, kNamePathSeparator);
  AppendIndex(name, *v.primitive);
  if (v.target) {
    Append(name, kMorphNameSegment);
    AppendIndex(name, *v.target);
    Append(name, kNamePathSeparator);
  } else
    Append(name, kBaseNameSegment);
  AppendIndex(name, v.semantic.size());
  Append(name, kNameLengthSeparator);
  for (unsigned char byte : v.semantic) {
    s_.allocation.Checkpoint();
    name.push_back(kHex[byte >> kHexNibbleBits]);
    name.push_back(kHex[byte & kHexNibbleMask]);
  }
  auto expected = std::string_view(name.data(), name.size());
  bool found = false;
  for (const auto& declaration : s_.declarations) {
    Relationship();
    if (declaration.name == expected) found = true;
  }
  if (!found)
    gd::Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole,
             {}, kRoleRule);
}

void Catalog::Defaults(std::size_t row, GltfMorphWeightOwner owner,
                       std::uint64_t index, std::uint64_t count) {
  gd::OwnedVector<GltfRawNumberView> values{
      gd::Allocator<GltfRawNumberView>(s_.descriptors)};
  auto weights = Member(d_, row, kWeightsField);
  if (weights) {
    Require(CountChildren(d_, *weights) == count);
    for (auto n = d_.FirstChild(*weights); n; n = d_.NextSibling(*n)) {
      Relationship();
      auto token = d_.Number(*n);
      values.push_back(
          {{static_cast<std::uint64_t>(token.data() - d_.Original().data()),
            token.size()},
           token.find_first_of(kDecimalMarkers) == std::string_view::npos
               ? GltfJsonNumberKind::kIntegerSpelling
               : GltfJsonNumberKind::kDecimalSpelling,
           token});
    }
  }
  s_.defaults_storage.push_back(std::move(values));
  s_.defaults.push_back({owner, index, weights.has_value(), {}});
}

void Catalog::Meshes() {
  mesh_targets_.resize(Host(Count(kMeshesField)));
  for (std::uint64_t m = 0; m < mesh_targets_.size(); ++m) {
    auto mesh = Row(d_, kMeshesField, m);
    auto table = Required(d_, mesh, kPrimitivesField);
    std::uint64_t p = 0;
    for (auto primitive = d_.FirstChild(table); primitive;
         primitive = d_.NextSibling(*primitive), ++p) {
      Relationship();
      auto attributes = Required(d_, *primitive, kAttributesField);
      auto position =
          gd::ReadExactUnsigned(d_, Required(d_, attributes, kPosition));
      auto position_source =
          Resolve(position, gd::AccessorRole::kDeformationVertex);
      Primitive info{m, p, position_source.count, 0, s_.influences.size(), 0};
      std::uint64_t set_count = 0, max_set = 0;
      std::uint64_t uv_count = 0, uv_max = 0, color_count = 0, color_max = 0;
      for (auto a = d_.FirstChild(attributes); a; a = d_.NextSibling(*a)) {
        Relationship();
        auto semantic = d_.Key(*a);
        auto joints = Set(semantic, kJoints), weights = Set(semantic, kWeights);
        auto uv = Set(semantic, kTexcoord), color = Set(semantic, kColor);
        if (uv) {
          ++uv_count;
          uv_max = std::max(uv_max, *uv);
        }
        if (color) {
          ++color_count;
          color_max = std::max(color_max, *color);
        }
        Role role = joints    ? Role::kJoints
                    : weights ? Role::kWeights
                              : Role::kBaseAttribute;
        auto storage = AddUse({gd::ReadExactUnsigned(d_, *a),
                               role,
                               semantic,
                               m,
                               p,
                               {},
                               joints    ? joints
                               : weights ? weights
                               : uv      ? uv
                                         : color,
                               {},
                               {},
                               {},
                               {},
                               {}});
        Require(s_.accessor_storage[storage].source.count == info.rows);
        if (joints) {
          ++set_count;
          max_set = std::max(max_set, *joints);
        }
      }
      Limit(set_count, s_.policy.limits.max_influence_sets);
      Require(!uv_count || uv_max == uv_count - 1);
      Require(!color_count || color_max == color_count - 1);
      Limit(Mul(set_count, kLanes), s_.policy.limits.max_lanes_per_vertex);
      Require(!set_count || max_set == set_count - 1);
      for (std::uint64_t set = 0; set < set_count; ++set) {
        const GltfDeformationAccessorUseView* j = nullptr;
        const GltfDeformationAccessorUseView* w = nullptr;
        for (const auto& use : uses_) {
          Relationship();
          if (use.view.mesh != m || use.view.primitive != p ||
              use.view.target || use.view.set != set)
            continue;
          if (use.view.role == Role::kJoints) j = &use.view;
          if (use.view.role == Role::kWeights) w = &use.view;
        }
        Require(j && w);
        s_.influences.push_back({m, p, set, j->accessor, w->accessor});
      }
      for (const auto& use : uses_) {
        Relationship();
        if (use.view.mesh == m && use.view.primitive == p &&
            use.view.role == Role::kWeights)
          Require(use.view.set && *use.view.set < set_count);
      }
      info.sets = Host(set_count);
      s_.facts.counts.influence_sets =
          Add(s_.facts.counts.influence_sets, set_count);
      if (auto indices = Index(d_, *primitive, kIndices))
        AddUse({*indices,
                Role::kBaseAttribute,
                kIndices,
                m,
                p,
                {},
                {},
                {},
                {},
                {},
                {},
                {}});
      auto targets = Member(d_, *primitive, kTargetsField);
      auto first_channel = s_.morph_channels.size();
      if (targets) {
        info.targets = CountChildren(d_, *targets);
        Limit(info.targets, s_.policy.limits.max_morph_targets);
        mesh_targets_[Host(m)] = info.targets;
        std::uint64_t t = 0;
        for (auto target = d_.FirstChild(*targets); target;
             target = d_.NextSibling(*target), ++t)
          for (auto a = d_.FirstChild(*target); a; a = d_.NextSibling(*a)) {
            Relationship();
            auto semantic = d_.Key(*a);
            auto base = Member(d_, attributes, semantic);
            Require(base.has_value());
            auto delta = gd::ReadExactUnsigned(d_, *a);
            auto set = Set(semantic, kTexcoord);
            if (!set) set = Set(semantic, kColor);
            auto storage = AddUse({delta,
                                   Role::kMorphDelta,
                                   semantic,
                                   m,
                                   p,
                                   t,
                                   set,
                                   {},
                                   {},
                                   {},
                                   {},
                                   {}});
            Require(s_.accessor_storage[storage].source.count == info.rows);
            s_.morph_channels.push_back({m, p, t, semantic, set,
                                         gd::ReadExactUnsigned(d_, *base),
                                         delta});
            Limit(s_.morph_channels.size(),
                  s_.policy.limits.max_morph_channels);
          }
      }
      s_.morphs.push_back({m, p, targets.has_value(), info.targets, {}});
      (void)first_channel;
      s_.facts.counts.morph_targets =
          Add(s_.facts.counts.morph_targets, info.targets);
      primitives_.push_back(info);
    }
    Defaults(mesh, GltfMorphWeightOwner::kMesh, m, mesh_targets_[Host(m)]);
  }
  s_.facts.counts.morph_channels = s_.morph_channels.size();
}

gd::OwnedVector<std::uint64_t> Catalog::Ancestors(std::uint64_t node) {
  gd::OwnedVector<std::uint64_t> out{
      gd::Allocator<std::uint64_t>(s_.descriptors)};
  for (auto n = source_.nodes[Host(node)].parent; n;
       n = source_.nodes[Host(*n)].parent) {
    Relationship();
    out.push_back(*n);
  }
  return out;
}
bool Catalog::Ancestor(std::uint64_t ancestor, std::uint64_t node) {
  for (std::optional<std::uint64_t> n = node; n;
       n = source_.nodes[Host(*n)].parent) {
    Relationship();
    if (*n == ancestor) return true;
  }
  return false;
}
void Catalog::MarkAncestors(std::uint64_t node) {
  for (std::optional<std::uint64_t> n = node; n;
       n = source_.nodes[Host(*n)].parent) {
    Relationship();
    selected_nodes_[Host(*n)] = 1;
  }
}
void Catalog::Skins() {
  auto count = Count(kSkinsField);
  Limit(count, s_.policy.limits.max_skin_rows);
  s_.joint_storage.reserve(Host(count));
  s_.skins.reserve(Host(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    auto skin = Row(d_, kSkinsField, i);
    gd::OwnedVector<std::uint64_t> joints{
        gd::Allocator<std::uint64_t>(s_.descriptors)};
    auto list = Required(d_, skin, kJointsField);
    for (auto n = d_.FirstChild(list); n; n = d_.NextSibling(*n)) {
      Relationship();
      auto index = gd::ReadExactUnsigned(d_, *n);
      for (auto old : joints) {
        Relationship();
        Require(old != index);
      }
      joints.push_back(index);
      s_.facts.counts.joint_references =
          Add(s_.facts.counts.joint_references, 1);
      Limit(s_.facts.counts.joint_references,
            s_.policy.limits.max_joint_references);
    }
    auto inverse = Index(d_, skin, kInverseBindMatricesField);
    if (inverse)
      AddUse({*inverse,
              Role::kInverseBindMatrices,
              {},
              {},
              {},
              {},
              {},
              i,
              {},
              {},
              {},
              {}},
             joints.size());
    s_.skins.push_back({i, {}, Index(d_, skin, kSkeletonField), inverse});
    s_.joint_storage.push_back(std::move(joints));
  }
  s_.facts.counts.skin_rows = count;
}
void Catalog::Users() {
  const auto* selected = gd::Access::PrimitiveRecordFor(s_.asset, s_.primitive);
  s_.facts.counts.node_rows = source_.nodes.size();
  Limit(source_.nodes.size(), s_.policy.limits.max_node_rows);
  selected_nodes_.resize(source_.nodes.size());
  for (const auto& node : source_.nodes) {
    Relationship();
    if (!node.mesh) continue;
    auto row = Row(d_, kNodesField, node.table_index);
    Defaults(row, GltfMorphWeightOwner::kNode, node.table_index,
             mesh_targets_[Host(*node.mesh)]);
    for (const auto& primitive : primitives_) {
      Relationship();
      if (primitive.mesh != *node.mesh) continue;
      bool reachable = false;
      for (const auto& scene : source_.scenes)
        for (auto root : scene.roots)
          if (Ancestor(root, node.table_index)) reachable = true;
      auto ancestors = Ancestors(node.table_index);
      gd::OwnedVector<std::uint64_t> joint_ancestors{
          gd::Allocator<std::uint64_t>(s_.descriptors)};
      const bool instantiated = node.skin.has_value() && primitive.sets != 0;
      if (instantiated)
        for (auto joint : s_.joint_storage[Host(*node.skin)]) {
          Relationship();
          auto a = Ancestors(joint);
          for (auto n : a) {
            bool present = false;
            for (auto old : joint_ancestors) {
              Relationship();
              if (old == n) present = true;
            }
            if (!present) joint_ancestors.push_back(n);
          }
        }
      s_.ancestors.push_back(std::move(ancestors));
      s_.joint_ancestors.push_back(std::move(joint_ancestors));
      s_.users.push_back({node.table_index,
                          *node.mesh,
                          primitive.ordinal,
                          node.skin,
                          instantiated,
                          reachable,
                          {},
                          {}});
      Limit(s_.users.size(), s_.policy.limits.max_mesh_users);
      if (*node.mesh == selected->mesh_index &&
          primitive.ordinal == selected->primitive_index) {
        MarkAncestors(node.table_index);
        if (instantiated)
          for (auto joint : s_.joint_storage[Host(*node.skin)])
            MarkAncestors(joint);
      }
    }
  }
  s_.facts.counts.mesh_users = s_.users.size();
}

void Catalog::Animations() {
  const auto* selected = gd::Access::PrimitiveRecordFor(s_.asset, s_.primitive);
  bool selected_has_targets = false;
  for (const auto& primitive : primitives_) {
    Relationship();
    if (primitive.mesh == selected->mesh_index &&
        primitive.ordinal == selected->primitive_index)
      selected_has_targets = primitive.targets != 0;
  }
  std::uint64_t samplers = 0;
  for (std::uint64_t a = 0; a < Count(kAnimationsField); ++a) {
    auto animation = Row(d_, kAnimationsField, a);
    auto table = Required(d_, animation, kSamplersField);
    samplers = Add(samplers, CountChildren(d_, table));
    Limit(samplers, s_.policy.limits.max_samplers);
    std::uint64_t sampler_ordinal = 0;
    for (auto sampler = d_.FirstChild(table); sampler;
         sampler = d_.NextSibling(*sampler), ++sampler_ordinal) {
      Relationship();
      auto input =
          gd::ReadExactUnsigned(d_, Required(d_, *sampler, kInputField));
      auto storage = AddUse({input,
                             Role::kAnimationTimes,
                             {},
                             {},
                             {},
                             {},
                             {},
                             {},
                             a,
                             sampler_ordinal,
                             {},
                             {}});
      planned_keys_ =
          Add(planned_keys_, s_.accessor_storage[storage].source.count);
      Limit(planned_keys_, s_.policy.limits.max_keys);
      if (auto interpolation = Member(d_, *sampler, kInterpolationField))
        if (d_.String(*interpolation) == kCubicInterpolation)
          Require(s_.accessor_storage[storage].source.count >= 2);
    }
    auto channels = Required(d_, animation, kChannelsField);
    std::uint64_t c = 0;
    for (auto channel = d_.FirstChild(channels); channel;
         channel = d_.NextSibling(*channel), ++c) {
      Relationship();
      auto sampler_index =
          gd::ReadExactUnsigned(d_, Required(d_, *channel, kSamplerField));
      auto sampler = Child(d_, table, sampler_index);
      Require(sampler.has_value());
      auto target = Required(d_, *channel, kTargetField);
      auto node = Index(d_, target, kNodeField);
      auto path_text = d_.String(Required(d_, target, kPathField));
      Path path;
      Role role;
      if (path_text == kTranslationPath) {
        path = Path::kTranslation;
        role = Role::kTranslationValues;
      } else if (path_text == kRotationPath) {
        path = Path::kRotation;
        role = Role::kRotationValues;
      } else if (path_text == kScalePath) {
        path = Path::kScale;
        role = Role::kScaleValues;
      } else if (path_text == kWeightsField) {
        path = Path::kWeights;
        role = Role::kMorphWeightValues;
      } else {
        gd::Fail(GltfStatus::kUnsupportedProfile,
                 GltfDiagnosticCode::kIndexRole);
      }
      for (const auto& old : s_.animations) {
        Relationship();
        Require(!node || old.animation != a || old.node != node ||
                old.path != path);
      }
      auto interp = Interpolation::kLinear;
      if (auto v = Member(d_, *sampler, kInterpolationField)) {
        auto text = d_.String(*v);
        if (text == kStepInterpolation)
          interp = Interpolation::kStep;
        else if (text == kCubicInterpolation)
          interp = Interpolation::kCubicSpline;
        else
          Require(text == kLinearInterpolation);
      }
      auto input =
          gd::ReadExactUnsigned(d_, Required(d_, *sampler, kInputField));
      auto output =
          gd::ReadExactUnsigned(d_, Required(d_, *sampler, kOutputField));
      auto times = Storage(input, gd::AccessorRole::kGeneric);
      auto values = AddUse(
          {output, role, {}, {}, {}, {}, {}, {}, a, sampler_index, c, node});
      auto keys = s_.accessor_storage[times].source.count;
      auto per_key = std::uint64_t{1};
      if (path == Path::kWeights) {
        per_key = 0;  // Ignored unbound channel has no mesh/grouping authority.
        if (node) {
          Require(source_.nodes[Host(*node)].mesh.has_value());
          per_key = mesh_targets_[Host(*source_.nodes[Host(*node)].mesh)];
          Require(per_key != 0);
        } else if (auto extension = Member(d_, target, kExtensionsField)) {
          if (CountChildren(d_, *extension))
            gd::Fail(GltfStatus::kUnsupportedProfile,
                     GltfDiagnosticCode::kIndexRole);
        }
      }
      if (node && path != Path::kWeights)
        Require(source_.nodes[Host(*node)].transform_kind !=
                GltfTransformKind::kMatrix);
      const auto factor =
          interp == Interpolation::kCubicSpline ? kCubicGroups : 1;
      if (path != Path::kWeights || node)
        Require(s_.accessor_storage[values].source.count ==
                Mul(Mul(keys, per_key), factor));
      Require(interp != Interpolation::kCubicSpline || keys >= 2);
      const bool affects = node && (path == Path::kWeights
                                        ? selected_has_targets &&
                                              source_.nodes[Host(*node)].mesh ==
                                                  selected->mesh_index
                                        : selected_nodes_[Host(*node)] != 0);
      s_.animations.push_back({a, c, sampler_index, node, path, interp, input,
                               output, keys, per_key, affects});
      Limit(s_.animations.size(), s_.policy.limits.max_animation_channels);
    }
  }
  s_.facts.counts.distinct_samplers = samplers;
  s_.facts.counts.animation_channels = s_.animations.size();
}

void Catalog::CheckSharedViews() {
  // Pinned1114 forbids an entire image view from serving any accessor
  // region, including sparse selectors/values and unreferenced declarations.
  const auto image_count = Count(kImagesField);
  const auto accessor_count = image_count ? Count(kAccessorsField) : 0;
  for (std::uint64_t image = 0; image < image_count; ++image) {
    Relationship();
    const auto image_view =
        Index(d_, Row(d_, kImagesField, image), kBufferViewField);
    if (!image_view) continue;
    for (std::uint64_t accessor = 0; accessor < accessor_count; ++accessor) {
      Relationship();
      const auto row = Row(d_, kAccessorsField, accessor);
      auto exclude = [&](std::size_t region) {
        Relationship();
        if (Index(d_, region, kBufferViewField) == image_view)
          gd::Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout,
                   {}, kRoleRule);
      };
      exclude(row);
      if (const auto sparse = Member(d_, row, kSparseField))
        for (auto field : {kIndices, kSparseValuesField}) {
          Relationship();
          if (const auto region = Member(d_, *sparse, field)) exclude(*region);
        }
    }
  }
  // Pinned core1112/1352: classify actual direct uses, not caller role flags.
  // Reuse of the same accessor within one kind remains legal. Distinct vertex
  // accessors sharing a view require an authored stride even with FLOAT words.
  enum class Kind { kVertex, kIndices, kInverseBind, kAnimation };
  gd::OwnedVector<Kind> kinds{gd::Allocator<Kind>(s_.descriptors)};
  kinds.reserve(s_.accessor_storage.size());
  for (std::size_t i = 0; i < s_.accessor_storage.size(); ++i) {
    std::optional<Kind> kind;
    for (const auto& use : uses_) {
      Relationship();
      if (use.storage != i) continue;
      auto next = use.view.semantic == kIndices ? Kind::kIndices
                  : use.view.role == Role::kInverseBindMatrices
                      ? Kind::kInverseBind
                  : use.view.animation ? Kind::kAnimation
                                       : Kind::kVertex;
      if (kind && kind != next)
        gd::Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout, {},
                 kRoleRule);
      kind = next;
    }
    if (!kind) throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
    kinds.push_back(*kind);
  }
  for (std::size_t i = 0; i < s_.accessor_storage.size(); ++i) {
    const auto& a = s_.accessor_storage[i].source;
    if (!a.base) continue;
    for (std::size_t j = 0; j < i; ++j) {
      Relationship();
      const auto& b = s_.accessor_storage[j].source;
      if (!b.base || a.base->index != b.base->index) continue;
      if (kinds[i] != kinds[j] ||
          (kinds[i] == Kind::kVertex && !a.base->stride))
        gd::Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryLayout, {},
                 kRoleRule);
    }
  }
}

void Catalog::Build() {
  s_.facts.phase = GltfDeformationPhase::kDeclarations;
  // Names are copied into independent charged backing before public views
  // exist.
  s_.declaration_names.reserve(request_.deformation_row_local.size());
  s_.declarations.reserve(request_.deformation_row_local.size());
  for (const auto& declaration : request_.deformation_row_local) {
    s_.allocation.Checkpoint();
    gd::OwnedVector<char> name{gd::Allocator<char>(s_.descriptors)};
    name.reserve(declaration.name.size());
    for (char byte : declaration.name) {
      s_.allocation.Checkpoint();
      name.push_back(byte);
    }
    s_.declaration_names.push_back(std::move(name));
  }
  for (const auto& name : s_.declaration_names)
    s_.declarations.push_back({{name.data(), name.size()}});
  Meshes();
  Skins();
  Users();
  // No extension role decoder is owned by this core profile. Preserve the
  // existing conservative optional-effect boundary rather than upgrading an
  // opaque object into a typed source proof. Unrelated extras are not touched.
  for (std::size_t node = 0; node < source_.guard_rules.size(); ++node) {
    Relationship();
    if (gd::GuardPath(source_.guard_rules[node])
            .ends_with(kExtensionsPathSuffix) &&
        CountChildren(d_, node) != 0)
      gd::Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kIndexRole);
  }
  Animations();
  CheckSharedViews();
  // Domain cardinality is established from the actual complete role index,
  // before binary decoding. A later decode failure retains expected/zero facts.
  s_.facts.weight_sums.emplace();
  for (const auto& primitive : primitives_) {
    bool floating = false;
    for (std::size_t set = 0; set < primitive.sets; ++set)
      for (const auto& accessor : s_.accessor_storage) {
        Relationship();
        if (accessor.source.index ==
                s_.influences[primitive.first_set + set].weights_accessor &&
            accessor.source.component_type == Type::kFloat32)
          floating = true;
      }
    if (floating)
      s_.facts.weight_sums->floating_rows_expected =
          Add(s_.facts.weight_sums->floating_rows_expected, primitive.rows);
  }
  for (auto& accessor : s_.accessor_storage) {
    if (accessor.source.component_type != Type::kFloat32) continue;
    std::uint64_t constrained = 0;
    for (const auto& use : uses_) {
      Relationship();
      if (use.view.accessor == accessor.source.index)
        constrained = std::max(constrained, use.constrained_rows);
    }
    accessor.unconstrained_float_words = constrained < accessor.source.count;
  }
  PlanRoles();
  s_.facts.phase = GltfDeformationPhase::kRoles;
}

void Catalog::Decode() {
  GltfExtractionLimits clipped = limits_;
  clipped.max_components =
      std::min(clipped.max_components, s_.policy.limits.max_scalar_values);
  clipped.max_output_requested_bytes = std::min(
      clipped.max_output_requested_bytes, s_.policy.limits.max_decoded_bytes);
  std::uint64_t authored_scalars = 0, scalar_plan = 0, byte_plan = 0;
  std::uint64_t planned_sparse_writes = 0;
  tasks_.clear();
  total_units_ = 0;
  const gd::AccessorCopyWork preparation{
      [](void* state) {
        auto& catalog = *static_cast<Catalog*>(state);
        catalog.preparation_scalars_ = Add(catalog.preparation_scalars_, 1);
        if (catalog.preparation_scalars_ %
                catalog.s_.policy.work.cancellation_block_scalars ==
            0)
          catalog.s_.allocation.Checkpoint();
      },
      [](void* state) { static_cast<Catalog*>(state)->Relationship(); },
      [](std::size_t count, void* state) {
        auto& catalog = *static_cast<Catalog*>(state);
        for (std::size_t byte = 0; byte < count; ++byte) catalog.MetadataByte();
      },
      this};
  for (std::size_t index = 0; index < s_.accessor_storage.size(); ++index) {
    auto& storage = s_.accessor_storage[index];
    s_.allocation.Checkpoint();
    auto scalars = Mul(storage.source.count, Width(storage.source));
    authored_scalars = Add(authored_scalars, scalars);
    Limit(authored_scalars, s_.policy.limits.max_scalar_values);
    const auto c =
        Add(Width(storage.source),
            Add(Mul(2, std::bit_width(storage.source.sparse
                                          ? storage.source.sparse->count
                                          : 0)),
                2));
    const auto bytes =
        Mul(Width(storage.source), ScalarBytes(storage.source.component_type));
    tasks_.push_back(
        {TaskKind::kDecode, index, 0, storage.source.count, c, bytes});
    total_units_ = Add(total_units_, storage.source.count);
    scalar_plan = Add(scalar_plan, Mul(storage.source.count, c));
    const auto source_rows = storage.source.base ? storage.source.count
                             : storage.source.sparse
                                 ? storage.source.sparse->count
                                 : 0;
    byte_plan = Add(byte_plan, Mul(source_rows, bytes));
    if (storage.source.sparse) {
      planned_sparse_writes =
          Add(planned_sparse_writes, storage.source.sparse->count);
      Limit(planned_sparse_writes, s_.policy.limits.max_sparse_writes);
    }
  }
  (void)Add(scalar_plan, Mul(tasks_.size(),
                             std::min(total_units_, s_.policy.work.max_jobs)));
  // All collection extents are checked before payload storage preparation.
  // Existing allocator/STL value-initialization is an admitted indivisible
  // preparation boundary, not a copied/admitted row or an invented B count.
  for (auto& storage : s_.accessor_storage) {
    s_.allocation.Checkpoint();
    auto result = gd::PrepareAccessorDecode(storage.source, false, clipped,
                                            s_.decoded, &preparation);
    if (!result.values) throw gd::GltfFailure(result.diagnostic);
    storage.decoded = std::move(*result.values);
    s_.allocation.Checkpoint();
  }
  RunTasks(scalar_plan, byte_plan, true);
  for (auto& storage : s_.accessor_storage) {
    s_.allocation.Checkpoint();
    const auto accessor = Row(d_, kAccessorsField, storage.source.index);
    for (auto key :
         {std::string_view(kMinimumField), std::string_view(kMaximumField)})
      if (auto bound = Member(d_, accessor, key)) {
        if (!s_.facts.numeric) s_.facts.numeric.emplace();
        auto values = gd::ReadAccessorBound(d_, *bound, storage.source,
                                            s_.decoded, &*s_.facts.numeric);
        if (key == kMinimumField)
          storage.minimum = std::move(values);
        else
          storage.maximum = std::move(values);
      }
  }
}

void Catalog::Gate(Partial& partial, std::uint64_t partition, bool caller) {
  if (!partial.facts.numeric) partial.facts.numeric.emplace();
  if (!gd::EstablishGltfNumericalProfile(*partial.facts.numeric, partition,
                                         caller)) {
    partial.facts.phase = GltfDeformationPhase::kNumericGate;
    gd::Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kNumericalProfile);
  }
}
void Catalog::Scalar(Partial& partial, gd::QuaternionWork& work) {
  auto& n = partial.facts.counts.scalar_checks;
  n = Add(n, 1);
  if (n % s_.policy.work.cancellation_block_scalars == 0) work.Checkpoint();
}
GltfQuaternionEncoding Encoding(const gd::AccessorSource& a) {
  switch (a.component_type) {
    case Type::kFloat32:
      return GltfQuaternionEncoding::kFloat32;
    case Type::kInt8:
      return GltfQuaternionEncoding::kSnorm8;
    case Type::kUint8:
      return GltfQuaternionEncoding::kUnorm8;
    case Type::kInt16:
      return GltfQuaternionEncoding::kSnorm16;
    case Type::kUint16:
      return GltfQuaternionEncoding::kUnorm16;
    case Type::kUint32:
      break;
  }
  throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
}
void Catalog::RoleRow(const Use& use, std::uint64_t row, Partial& partial,
                      gd::QuaternionWork& work) {
  const auto& a = s_.accessor_storage[use.storage];
  const auto& v = use.view;
  auto width = Width(a.source);
  auto& location = partial.facts.location;
  location = {v.node,
              v.skin,
              v.target,
              v.set,
              v.sampler,
              v.channel,
              v.mesh ? std::optional(row) : std::nullopt,
              {}};
  partial.facts.phase = UsePhase(v);
  // Exact decode establishes layout before these source-word loads. Each dense
  // row and each replacement row is role-admitted even if later overwritten.
  auto check = [&](std::span<const std::uint32_t> words) {
    if (v.role == Role::kInverseBindMatrices && row >= use.constrained_rows)
      return;  // No finite/affine MUST applies to unused extra IBM rows.
    for (std::size_t c = 0; c < width; ++c) {
      Scalar(partial, work);
      location.component = c;
      if (v.role != Role::kRotationValues &&
          a.source.component_type == Type::kFloat32 && !Finite(words[c]))
        Malformed();
      if (v.role == Role::kWeights &&
          a.source.component_type == Type::kFloat32 && (words[c] & kSign) &&
          (words[c] & kMagnitude))
        Malformed();
      if (v.role == Role::kAnimationTimes && (words[c] & kSign) &&
          (words[c] & kMagnitude))
        Malformed();
      if (v.role == Role::kBaseAttribute && v.semantic.starts_with(kColor) &&
          Floating(a.source)) {
        if (((words[c] & kSign) && (words[c] & kMagnitude)) ||
            (words[c] & kMagnitude) > kFloatOne)
          Malformed();
      }
      if (v.semantic == kIndices) {
        const Primitive* primitive = nullptr;
        for (const auto& candidate : primitives_) {
          Reference(partial, work);
          if (candidate.mesh == v.mesh && candidate.ordinal == v.primitive) {
            primitive = &candidate;
            break;
          }
        }
        Require(primitive != nullptr);
        const std::uint32_t restart =
            a.source.component_type == Type::kUint8
                ? std::numeric_limits<std::uint8_t>::max()
            : a.source.component_type == Type::kUint16
                ? std::numeric_limits<std::uint16_t>::max()
                : std::numeric_limits<std::uint32_t>::max();
        Require(words[c] < primitive->rows && words[c] != restart);
      }
      if (v.role == Role::kJoints) {
        for (const auto& user : s_.users) {
          Reference(partial, work);
          if (user.mesh == v.mesh && user.primitive == v.primitive &&
              user.skin) {
            location.user = user.node;
            location.skin = user.skin;
            Require(words[c] < s_.joint_storage[Host(*user.skin)].size());
          }
        }
      }
    }
    if (v.role == Role::kInverseBindMatrices) {
      constexpr std::array<std::size_t, 3> kAffineZeroSlots{3, 7, 11};
      for (auto c : kAffineZeroSlots) Require((words[c] & kMagnitude) == 0);
      Require(words[kMatrixComponents - 1] == kFloatOne);
    }
    if (v.role == Role::kBaseAttribute && v.semantic == kTangent)
      Require((words[kLanes - 1] & kMagnitude) == kFloatOne);
    if (v.role == Role::kBaseAttribute &&
        (v.semantic == kNormal || v.semantic == kTangent)) {
      // Same inherited base-direction representation bound as the ordinary
      // extractor; morph deltas deliberately do not enter this predicate.
      constexpr double kMaximumSquaredDirectionError = 0x1p-16;
      Gate(partial, *work.partition, work.caller);
      volatile double square = 0;
      for (std::size_t c = 0; c < kDirectionComponents; ++c) {
        Scalar(partial, work);
        const double value = Value(words[c], Type::kFloat32, false);
        volatile double product = value * value;
        square = square + product;
      }
      if (std::abs(square - 1.0) > kMaximumSquaredDirectionError)
        gd::Fail(GltfStatus::kUnsupportedProfile,
                 GltfDiagnosticCode::kBinaryValue, {}, kRoleRule);
    }
    if (v.role == Role::kRotationValues) {
      const GltfDeformationAnimationView* animation_pointer = nullptr;
      for (const auto& candidate : s_.animations) {
        Reference(partial, work);
        if (candidate.animation == v.animation &&
            candidate.channel == v.channel) {
          animation_pointer = &candidate;
          break;
        }
      }
      if (!animation_pointer)
        throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
      const auto& animation = *animation_pointer;
      const bool tangent =
          animation.interpolation == Interpolation::kCubicSpline &&
          row % kCubicGroups != 1;
      const auto role = tangent ? GltfQuaternionRole::kCubicTangent
                                : GltfQuaternionRole::kAnimationValue;
      if (!partial.facts.quaternions) {
        partial.facts.quaternions.emplace();
        partial.facts.quaternions->policy = gd::kDeformationQuaternionPolicy;
      }
      auto& q = *partial.facts.quaternions;
      std::span<const std::uint32_t, gd::kQuaternionComponents> tuple(
          words.data(), gd::kQuaternionComponents);
      if (!partial.facts.numeric) partial.facts.numeric.emplace();
      auto result = gd::EvaluateQuaternionWords(tuple, Encoding(a.source), role,
                                                *partial.work_space, work,
                                                *partial.facts.numeric);
      if (tangent)
        q.tangent_rows_checked = Add(q.tangent_rows_checked, 1);
      else
        q.animation_values_checked = Add(q.animation_values_checked, 1);
      if (result.decision != gd::QuaternionDecision::kAdmitted) {
        location.component = result.component;
        if (result.decision == gd::QuaternionDecision::kNumericRefusal) {
          partial.facts.phase = GltfDeformationPhase::kNumericGate;
          gd::Fail(GltfStatus::kUnsupportedProfile,
                   GltfDiagnosticCode::kNumericalProfile);
        }
        auto reason =
            result.decision == gd::QuaternionDecision::kNonfinite
                ? GltfQuaternionSourceRefusalReason::kNonfinite
            : result.decision == gd::QuaternionDecision::kRepresentation
                ? GltfQuaternionSourceRefusalReason::kRepresentation
                : GltfQuaternionSourceRefusalReason::kNonunit;
        q.first_refusal = {
            role,
            Encoding(a.source),
            v.node,
            v.animation,
            v.sampler,
            animation.interpolation == Interpolation::kCubicSpline
                ? row / kCubicGroups
                : row,
            a.source.index,
            row,
            reason};
        gd::Fail(result.decision == gd::QuaternionDecision::kRepresentation
                     ? GltfStatus::kUnsupportedProfile
                     : GltfStatus::kMalformed,
                 result.decision == gd::QuaternionDecision::kRepresentation
                     ? GltfDiagnosticCode::kUnsupportedNumberRepresentation
                     : GltfDiagnosticCode::kBinaryValue,
                 {},
                 result.rule ? static_cast<std::uint32_t>(*result.rule) : 0);
      }
      q.rows_admitted = Add(q.rows_admitted, 1);
    }
  };
  const auto layout =
      gd::DescribeAccessorLayout(a.source.component_type, a.source.shape);
  auto source_row = [&](const gd::AccessorByteView& view, std::size_t offset,
                        std::size_t ordinal, std::size_t step) {
    std::array<std::uint32_t, kMatrixComponents> words{};
    auto start = offset + ordinal * step;
    for (std::size_t c = 0; c < width; ++c) {
      auto position = start + (c / layout.rows) * layout.column_step +
                      (c % layout.rows) * layout.component_bytes;
      for (std::size_t b = 0; b < layout.component_bytes; ++b) work.ByteVisit();
      words[c] = ReadWord(view.bytes.data() + position, layout.component_bytes);
    }
    try {
      check(std::span(words).first(width));
    } catch (const gd::GltfFailure& error) {
      auto diagnostic = error.Diagnostic();
      diagnostic.resource_kind = GltfResourceKind::kBuffer;
      diagnostic.resource_index = view.resource_index;
      diagnostic.resource_span = {Add(view.resource_offset, start),
                                  layout.final_extent};
      throw gd::GltfFailure(diagnostic);
    }
  };
  if (a.source.base)
    source_row(*a.source.base, a.source.base_offset, Host(row),
               a.decoded.base_stride);
  bool replaced = false;
  if (a.source.sparse) {
    std::size_t low = 0, high = a.decoded.sparse_destinations.size();
    while (low < high) {
      Reference(partial, work);
      const auto middle = low + (high - low) / 2;
      if (a.decoded.sparse_destinations[middle] < row)
        low = middle + 1;
      else
        high = middle;
    }
    if (low != a.decoded.sparse_destinations.size() &&
        a.decoded.sparse_destinations[low] == row) {
      replaced = true;
      source_row(a.source.sparse->values, a.source.sparse->values_offset, low,
                 layout.packed_step);
    }
  }
  // Effective implicit-zero rows are real role inputs, not an absent buffer.
  std::array<std::uint32_t, kMatrixComponents> effective{};
  for (std::size_t c = 0; c < width; ++c)
    effective[c] = Load(a, Host(row) * width + c, work);
  if (!a.source.base && !replaced) check(std::span(effective).first(width));
  if (v.role == Role::kAnimationTimes && row) {
    Gate(partial, *work.partition, work.caller);
    auto previous_word = Load(a, Host(row - 1), work);
    Scalar(partial, work);
    Require(Finite(previous_word));
    const auto previous = Value(previous_word, Type::kFloat32, false);
    const auto current = Value(effective[0], Type::kFloat32, false);
    Require(current > previous);
  }
  if (v.role == Role::kAnimationTimes)
    partial.facts.counts.keys_checked =
        Add(partial.facts.counts.keys_checked, 1);
}

void Catalog::InfluenceRow(const Primitive& primitive, std::uint64_t row,
                           Partial& partial, gd::QuaternionWork& work) {
  partial.facts.phase = GltfDeformationPhase::kWeightSums;
  partial.facts.location.vertex = row;
  bool floating = false, all_zero = true;
  std::uint64_t integer_sum = 0, nonzero = 0;
  double sum = 0;
  auto storage_for = [&](std::uint64_t index) -> const AccessorStorage& {
    for (const auto& a : s_.accessor_storage) {
      Reference(partial, work);
      if (a.source.index == index) return a;
    }
    throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
  };
  for (std::size_t set = 0; set < primitive.sets; ++set) {
    const auto& pair = s_.influences[primitive.first_set + set];
    if (storage_for(pair.weights_accessor).source.component_type ==
        Type::kFloat32)
      floating = true;
  }
  if (floating) Gate(partial, *work.partition, work.caller);
  for (std::size_t set = 0; set < primitive.sets; ++set) {
    partial.facts.location.set = set;
    const auto& pair = s_.influences[primitive.first_set + set];
    const auto& weights = storage_for(pair.weights_accessor);
    const auto& joints = storage_for(pair.joints_accessor);
    for (std::size_t lane = 0; lane < kLanes; ++lane) {
      Scalar(partial, work);
      partial.facts.counts.lane_checks =
          Add(partial.facts.counts.lane_checks, 1);
      auto scalar = Host(row) * kLanes + lane;
      auto word = Load(weights, scalar, work);
      if (weights.source.component_type == Type::kFloat32) {
        Require(Finite(word));
        Require(!(word & kSign) || !(word & kMagnitude));
      }
      const bool zero = weights.source.component_type == Type::kFloat32
                            ? !(word & kMagnitude)
                            : word == 0;
      if (!zero) {
        ++nonzero;
        all_zero = false;
        auto joint = Load(joints, scalar, work);
        // Check all preceding lanes, including preceding authored sets. No lane
        // pruning, sorting or accumulator keyed by a guessed palette size.
        for (std::size_t previous_set = 0; previous_set <= set;
             ++previous_set) {
          const auto& earlier =
              s_.influences[primitive.first_set + previous_set];
          const auto& earlier_weights = storage_for(earlier.weights_accessor);
          const auto& earlier_joints = storage_for(earlier.joints_accessor);
          const auto end = previous_set == set ? lane : kLanes;
          for (std::size_t previous_lane = 0; previous_lane < end;
               ++previous_lane) {
            Reference(partial, work);
            auto offset = Host(row) * kLanes + previous_lane;
            auto w = Load(earlier_weights, offset, work);
            const bool previous_zero =
                earlier_weights.source.component_type == Type::kFloat32
                    ? !(w & kMagnitude)
                    : w == 0;
            if (!previous_zero)
              Require(Load(earlier_joints, offset, work) != joint);
          }
        }
      }
      if (floating) {
        volatile double next = sum + Value(word, weights.source.component_type,
                                           weights.source.normalized);
        sum = next;
      } else {
        integer_sum = Add(
            integer_sum, Mul(word, weights.source.component_type == Type::kUint8
                                       ? kByteToShort
                                       : 1));
      }
    }
  }
  if (!floating) {
    Require(integer_sum == kQuantizedOne);
    return;
  }
  volatile double delta = sum - 1.0;
  const double deviation = std::abs(delta);
  volatile double epsilon = kWeightEpsilon * static_cast<double>(nonzero);
  GltfWeightSumSample sample{
      primitive.mesh,
      primitive.ordinal,
      row,
      primitive.sets,
      nonzero,
      std::bit_cast<std::uint64_t>(sum),
      std::bit_cast<std::uint64_t>(deviation),
      std::bit_cast<std::uint64_t>(static_cast<double>(epsilon)),
      deviation > epsilon,
      all_zero};
  if (!partial.facts.weight_sums) partial.facts.weight_sums.emplace();
  auto& summary = *partial.facts.weight_sums;
  summary.rows_evaluated = Add(summary.rows_evaluated, 1);
  if (sample.advisory) summary.advisory_rows = Add(summary.advisory_rows, 1);
  if (sample.all_zero) summary.all_zero_rows = Add(summary.all_zero_rows, 1);
  if (!summary.first_evaluated) summary.first_evaluated = sample;
  if (sample.advisory && !summary.first_advisory)
    summary.first_advisory = sample;
}

void Catalog::Bound(const AccessorStorage& a, std::uint64_t component,
                    Partial& partial, gd::QuaternionWork& work) {
  partial.facts.phase = GltfDeformationPhase::kRoles;
  partial.facts.location.component = component;
  auto width = Width(a.source);
  if (!a.source.count) return;
  Gate(partial, *work.partition, work.caller);
  auto first = Load(a, Host(component), work);
  // Bounds cannot lend a finite arithmetic interpretation to unconstrained
  // extra IBM NaN payloads. Such authored finite bounds cannot be true extrema.
  if (a.source.component_type == Type::kFloat32) Require(Finite(first));
  auto low = Value(first, a.source.component_type, false), high = low;
  for (std::size_t row = 0; row < a.source.count; ++row) {
    Scalar(partial, work);
    if (row % s_.policy.work.cancellation_block_scalars == 0)
      Gate(partial, *work.partition, work.caller);
    auto word = Load(a, row * width + Host(component), work);
    if (a.source.component_type == Type::kFloat32) Require(Finite(word));
    auto value = Value(word, a.source.component_type, false);
    low = std::min(low, value);
    high = std::max(high, value);
  }
  auto bound_value = [&](const gd::OwnedRawValues& raw) {
    return std::visit(
        [&](const auto& values) {
          return static_cast<double>(values[Host(component)]);
        },
        raw);
  };
  if ((a.minimum && bound_value(*a.minimum) != low) ||
      (a.maximum && bound_value(*a.maximum) != high))
    gd::Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kBinaryValue, {},
             kBoundsRule);
}

void Catalog::Derived(std::size_t index, std::uint64_t row, Partial& partial,
                      gd::QuaternionWork& work) {
  auto& a = s_.accessor_storage[index];
  auto width = Width(a.source);
  if (a.source.normalized &&
      request_.values == GltfValueRequest::kRawAndNormalized)
    Gate(partial, *work.partition, work.caller);
  for (std::size_t component = 0; component < width; ++component) {
    Scalar(partial, work);
    auto offset = Host(row) * width + component;
    auto word = Load(a, offset, work);
    if (!a.decoded.normalized.empty()) {
      a.decoded.normalized[offset] = Value(word, a.source.component_type, true);
      for (std::size_t b = 0; b < sizeof(double); ++b) work.ByteVisit();
      partial.facts.counts.decoded_bytes_checked =
          Add(partial.facts.counts.decoded_bytes_checked, sizeof(double));
    }
  }
}
void Catalog::Node(std::size_t index, Partial& partial,
                   gd::QuaternionWork& work) {
  const auto& source = s_.asset.NodeRotationSources()[index];
  Require(source.authored && source.projected_components &&
          source.authored_components.size() == gd::kQuaternionComponents);
  partial.facts.phase = GltfDeformationPhase::kDeclarations;
  partial.facts.location.user = source.node;
  if (!partial.facts.quaternions) {
    partial.facts.quaternions.emplace();
    partial.facts.quaternions->policy = gd::kDeformationQuaternionPolicy;
  }
  auto& q = *partial.facts.quaternions;
  if (!partial.facts.numeric) partial.facts.numeric.emplace();
  auto result = gd::EvaluateNodeQuaternion(
      std::span<const GltfRawNumberView, gd::kQuaternionComponents>(
          source.authored_components.data(), gd::kQuaternionComponents),
      *source.projected_components, *partial.work_space, work,
      *partial.facts.numeric);
  q.node_values_checked = Add(q.node_values_checked, 1);
  if (result.decision != gd::QuaternionDecision::kAdmitted) {
    partial.facts.location.component = result.component;
    if (result.decision == gd::QuaternionDecision::kNumericRefusal) {
      partial.facts.phase = GltfDeformationPhase::kNumericGate;
      gd::Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kNumericalProfile);
    }
    const bool representation =
        result.decision == gd::QuaternionDecision::kRepresentation;
    q.first_refusal = {GltfQuaternionRole::kNodeRotation,
                       GltfQuaternionEncoding::kJsonDecimal,
                       source.node,
                       {},
                       {},
                       {},
                       {},
                       {},
                       representation
                           ? GltfQuaternionSourceRefusalReason::kRepresentation
                       : result.decision == gd::QuaternionDecision::kRange
                           ? GltfQuaternionSourceRefusalReason::kRange
                           : GltfQuaternionSourceRefusalReason::kNonunit};
    gd::Fail(representation ? GltfStatus::kUnsupportedProfile
                            : GltfStatus::kMalformed,
             representation
                 ? GltfDiagnosticCode::kUnsupportedNumberRepresentation
                 : GltfDiagnosticCode::kCoreRange,
             result.component
                 ? std::optional(
                       source.authored_components[*result.component].source)
                 : std::nullopt,
             result.rule ? static_cast<std::uint32_t>(*result.rule) : 0);
  }
  q.rows_admitted = Add(q.rows_admitted, 1);
}

void Catalog::CopyRow(std::size_t index, std::uint64_t row, Partial& partial,
                      gd::QuaternionWork& work) {
  struct CopyState {
    Catalog& catalog;
    Partial& partial;
    gd::QuaternionWork& work;
  } state{*this, partial, work};
  const gd::AccessorCopyWork callbacks{
      [](void* opaque) {
        auto& copy = *static_cast<CopyState*>(opaque);
        copy.partial.copy_scalar_visits =
            Add(copy.partial.copy_scalar_visits, 1);
        if (copy.partial.copy_scalar_visits %
                copy.catalog.s_.policy.work.cancellation_block_scalars ==
            0)
          copy.work.Checkpoint();
      },
      [](void* opaque) {
        auto& copy = *static_cast<CopyState*>(opaque);
        copy.catalog.Reference(copy.partial, copy.work);
      },
      [](std::size_t bytes, void* opaque) {
        auto& copy = *static_cast<CopyState*>(opaque);
        for (std::size_t byte = 0; byte < bytes; ++byte) copy.work.ByteVisit();
      },
      &state};
  auto& storage = s_.accessor_storage[index];
  partial.facts.phase = GltfDeformationPhase::kRoles;
  const bool replaced =
      std::binary_search(storage.decoded.sparse_destinations.begin(),
                         storage.decoded.sparse_destinations.end(), row,
                         [&](std::uint64_t a, std::uint64_t b) {
                           Reference(partial, work);
                           return a < b;
                         });
  auto diagnostic = gd::CopyPreparedAccessorRows(
      storage.source, storage.decoded, Host(row), Host(Add(row, 1)), s_.decoded,
      {}, &callbacks);
  if (diagnostic.status != GltfStatus::kAccepted)
    throw gd::GltfFailure(diagnostic);
  if (replaced)
    partial.facts.counts.sparse_writes =
        Add(partial.facts.counts.sparse_writes, 1);
  partial.facts.counts.decoded_bytes_checked = Add(
      partial.facts.counts.decoded_bytes_checked,
      Mul(Width(storage.source), ScalarBytes(storage.source.component_type)));
}
void Catalog::RunJob(std::size_t ordinal) {
  auto& partial = partials_[ordinal];
  partial.entered = true;
  if (!decode_phase_)
    partial.work_space =
        workspaces_[caller_ ? 0 : ordinal % workspaces_.size()];
  gd::QuaternionWork work{s_.descriptors, s_.policy.work, 0, 0,
                          ordinal,        caller_};
  struct RecordWork {
    gd::QuaternionWork& work;
    Partial& partial;
    ~RecordWork() noexcept {
      partial.integer_steps = work.integer_steps;
      partial.byte_visits = work.byte_visits;
    }
  } record{work, partial};
  const auto jobs = partials_.size();
  const auto quotient = total_units_ / jobs, remainder = total_units_ % jobs;
  const auto first =
      Add(Mul(ordinal, quotient), std::min<std::uint64_t>(ordinal, remainder));
  const auto end = Add(first, quotient + (ordinal < remainder ? 1 : 0));
  std::uint64_t offset = 0;
  Gate(partial, ordinal, caller_);
  for (const auto& task : tasks_) {
    Reference(partial, work);
    const auto task_end = Add(offset, task.count);
    const auto begin = std::max(first, offset),
               finish = std::min(end, task_end);
    for (auto unit = begin; unit < finish; ++unit) {
      partial.facts.phase =
          task.kind == TaskKind::kRole ? UsePhase(uses_[task.index].view)
          : task.kind == TaskKind::kInfluence
              ? GltfDeformationPhase::kWeightSums
          : task.kind == TaskKind::kNode ? GltfDeformationPhase::kDeclarations
                                         : GltfDeformationPhase::kRoles;
      partial.facts.location = {};
      work.Checkpoint();
      partial.failed_unit = unit;
      const auto row = Add(task.first, unit - offset);
      switch (task.kind) {
        case TaskKind::kDecode:
          CopyRow(task.index, row, partial, work);
          break;
        case TaskKind::kRole:
          RoleRow(uses_[task.index], row, partial, work);
          break;
        case TaskKind::kInfluence:
          InfluenceRow(primitives_[task.index], row, partial, work);
          break;
        case TaskKind::kBounds:
          Bound(s_.accessor_storage[task.index], row, partial, work);
          break;
        case TaskKind::kNode:
          Node(task.index, partial, work);
          break;
        case TaskKind::kDerived:
          Derived(task.index, row, partial, work);
          break;
      }
      partial.failed_unit.reset();
    }
    offset = task_end;
  }
  work.Checkpoint();
  partial.completed = true;
}
void Catalog::Job(void* state, std::size_t ordinal) noexcept {
  auto& catalog = *static_cast<Catalog*>(state);
  try {
    catalog.RunJob(ordinal);
  } catch (const gd::GltfFailure& error) {
    catalog.partials_[ordinal].facts.diagnostic = error.Diagnostic();
    catalog.partials_[ordinal].facts.status = error.Diagnostic().status;
  } catch (const gd::AdmissionFailure& error) {
    if (error.Code() == gd::AdmissionCode::kInvalidObject) {
      catalog.partials_[ordinal].error = std::current_exception();
      return;
    }
    const auto status = error.Code() == gd::AdmissionCode::kCanceled
                            ? GltfStatus::kCanceled
                        : error.Code() == gd::AdmissionCode::kBudgetExceeded
                            ? GltfStatus::kBudgetExceeded
                            : GltfStatus::kUnsupportedProfile;
    const auto code = status == GltfStatus::kCanceled
                          ? GltfDiagnosticCode::kCanceled
                      : status == GltfStatus::kBudgetExceeded
                          ? GltfDiagnosticCode::kPayloadBudget
                          : GltfDiagnosticCode::kLimitExceeded;
    catalog.partials_[ordinal].facts.status = status;
    catalog.partials_[ordinal].facts.diagnostic = {status, code, {},        {},
                                                   {},     {},   kLimitRule};
  } catch (...) {
    catalog.partials_[ordinal].error = std::current_exception();
  }
}

bool Earlier(const GltfWeightSumSample& a, const GltfWeightSumSample& b) {
  if (a.mesh != b.mesh) return a.mesh < b.mesh;
  if (a.primitive != b.primitive) return a.primitive < b.primitive;
  return a.vertex < b.vertex;
}
void Merge(GltfWeightSumSummary& to, const GltfWeightSumSummary& from) {
  to.rows_evaluated = Add(to.rows_evaluated, from.rows_evaluated);
  to.advisory_rows = Add(to.advisory_rows, from.advisory_rows);
  to.all_zero_rows = Add(to.all_zero_rows, from.all_zero_rows);
  if (from.first_evaluated &&
      (!to.first_evaluated ||
       Earlier(*from.first_evaluated, *to.first_evaluated)))
    to.first_evaluated = from.first_evaluated;
  if (from.first_advisory &&
      (!to.first_advisory || Earlier(*from.first_advisory, *to.first_advisory)))
    to.first_advisory = from.first_advisory;
}
void Merge(GltfDeformationNumericObservation& to,
           const GltfDeformationNumericObservation& from) {
  to.guard_calls = Add(to.guard_calls, from.guard_calls);
  to.established_calls = Add(to.established_calls, from.established_calls);
  to.exact_blocks = Add(to.exact_blocks, from.exact_blocks);
  if (from.compiled_profile) to.compiled_profile = from.compiled_profile;
  const auto& a = from.exception_masks;
  auto& b = to.exception_masks;
  if (a.required_units &&
      (!b.required_units ||
       a.sample_partition.value_or(0) < b.sample_partition.value_or(0) ||
       (a.sample_partition.value_or(0) == b.sample_partition.value_or(0) &&
        a.sample_caller == true && b.sample_caller != true)))
    b = a;
  if (from.refusal && (!to.refusal ||
                       from.refusal->partition.value_or(0) <
                           to.refusal->partition.value_or(0) ||
                       (from.refusal->partition.value_or(0) ==
                            to.refusal->partition.value_or(0) &&
                        from.refusal->caller && !to.refusal->caller)))
    to.refusal = from.refusal;
}
void Catalog::PlanRoles() {
  tasks_.clear();
  total_units_ = 0;
  std::uint64_t scalar_plan = 0, byte_plan = 0;
  auto task = [&](Task t) {
    if (!t.count) return;
    Relationship();
    total_units_ = Add(total_units_, t.count);
    scalar_plan = Add(scalar_plan, Mul(t.count, t.scalar_bound));
    byte_plan = Add(byte_plan, Mul(t.count, t.byte_bound));
    tasks_.push_back(t);
  };
  // Fixed finite implementation bounds, not visits computed by doing the work
  // first. C and R are exclusive; quaternion I uses its owning private bound.
  const auto node_sources = s_.asset.NodeRotationSources();
  for (std::size_t i = 0; i < node_sources.size(); ++i) {
    Relationship();
    if (node_sources[i].authored) {
      const auto& node = node_sources[i];
      Require(node.authored_components.size() == gd::kQuaternionComponents);
      auto views =
          std::span<const GltfRawNumberView, gd::kQuaternionComponents>(
              node.authored_components.data(), gd::kQuaternionComponents);
      task({TaskKind::kNode, i, 0, 1, gd::kQuaternionNodeMaxIntegerSteps,
            gd::QuaternionNodeByteVisitBound(views)});
    }
  }
  auto role_tasks = [&](bool later) {
    for (std::size_t i = 0; i < uses_.size(); ++i) {
      Relationship();
      const auto& use = uses_[i];
      const bool delayed = use.view.role == Role::kJoints ||
                           use.view.role == Role::kInverseBindMatrices ||
                           use.view.role == Role::kMorphDelta ||
                           use.view.animation.has_value();
      if (delayed != later) continue;
      const auto& a = s_.accessor_storage[use.storage];
      auto c = Mul(kMaximumRolePredicatePasses, Width(a.source));
      const auto palette = Mul(Width(a.source), s_.users.size());
      const auto r =
          Add(std::bit_width(a.source.sparse ? a.source.sparse->count : 0),
              Mul(kMaximumRoleSourcePasses,
                  Add(palette, Add(primitives_.size(), s_.animations.size()))));
      auto s = Add(c, Add(r, 1));
      if (use.view.role == Role::kRotationValues)
        s = Add(s, Mul(kMaximumRoleSourcePasses,
                       gd::QuaternionWordsIntegerStepBound(
                           Encoding(a.source),
                           GltfQuaternionRole::kAnimationValue)));
      task({TaskKind::kRole, i, 0, a.source.count, s,
            Mul(use.view.role == Role::kAnimationTimes
                    ? kMaximumRoleSourcePasses + 1
                    : kMaximumRoleSourcePasses,
                Mul(Width(a.source), ScalarBytes(a.source.component_type)))});
    }
  };
  role_tasks(false);
  for (std::size_t i = 0; i < s_.accessor_storage.size(); ++i) {
    Relationship();
    const auto& a = s_.accessor_storage[i];
    if (Member(d_, Row(d_, kAccessorsField, a.source.index), kMinimumField) ||
        Member(d_, Row(d_, kAccessorsField, a.source.index), kMaximumField))
      task({TaskKind::kBounds, i, 0, Width(a.source), Add(a.source.count, 1),
            Mul(Add(a.source.count, 1), ScalarBytes(a.source.component_type))});
  }
  for (std::size_t i = 0; i < primitives_.size(); ++i) {
    Relationship();
    const auto& p = primitives_[i];
    if (!p.sets) continue;
    const auto lanes = Mul(p.sets, kLanes);
    const auto search =
        Mul(Add(Mul(lanes, lanes), Mul(lanes, kInfluenceLinearLookupPasses)),
            Add(s_.accessor_storage.size(), 1));
    task({TaskKind::kInfluence, i, 0, p.rows, Add(search, Mul(lanes, lanes)),
          Add(Mul(kInfluencePairSourceBytes, Mul(lanes, lanes)),
              Mul(kInfluenceLaneSourceBytes, lanes))});
  }
  role_tasks(true);
  for (std::size_t i = 0; i < s_.accessor_storage.size(); ++i) {
    Relationship();
    const auto& a = s_.accessor_storage[i];
    if (request_.values == GltfValueRequest::kRawAndNormalized &&
        a.source.normalized)
      task({TaskKind::kDerived, i, 0, a.source.count, Width(a.source),
            Mul(Width(a.source),
                Add(ScalarBytes(a.source.component_type), sizeof(double)))});
  }
  // Check the complete conservative extent before any payload preparation.
  (void)Add(scalar_plan, Mul(tasks_.size(),
                             std::min(total_units_, s_.policy.work.max_jobs)));
  role_units_ = total_units_;
  role_scalar_plan_ = scalar_plan;
  role_byte_plan_ = byte_plan;
  role_tasks_.swap(tasks_);
  total_units_ = 0;
}
void Catalog::Execute() {
  // The source/bounds phase is joined. Every required allocation is still
  // admitted under the same decoded sibling/complete parent before role jobs.
  for (auto& storage : s_.accessor_storage) {
    s_.allocation.Checkpoint();
    if (request_.values == GltfValueRequest::kRawAndNormalized &&
        storage.source.normalized) {
      storage.decoded.normalized.resize(
          Host(Mul(storage.source.count, Width(storage.source))));
      s_.allocation.Checkpoint();
    }
  }
  tasks_.clear();
  tasks_.swap(role_tasks_);
  total_units_ = role_units_;
  RunTasks(role_scalar_plan_, role_byte_plan_, false);
}
void Catalog::RunTasks(std::uint64_t scalar_plan, std::uint64_t byte_plan,
                       bool decode_phase) {
  decode_phase_ = decode_phase;
  scalar_plan =
      Add(scalar_plan,
          Mul(tasks_.size(), std::min(total_units_, s_.policy.work.max_jobs)));
  auto ceil = [](std::uint64_t value, std::uint64_t partition) {
    return value / partition + (value % partition != 0);
  };
  const auto& w = s_.policy.work;
  const bool eligible = scalar_plan >= w.parallel_min_scalars ||
                        byte_plan >= w.parallel_min_bytes;
  auto jobs = std::uint64_t{1};
  if (eligible && total_units_)
    jobs = std::min({w.max_jobs, total_units_,
                     std::max(ceil(scalar_plan, w.partition_scalars),
                              ceil(byte_plan, w.partition_bytes))});
  jobs = std::max<std::uint64_t>(jobs, 1);
  auto lease =
      geometry::TryReserveWorkers(source_.context, eligible ? Host(jobs) : 0);
  relationships_.store(s_.facts.counts.relationship_steps,
                       std::memory_order_relaxed);
  caller_ = lease.Count() < 2;
  workspaces_.clear();
  if (!decode_phase) {
    workspaces_.reserve(caller_ ? 1 : lease.Count());
    for (std::size_t worker = 0; worker < (caller_ ? 1 : lease.Count());
         ++worker)
      workspaces_.push_back(std::allocate_shared<gd::QuaternionWorkspace>(
          gd::Allocator<gd::QuaternionWorkspace>(s_.descriptors)));
  }
  gd::OwnedVector<Partial> partials{gd::Allocator<Partial>(s_.descriptors)};
  partials.reserve(Host(jobs));
  for (std::uint64_t j = 0; j < jobs; ++j) {
    partials.emplace_back();
    partials.back().facts.status = GltfStatus::kAccepted;
    partials.back().facts.diagnostic = gd::AcceptedDiagnostic();
  }
  partials_ = partials;
  s_.facts.execution.planned_jobs = Add(s_.facts.execution.planned_jobs, jobs);
  std::exception_ptr launch_error;
  gd::OperationWorkerStats stats;
  if (!caller_) {
    const gd::NativeExecutionOptions defaults;
    try {
      gd::RunOperationJobs(s_.descriptors, lease, Host(jobs), Job, this,
                           execution_ ? *execution_ : defaults, stats);
    } catch (...) {
      launch_error = std::current_exception();
    }
  } else {
    for (std::size_t j = 0; j < partials.size(); ++j) {
      ++s_.facts.execution.caller_jobs;
      Job(this, j);
    }
  }
  s_.facts.execution.launched_workers =
      Add(s_.facts.execution.launched_workers, stats.launched);
  s_.facts.execution.joined_workers =
      Add(s_.facts.execution.joined_workers, stats.joined);
  lease.Reset();
  std::optional<std::size_t> failed;
  std::uint64_t observed_scalar_work = 0, observed_byte_work = 0;
  for (std::size_t j = 0; j < partials.size(); ++j) {
    const auto& p = partials[j];
    observed_scalar_work =
        Add(observed_scalar_work,
            Add(p.copy_scalar_visits,
                Add(p.integer_steps, Add(p.facts.counts.scalar_checks,
                                         p.facts.counts.relationship_steps))));
    observed_byte_work = Add(observed_byte_work, p.byte_visits);
    s_.facts.counts.scalar_checks =
        Add(s_.facts.counts.scalar_checks, p.facts.counts.scalar_checks);
    s_.facts.counts.lane_checks =
        Add(s_.facts.counts.lane_checks, p.facts.counts.lane_checks);
    s_.facts.counts.keys_checked =
        Add(s_.facts.counts.keys_checked, p.facts.counts.keys_checked);
    s_.facts.counts.decoded_bytes_checked =
        Add(s_.facts.counts.decoded_bytes_checked,
            p.facts.counts.decoded_bytes_checked);
    s_.facts.counts.sparse_writes =
        Add(s_.facts.counts.sparse_writes, p.facts.counts.sparse_writes);
    s_.facts.counts.relationship_steps = Add(s_.facts.counts.relationship_steps,
                                             p.facts.counts.relationship_steps);
    if (p.facts.weight_sums) Merge(*s_.facts.weight_sums, *p.facts.weight_sums);
    if (p.facts.numeric) {
      if (!s_.facts.numeric) s_.facts.numeric.emplace();
      Merge(*s_.facts.numeric, *p.facts.numeric);
    }
    if (p.facts.quaternions) {
      if (!s_.facts.quaternions) {
        s_.facts.quaternions.emplace();
        s_.facts.quaternions->policy = gd::kDeformationQuaternionPolicy;
      }
      auto& q = *s_.facts.quaternions;
      const auto& f = *p.facts.quaternions;
      q.node_values_checked = Add(q.node_values_checked, f.node_values_checked);
      q.animation_values_checked =
          Add(q.animation_values_checked, f.animation_values_checked);
      q.tangent_rows_checked =
          Add(q.tangent_rows_checked, f.tangent_rows_checked);
      q.rows_admitted = Add(q.rows_admitted, f.rows_admitted);
      if (!q.first_refusal && f.first_refusal)
        q.first_refusal = f.first_refusal;
    }
    if (p.facts.status != GltfStatus::kAccepted &&
        (!failed ||
         (p.failed_unit && (!partials[*failed].failed_unit ||
                            p.failed_unit < partials[*failed].failed_unit))))
      failed = j;
  }
  if (launch_error) std::rethrow_exception(launch_error);
  for (const auto& p : partials)
    if (p.error) std::rethrow_exception(p.error);
  for (const auto& p : partials)
    if (!p.entered)
      throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
  if (observed_scalar_work > scalar_plan || observed_byte_work > byte_plan)
    throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
  // The declared floating fold domain has its own completion decision, even
  // when a different role later fails. Equality alone never attests completion:
  // this is the actual required final sum-domain checkpoint.
  if (!decode_phase && s_.facts.weight_sums->rows_evaluated ==
                           s_.facts.weight_sums->floating_rows_expected) {
    s_.allocation.Checkpoint();
    s_.facts.weight_sums->complete = true;
    s_.weight_sums = *s_.facts.weight_sums;
  }
  // Actual interruption/resource failures take precedence over a data verdict.
  for (std::size_t j = 0; j < partials.size(); ++j)
    if (partials[j].facts.status != GltfStatus::kAccepted &&
        partials[j].facts.status != GltfStatus::kMalformed) {
      failed = j;
      break;
    }
  if (failed) {
    s_.facts.phase = partials[*failed].facts.phase;
    s_.facts.location = partials[*failed].facts.location;
    throw gd::GltfFailure(partials[*failed].facts.diagnostic);
  }
  for (const auto& p : partials)
    if (!p.completed)
      throw gd::AdmissionFailure(gd::AdmissionCode::kInvalidObject);
  Limit(s_.facts.counts.relationship_steps,
        s_.policy.limits.max_relationship_steps);
  s_.allocation.Checkpoint();
  Limit(s_.facts.counts.decoded_bytes_checked,
        s_.policy.limits.max_decoded_bytes);
  if (decode_phase) return;
  Require(s_.facts.weight_sums->complete);
  if (s_.facts.quaternions) s_.facts.quaternions->complete = true;
  s_.weight_sums = *s_.facts.weight_sums;
  s_.facts.predicates.source_admission = true;
  s_.facts.predicates.influences = true;
  s_.facts.predicates.bindings = true;
  s_.facts.predicates.morphs = true;
  s_.facts.predicates.affecting_animation = true;
}

void Catalog::Freeze() {
  // All growable backing is final before publishing any span into it. Nested
  // vector moves retain their own allocations; no public span points at a
  // vector object or a temporary row/decoder result.
  s_.accessors.reserve(s_.accessor_storage.size());
  for (auto& a : s_.accessor_storage) {
    s_.allocation.Checkpoint();
    std::optional<GltfSparseProvenance> sparse;
    if (a.source.sparse) {
      const auto& x = *a.source.sparse;
      sparse = {x.indices.index, x.indices_offset, x.index_type, x.values.index,
                x.values_offset};
    }
    auto state = GltfDeformationDerivedValues::kNotRequested;
    if (request_.values == GltfValueRequest::kRawAndNormalized &&
        a.source.normalized) {
      state = GltfDeformationDerivedValues::kAvailable;
    }
    if (a.unconstrained_float_words)
      state = GltfDeformationDerivedValues::kUnconstrainedRawBits;
    s_.accessors.push_back(
        {a.source.index, a.source.component_type, a.source.shape,
         a.source.normalized, a.source.count,
         a.source.base ? std::optional(a.source.base->index) : std::nullopt,
         a.source.base_offset, a.decoded.base_stride,
         a.decoded.packed_sparse_step, sparse, a.decoded.sparse_destinations,
         a.minimum ? std::optional(Raw(*a.minimum, false)) : std::nullopt,
         a.maximum ? std::optional(Raw(*a.maximum, false)) : std::nullopt,
         Raw(a.decoded.raw, a.decoded.float_words), state,
         a.decoded.normalized});
  }
  for (std::size_t i = 0; i < s_.skins.size(); ++i)
    s_.skins[i].joints = s_.joint_storage[i];
  for (std::size_t i = 0; i < s_.users.size(); ++i) {
    s_.users[i].ancestor_nodes = s_.ancestors[i];
    s_.users[i].joint_ancestor_nodes = s_.joint_ancestors[i];
  }
  std::size_t first = 0;
  for (auto& morph : s_.morphs) {
    auto end = first;
    while (end < s_.morph_channels.size() &&
           s_.morph_channels[end].mesh == morph.mesh &&
           s_.morph_channels[end].primitive == morph.primitive)
      ++end;
    morph.channels = std::span(s_.morph_channels).subspan(first, end - first);
    first = end;
  }
  for (std::size_t i = 0; i < s_.defaults.size(); ++i)
    s_.defaults[i].authored_values = s_.defaults_storage[i];
}

}  // namespace

ValidationResult ValidateDeformation(
    const GltfAsset& asset, const GltfPrimitiveHandle& primitive,
    const GltfExtractionRequest& request,
    const gd::AllocationContext& complete_allocation,
    const GltfExtractionLimits& limits,
    const gd::NativeExecutionOptions* execution) {
  ValidationResult result{{}, gd::AcceptedDiagnostic(), {}};
  std::shared_ptr<State> state;
  std::optional<gd::AllocationContext> scope;
  auto& observation = result.observation;
  observation.work = request.deformation.work;
  try {
    gd::ValidateDeformationConfiguration(request.deformation);
    if ((request.triangles != GltfTriangleRequest::kRequireTriangles &&
         request.triangles != GltfTriangleRequest::kConvertStripOrFan) ||
        (request.values != GltfValueRequest::kRaw &&
         request.values != GltfValueRequest::kRawAndNormalized))
      gd::Fail(GltfStatus::kInvalidRequest,
               GltfDiagnosticCode::kInvalidRequest);
    ValidateDeclarations(request);
    if (!gd::Access::PrimitiveRecordFor(asset, primitive))
      gd::Fail(GltfStatus::kForeignOwner, GltfDiagnosticCode::kForeignOwner);
    if (request.processing_profile !=
            GltfProcessingProfile::kDeformationRetainedTrianglesV1 ||
        asset.AdmissionProfile() !=
            GltfAdmissionProfile::kRetainedCoreDeformationV1)
      gd::Fail(GltfStatus::kInvalidRequest,
               GltfDiagnosticCode::kAdmissionProfileMismatch);
    if (!complete_allocation.SharesExecutionBudget(
            gd::Access::State(asset).allocation))
      gd::Fail(GltfStatus::kForeignOwner, GltfDiagnosticCode::kForeignOwner);
    const auto occupied = complete_allocation.OperationActiveBytes();
    const auto remaining = occupied < limits.max_output_requested_bytes
                               ? limits.max_output_requested_bytes - occupied
                               : 0;
    observation.effective_request_cap =
        std::min(request.deformation.limits.max_new_requested_bytes, remaining);
    if (observation.effective_request_cap == 0)
      gd::Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kLimitExceeded, {}, kLimitRule);
    scope.emplace(complete_allocation.WithAdditionalRequestLimit(
        Host(observation.effective_request_cap)));
    auto descriptors = scope->WithAdditionalRequestLimit(
        Host(request.deformation.limits.max_descriptor_bytes));
    auto decoded = scope->WithAdditionalRequestLimit(
        Host(request.deformation.limits.max_decoded_bytes));
    state = std::allocate_shared<State>(gd::Allocator<State>(descriptors),
                                        asset, primitive, request, *scope,
                                        descriptors, decoded);
    state->facts = observation;
    state->facts.phase = GltfDeformationPhase::kNumericGate;
    state->facts.numeric.emplace();
    if (!gd::EstablishGltfNumericalProfile(*state->facts.numeric, {}, true))
      gd::Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kNumericalProfile);
    {
      Catalog catalog(*state, request, limits, execution);
      catalog.Build();
      catalog.Decode();
      catalog.Execute();
      catalog.Freeze();
    }
    state->facts.phase = GltfDeformationPhase::kComplete;
    scope->Checkpoint();
    state->facts.complete = true;
    state->facts.phase = GltfDeformationPhase::kComplete;
    state->facts.status = GltfStatus::kAccepted;
    state->facts.diagnostic = gd::AcceptedDiagnostic();
    state->facts.new_requested_bytes = scope->OperationActiveBytes();
    result.observation = state->facts;
    result.proof = std::move(state);
    return result;
  } catch (const gd::GltfFailure& error) {
    result.diagnostic = error.Diagnostic();
  } catch (const gd::AdmissionFailure& error) {
    if (error.Code() == gd::AdmissionCode::kInvalidObject) throw;
    auto status = error.Code() == gd::AdmissionCode::kCanceled
                      ? GltfStatus::kCanceled
                  : error.Code() == gd::AdmissionCode::kBudgetExceeded
                      ? GltfStatus::kBudgetExceeded
                      : GltfStatus::kUnsupportedProfile;
    auto code = status == GltfStatus::kCanceled ? GltfDiagnosticCode::kCanceled
                : status == GltfStatus::kBudgetExceeded
                    ? GltfDiagnosticCode::kPayloadBudget
                    : GltfDiagnosticCode::kLimitExceeded;
    result.diagnostic = {status, code, {}, {}, {}, {}, kLimitRule};
  }
  if (state) observation = state->facts;
  if (scope) observation.new_requested_bytes = scope->OperationActiveBytes();
  observation.complete = false;
  if (result.diagnostic.code == GltfDiagnosticCode::kNumericalProfile)
    observation.phase = GltfDeformationPhase::kNumericGate;
  observation.status = result.diagnostic.status;
  observation.diagnostic = result.diagnostic;
  return result;
}

}  // namespace meshvale::interchange::gltf_deformation_detail
