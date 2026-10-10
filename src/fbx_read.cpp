// SPDX-License-Identifier: Apache-2.0
#include <fbxsdk.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#include "meshvale/interchange/fbx.h"

namespace meshvale::interchange {
namespace {

struct ImportFailure {
  std::string code;
  std::string subject;
};
[[noreturn]] void Fail(std::string code, std::string subject) {
  throw ImportFailure{std::move(code), std::move(subject)};
}
void Require(bool condition, const char* code, const char* subject) {
  if (!condition) Fail(code, subject);
}

// SDK objects created with a manager are destroyed by that manager, including
// early failures. No SDK object or borrowed value escapes this translation
// unit.
struct ManagerDeleter {
  void operator()(FbxManager* manager) const {
    if (manager) manager->Destroy();
  }
};
std::mutex& SdkMutex() {
  static std::mutex mutex;
  return mutex;
}

class InputStream final : public FbxStream {
 public:
  explicit InputStream(const std::vector<std::byte>& bytes) : bytes_(bytes) {}
  EState GetState() override { return open_ ? eOpen : eClosed; }
  bool Open(void*) override {
    position_ = 0;
    open_ = true;
    error_ = 0;
    return true;
  }
  bool Close() override {
    position_ = 0;
    open_ = false;
    return true;
  }
  bool Flush() override { return false; }
  std::size_t Write(const void*, FbxUInt64) override {
    error_ = 1;
    return 0;
  }
  std::size_t Read(void* data, FbxUInt64 size) const override {
    if (!open_ || (size && !data)) {
      error_ = 1;
      return 0;
    }
    const auto count = static_cast<std::size_t>(
        std::min<FbxUInt64>(size, bytes_.size() - position_));
    if (count) std::memcpy(data, bytes_.data() + position_, count);
    position_ += count;
    return count;
  }
  int GetReaderID() const override { return -1; }
  int GetWriterID() const override { return -1; }
  void Seek(const FbxInt64& offset, const FbxFile::ESeekPos& origin) override {
    const auto base = origin == FbxFile::eBegin ? 0
                      : origin == FbxFile::eCurrent
                          ? static_cast<FbxInt64>(position_)
                          : static_cast<FbxInt64>(bytes_.size());
    if (offset < -base ||
        offset > static_cast<FbxInt64>(bytes_.size()) - base) {
      error_ = 1;
      return;
    }
    position_ = static_cast<std::size_t>(base + offset);
  }
  FbxInt64 GetPosition() const override {
    return static_cast<FbxInt64>(position_);
  }
  void SetPosition(FbxInt64 position) override {
    Seek(position, FbxFile::eBegin);
  }
  int GetError() const override { return error_; }
  void ClearError() override { error_ = 0; }

 private:
  const std::vector<std::byte>& bytes_;
  mutable std::size_t position_ = 0;
  mutable int error_ = 0;
  bool open_ = false;
};

std::vector<std::byte> ReadBytes(const std::filesystem::path& input,
                                 const FbxImportOptions& options) {
  std::ifstream stream(input, std::ios::binary | std::ios::ate);
  Require(stream.is_open(), "fbx.io", "Cannot open input");
  const auto length = stream.tellg();
  Require(length >= 0, "fbx.io", "Cannot measure input");
  const auto size = static_cast<std::uintmax_t>(length);
  Require(size <= options.max_input_bytes &&
              size <= static_cast<std::uintmax_t>(
                          std::numeric_limits<std::streamsize>::max()) &&
              size <= static_cast<std::uintmax_t>(
                          std::numeric_limits<FbxInt64>::max()),
          "fbx.limit", "Input byte limit");
  Require(size != 0, "fbx.malformed", "Empty input");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(size));
  Require(static_cast<bool>(stream), "fbx.io", "Cannot read complete input");
  return bytes;
}

template <class Vector>
bool IsXyz(const Vector& vector, double value) {
  return vector[0] == value && vector[1] == value && vector[2] == value;
}
void CheckNode(FbxNode& node) {
  Require(IsXyz(node.LclTranslation.Get(), 0) &&
              IsXyz(node.LclRotation.Get(), 0) &&
              IsXyz(node.LclScaling.Get(), 1),
          "fbx.unsupported_transform", "Local node transform");
  for (const auto pivot : {FbxNode::eSourcePivot, FbxNode::eDestinationPivot}) {
    Require(IsXyz(node.GetGeometricTranslation(pivot), 0) &&
                IsXyz(node.GetGeometricRotation(pivot), 0) &&
                IsXyz(node.GetGeometricScaling(pivot), 1) &&
                IsXyz(node.GetRotationOffset(pivot), 0) &&
                IsXyz(node.GetRotationPivot(pivot), 0) &&
                IsXyz(node.GetScalingOffset(pivot), 0) &&
                IsXyz(node.GetScalingPivot(pivot), 0) &&
                IsXyz(node.GetPreRotation(pivot), 0) &&
                IsXyz(node.GetPostRotation(pivot), 0),
            "fbx.unsupported_transform", "Geometric/pivot node transform");
  }
  Require(node.GetMaterialCount() == 0, "fbx.unsupported_material",
          "Node material");
  Require(node.GetNodeAttributeCount() <= 1, "fbx.unsupported_geometry",
          "Multiple node attributes");
}
void CheckProperties(FbxObject& object) {
  for (auto property = object.GetFirstProperty(); property.IsValid();
       property = object.GetNextProperty(property)) {
    Require(!property.GetFlag(FbxPropertyFlags::eUserDefined),
            "fbx.unsupported_property", "User-defined node/mesh property");
  }
}
void CheckLayers(FbxMesh& mesh) {
  for (int layer_index = 0; layer_index < mesh.GetLayerCount(); ++layer_index) {
    const auto* layer = mesh.GetLayer(layer_index);
    Require(layer != nullptr, "fbx.malformed", "Null mesh layer");
    for (int type = FbxLayerElement::eNormal;
         type < FbxLayerElement::eTypeCount; ++type) {
      const auto element_type = static_cast<FbxLayerElement::EType>(type);
      if (element_type != FbxLayerElement::eNormal &&
          element_type != FbxLayerElement::eUV) {
        Require(layer->GetLayerElementOfType(element_type) == nullptr,
                "fbx.unsupported_attribute", "Unsupported layer element");
      }
    }
  }
}

template <class Element>
void CopyAttribute(Element& element, bool normal, std::uint32_t set_index,
                   FbxPolygonMesh& output, const FbxImportOptions& options) {
  FbxAttributeSource source;
  source.name = element.GetName();
  source.semantic = normal ? "normal" : "texcoord";
  source.components = normal ? 4 : 2;
  geometry::Attribute attribute;
  attribute.name = source.semantic + ":" + std::to_string(set_index);
  attribute.semantic = source.semantic;
  attribute.set_index = set_index;
  attribute.components = normal ? 3 : 2;
  std::size_t rows = 0;
  switch (element.GetMappingMode()) {
    case FbxLayerElement::eByControlPoint:
      source.mapping = FbxMapping::kByControlPoint;
      attribute.domain = geometry::AttributeDomain::vertex;
      rows = output.mesh.positions.size();
      break;
    case FbxLayerElement::eByPolygonVertex:
      source.mapping = FbxMapping::kByPolygonVertex;
      attribute.domain = geometry::AttributeDomain::corner;
      rows = output.mesh.corner_vertices.size();
      break;
    case FbxLayerElement::eByPolygon:
      source.mapping = FbxMapping::kByPolygon;
      attribute.domain = geometry::AttributeDomain::face;
      rows = static_cast<std::size_t>(output.mesh.face_count());
      break;
    case FbxLayerElement::eAllSame:
      source.mapping = FbxMapping::kAllSame;
      attribute.domain = geometry::AttributeDomain::face;
      rows = static_cast<std::size_t>(output.mesh.face_count());
      break;
    default:
      Fail("fbx.unsupported_mapping", "Normal/UV mapping mode");
  }
  const bool indexed =
      element.GetReferenceMode() == FbxLayerElement::eIndexToDirect;
  Require(normal || indexed, "fbx.unsupported_mapping",
          "Direct UV references are outside the evaluated import subset");
  Require(indexed || element.GetReferenceMode() == FbxLayerElement::eDirect,
          "fbx.unsupported_mapping", "Normal/UV reference mode");
  const auto& direct = element.GetDirectArray();
  const int direct_count = direct.GetCount();
  const int index_count = indexed ? element.GetIndexArray().GetCount() : 0;
  Require(direct_count >= 0 && index_count >= 0, "fbx.malformed",
          "Negative attribute count");
  Require(static_cast<std::uint32_t>(direct_count) <=
                  options.max_attribute_table_rows &&
              static_cast<std::uint32_t>(index_count) <=
                  options.max_attribute_table_rows,
          "fbx.limit", "Attribute table limit");
  const std::size_t mapping_rows =
      source.mapping == FbxMapping::kAllSame ? 1 : rows;
  switch (element.GetReferenceMode()) {
    case FbxLayerElement::eDirect:
      source.reference = FbxReference::kDirect;
      Require(static_cast<std::size_t>(direct_count) >= mapping_rows,
              "fbx.malformed_attribute", "Short direct attribute table");
      break;
    case FbxLayerElement::eIndexToDirect:
      source.reference = FbxReference::kIndexToDirect;
      Require(static_cast<std::size_t>(index_count) == mapping_rows,
              "fbx.malformed_attribute", "Attribute index cardinality");
      break;
    default:
      Fail("fbx.unsupported_mapping", "Normal/UV reference mode");
  }
  Require(static_cast<std::size_t>(direct_count) <=
                  std::numeric_limits<std::size_t>::max() / source.components &&
              rows <= std::numeric_limits<std::size_t>::max() /
                          attribute.components,
          "fbx.limit", "Attribute scalar extent");
  source.direct_values.reserve(static_cast<std::size_t>(direct_count) *
                               source.components);
  for (int i = 0; i < direct_count; ++i) {
    const auto value = direct.GetAt(i);
    for (std::uint32_t component = 0; component < source.components;
         ++component)
      source.direct_values.push_back(value[component]);
  }
  source.indices.reserve(static_cast<std::size_t>(index_count));
  for (int i = 0; i < index_count; ++i) {
    const int index = element.GetIndexArray().GetAt(i);
    if (source.reference == FbxReference::kIndexToDirect)
      Require(index >= 0 && index < direct_count, "fbx.malformed_attribute",
              "Attribute index range");
    source.indices.push_back(index);
  }
  std::vector<double> values;
  values.reserve(rows * attribute.components);
  for (std::size_t row = 0; row < rows; ++row) {
    const auto mapped_row = source.mapping == FbxMapping::kAllSame ? 0 : row;
    const auto direct_row =
        source.reference == FbxReference::kDirect
            ? mapped_row
            : static_cast<std::size_t>(source.indices[mapped_row]);
    for (std::uint32_t component = 0; component < attribute.components;
         ++component)
      values.push_back(
          source.direct_values[direct_row * source.components + component]);
  }
  attribute.values = std::move(values);
  attribute.metadata.emplace("fbx.element_name", source.name);
  output.mesh.attributes.push_back(std::move(attribute));
  output.attribute_sources.push_back(std::move(source));
}

void CopyMesh(FbxMesh& mesh, FbxPolygonMesh& output,
              const FbxImportOptions& options) {
  CheckProperties(mesh);
  Require(mesh.GetDeformerCount() == 0 && mesh.GetShapeCount() == 0,
          "fbx.unsupported_deformer", "Skin/blend shape/other deformer");
  Require(mesh.GetNodeCount() == 1, "fbx.unsupported_instance",
          "Mesh shared by multiple nodes");
  CheckLayers(mesh);
  const int points = mesh.GetControlPointsCount();
  const int polygons = mesh.GetPolygonCount();
  Require(points >= 0 && polygons >= 0, "fbx.malformed", "Negative mesh count");
  Require(static_cast<std::uint32_t>(points) <= options.max_control_points,
          "fbx.limit", "Control point count");
  output.mesh.positions.reserve(static_cast<std::size_t>(points));
  for (int point = 0; point < points; ++point) {
    const auto value = mesh.GetControlPointAt(point);
    output.mesh.positions.Append({value[0], value[1], value[2]});
  }
  for (int polygon = 0; polygon < polygons; ++polygon) {
    const int size = mesh.GetPolygonSize(polygon);
    Require(size >= 3, "fbx.malformed_polygon",
            "Polygon has fewer than three corners");
    Require(static_cast<std::size_t>(size) <=
                options.max_corners - output.mesh.corner_vertices.size(),
            "fbx.limit", "Corner count");
    for (int corner = 0; corner < size; ++corner) {
      const int vertex = mesh.GetPolygonVertex(polygon, corner);
      Require(vertex >= 0 && vertex < points, "fbx.malformed_polygon",
              "Control point index range");
      output.mesh.corner_vertices.push_back(
          static_cast<geometry::index_t>(vertex));
    }
    output.mesh.face_offsets.push_back(output.mesh.corner_vertices.size());
  }
  const int normals = mesh.GetElementNormalCount();
  const int uvs = mesh.GetElementUVCount();
  Require(normals >= 0 && uvs >= 0 &&
              static_cast<std::uint64_t>(normals) +
                      static_cast<std::uint64_t>(uvs) <=
                  options.max_attribute_elements,
          "fbx.limit", "Attribute element count");
  for (int i = 0; i < normals; ++i) {
    auto* element = mesh.GetElementNormal(i);
    Require(element != nullptr, "fbx.malformed_attribute",
            "Null normal element");
    CopyAttribute(*element, true, static_cast<std::uint32_t>(i), output,
                  options);
  }
  for (int i = 0; i < uvs; ++i) {
    auto* element = mesh.GetElementUV(i);
    Require(element != nullptr, "fbx.malformed_attribute", "Null UV element");
    CopyAttribute(*element, false, static_cast<std::uint32_t>(i), output,
                  options);
  }
  Require(geometry::inspect_storage(output.mesh).empty(), "fbx.malformed",
          "Invalid converted mesh storage");
}

FbxAsset Import(std::vector<std::byte> bytes, const FbxImportOptions& options) {
  // Serialize manager creation too: the SDK uses process-level state
  // internally.
  const std::lock_guard lock(SdkMutex());
  InputStream stream(bytes);
  std::unique_ptr<FbxManager, ManagerDeleter> manager(FbxManager::Create());
  if (!manager) throw std::bad_alloc();
  Require(std::string(FbxManager::GetVersion(false)) == FBXSDK_VERSION_STRING,
          "fbx.dependency", "SDK runtime/header version mismatch");
  auto* settings = FbxIOSettings::Create(manager.get(), IOSROOT);
  auto* scene = FbxScene::Create(manager.get(), "MeshvaleImport");
  auto* importer = FbxImporter::Create(manager.get(), "MeshvaleReader");
  if (!settings || !scene || !importer) throw std::bad_alloc();
  manager->SetIOSettings(settings);
  settings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
  const int reader =
      manager->GetIOPluginRegistry()->FindReaderIDByExtension("fbx");
  Require(reader >= 0, "fbx.dependency", "SDK FBX reader missing");
  if (!importer->Initialize(&stream, nullptr, reader, settings))
    Fail("fbx.malformed", std::string("SDK initialization: ") +
                              importer->GetStatus().GetErrorString());
  Require(importer->IsFBX(), "fbx.malformed", "Input is not FBX");
  if (!importer->Import(scene))
    Fail("fbx.malformed",
         std::string("SDK import: ") + importer->GetStatus().GetErrorString());
  importer->Destroy();
  Require(scene->GetSrcObjectCount<FbxAnimStack>() == 0 &&
              scene->GetSrcObjectCount<FbxAnimLayer>() == 0 &&
              scene->GetSrcObjectCount<FbxAnimCurve>() == 0,
          "fbx.unsupported_animation", "Animation data");
  Require(scene->GetSrcObjectCount<FbxSurfaceMaterial>() == 0 &&
              scene->GetSrcObjectCount<FbxTexture>() == 0 &&
              scene->GetSrcObjectCount<FbxVideo>() == 0,
          "fbx.unsupported_material", "Materials/textures/videos");
  Require(scene->GetPoseCount() == 0, "fbx.unsupported_pose", "Scene pose");
  FbxAsset asset;
  const auto axis = scene->GetGlobalSettings().GetAxisSystem();
  asset.source_space.up_axis =
      static_cast<int>(axis.GetUpVector(asset.source_space.up_sign));
  asset.source_space.front_parity =
      static_cast<int>(axis.GetFrontVector(asset.source_space.front_sign));
  asset.source_space.right_handed =
      axis.GetCoorSystem() == FbxAxisSystem::eRightHanded;
  asset.source_space.centimeters_per_unit =
      scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor();
  Require(std::isfinite(asset.source_space.centimeters_per_unit) &&
              asset.source_space.centimeters_per_unit > 0,
          "fbx.malformed", "Invalid source unit scale");
  struct NodeWork {
    FbxNode* node;
    std::vector<std::string> names;
    std::vector<std::uint32_t> indices;
  };
  auto* root = scene->GetRootNode();
  Require(root != nullptr, "fbx.malformed", "Missing scene root");
  std::vector<NodeWork> pending{{root, {}, {}}};
  std::uint64_t node_count = 0;
  while (!pending.empty()) {
    auto work = std::move(pending.back());
    pending.pop_back();
    Require(++node_count <= options.max_nodes &&
                work.indices.size() <= options.max_node_depth,
            "fbx.limit", "Node count/depth");
    CheckNode(*work.node);
    CheckProperties(*work.node);
    auto* attribute = work.node->GetNodeAttribute();
    if (attribute && attribute->GetAttributeType() != FbxNodeAttribute::eNull) {
      Require(attribute->GetAttributeType() == FbxNodeAttribute::eMesh,
              "fbx.unsupported_geometry", "Nonpolygon node geometry");
      Require(asset.meshes.size() < options.max_meshes, "fbx.limit",
              "Mesh count");
      FbxPolygonMesh output;
      output.node_names = work.names;
      output.child_indices = work.indices;
      CopyMesh(*work.node->GetMesh(), output, options);
      asset.meshes.push_back(std::move(output));
    }
    const int children = work.node->GetChildCount();
    Require(children >= 0 && static_cast<std::uint64_t>(children) + node_count +
                                     pending.size() <=
                                 options.max_nodes,
            "fbx.limit", "Node count");
    for (int child = children; child-- > 0;) {
      auto* node = work.node->GetChild(child);
      Require(node != nullptr, "fbx.malformed", "Null child node");
      auto names = work.names;
      auto indices = work.indices;
      names.emplace_back(node->GetName());
      indices.push_back(static_cast<std::uint32_t>(child));
      pending.push_back({node, std::move(names), std::move(indices)});
    }
  }
  asset.source_bytes = std::move(bytes);
  return asset;
}

}  // namespace

FbxImportResult ReadFbxFile(const std::filesystem::path& input,
                            const FbxImportOptions& options) {
  try {
    return {Import(ReadBytes(input, options), options), {}};
  } catch (const ImportFailure& failure) {
    return {std::nullopt, {{failure.code, failure.subject, std::nullopt}}};
  }
}

}  // namespace meshvale::interchange
