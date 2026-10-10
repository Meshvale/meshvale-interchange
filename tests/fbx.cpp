// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/fbx.h"

#include <fbxsdk.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace interchange = meshvale::interchange;
namespace geometry = meshvale::geometry;
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct DestroyManager {
  void operator()(FbxManager* manager) const {
    if (manager) manager->Destroy();
  }
};
enum class Fixture {
  kNoAttributes,
  kPlain,
  kTransformed,
  kGeometric,
  kPivot,
  kParent,
  kAnimation,
  kSkin,
  kMaterial,
  kTexture,
  kUnsupportedMapping,
  kInvalidIndex,
  kShortIndices,
  kColor,
  kProperty,
  kCamera,
  kInstance,
  kDuplicateNames
};

void AddUv(FbxMesh& mesh, const char* name,
           FbxLayerElement::EMappingMode mapping,
           FbxLayerElement::EReferenceMode reference, int rows) {
  auto* uv = mesh.CreateElementUV(name);
  uv->SetMappingMode(mapping);
  uv->SetReferenceMode(reference);
  for (int i = 0; i < rows + 1; ++i)
    uv->GetDirectArray().Add(FbxVector2(i * 0.25, -i * 0.5));
  if (reference == FbxLayerElement::eIndexToDirect)
    for (int i = 0; i < rows; ++i) uv->GetIndexArray().Add(i);
}
void WriteFixture(const std::filesystem::path& path, Fixture kind,
                  bool ascii = false) {
  std::unique_ptr<FbxManager, DestroyManager> manager(FbxManager::Create());
  Check(static_cast<bool>(manager), "Fixture manager");
  manager->SetIOSettings(FbxIOSettings::Create(manager.get(), IOSROOT));
  auto* scene = FbxScene::Create(manager.get(), "OriginalTestFixture");
  scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::m);
  scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem::MayaZUp);
  auto* group = FbxNode::Create(scene, "Group");
  scene->GetRootNode()->AddChild(group);
  auto* node = FbxNode::Create(scene, "Mixed");
  group->AddChild(node);
  auto* mesh = FbxMesh::Create(scene, "MixedMesh");
  node->SetNodeAttribute(mesh);
  mesh->InitControlPoints(6);
  const std::array<std::array<double, 3>, 6> points{
      {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 1, 0}, {-1, 0, 0}}};
  for (int i = 0; i < 6; ++i)
    mesh->SetControlPointAt(
        FbxVector4(points[i][0], points[i][1], points[i][2]), i);
  for (const auto& face :
       {std::vector<int>{0, 1, 2, 3}, std::vector<int>{0, 3, 4},
        std::vector<int>{0, 1, 2, 4, 5}}) {
    mesh->BeginPolygon();
    for (int vertex : face) mesh->AddPolygon(vertex);
    mesh->EndPolygon();
  }
  auto* normals = mesh->CreateElementNormal();
  normals->SetMappingMode(FbxLayerElement::eByPolygonVertex);
  normals->SetReferenceMode(FbxLayerElement::eIndexToDirect);
  normals->GetDirectArray().Add(FbxVector4(0, 0, 2, 0));
  normals->GetDirectArray().Add(FbxVector4(0, 0, -3, 0));
  normals->GetDirectArray().Add(FbxVector4(0, 4, 0, 0));
  for (int i = 0; i < 12; ++i) normals->GetIndexArray().Add(i % 2);
  AddUv(*mesh, "Seams", FbxLayerElement::eByPolygonVertex,
        FbxLayerElement::eIndexToDirect, 12);
  AddUv(*mesh, "VertexUv", FbxLayerElement::eByControlPoint,
        FbxLayerElement::eIndexToDirect, 6);
  AddUv(*mesh, "FaceUv", FbxLayerElement::eByPolygon,
        FbxLayerElement::eIndexToDirect, 3);
  AddUv(*mesh, "ConstantUv", FbxLayerElement::eAllSame,
        FbxLayerElement::eIndexToDirect, 1);
  switch (kind) {
    case Fixture::kNoAttributes:
      mesh->RemoveElementNormal(normals);
      while (mesh->GetElementUVCount())
        mesh->RemoveElementUV(mesh->GetElementUV());
      break;
    case Fixture::kPlain:
      break;
    case Fixture::kTransformed:
      node->LclTranslation.Set(FbxDouble3(2, 0, 0));
      break;
    case Fixture::kGeometric:
      node->SetGeometricScaling(FbxNode::eSourcePivot, FbxVector4(2, 1, 1));
      break;
    case Fixture::kPivot:
      node->SetRotationPivot(FbxNode::eSourcePivot, FbxVector4(1, 0, 0));
      break;
    case Fixture::kParent:
      group->LclTranslation.Set(FbxDouble3(1, 2, 3));
      break;
    case Fixture::kAnimation: {
      auto* stack = FbxAnimStack::Create(scene, "Take");
      auto* layer = FbxAnimLayer::Create(scene, "Layer");
      stack->AddMember(layer);
      auto* curve = node->LclTranslation.GetCurve(
          layer, FBXSDK_CURVENODE_COMPONENT_X, true);
      FbxTime time;
      time.SetSecondDouble(0);
      const auto key = curve->KeyAdd(time);
      curve->KeySetValue(key, 0);
      time.SetSecondDouble(1);
      curve->KeySetValue(curve->KeyAdd(time), 1);
      break;
    }
    case Fixture::kSkin: {
      auto* skin = FbxSkin::Create(scene, "Skin");
      auto* cluster = FbxCluster::Create(scene, "Cluster");
      cluster->SetLink(group);
      cluster->SetLinkMode(FbxCluster::eNormalize);
      cluster->AddControlPointIndex(0, 0.3);
      skin->AddCluster(cluster);
      mesh->AddDeformer(skin);
      break;
    }
    case Fixture::kMaterial:
      node->AddMaterial(FbxSurfacePhong::Create(scene, "Material"));
      break;
    case Fixture::kTexture: {
      auto* material = FbxSurfacePhong::Create(scene, "Material");
      node->AddMaterial(material);
      auto* texture = FbxFileTexture::Create(scene, "Texture");
      const auto resource = path.parent_path() / "texture.bin";
      {
        std::ofstream file(resource, std::ios::binary);
        file << "Original fixture resource";
      }
      texture->SetFileName(resource.string().c_str());
      material->Diffuse.ConnectSrcObject(texture);
      manager->GetIOSettings()->SetBoolProp(EXP_FBX_EMBEDDED, true);
      break;
    }
    case Fixture::kUnsupportedMapping:
      mesh->GetElementUV(0)->SetMappingMode(FbxLayerElement::eByEdge);
      break;
    case Fixture::kInvalidIndex:
      mesh->GetElementUV(0)->GetIndexArray().SetAt(0, 999);
      break;
    case Fixture::kShortIndices:
      mesh->GetElementUV(0)->GetIndexArray().RemoveLast();
      break;
    case Fixture::kColor: {
      auto* color = mesh->CreateElementVertexColor();
      color->SetMappingMode(FbxLayerElement::eAllSame);
      color->SetReferenceMode(FbxLayerElement::eDirect);
      color->GetDirectArray().Add(FbxColor(1, 0, 0));
      break;
    }
    case Fixture::kProperty:
      FbxProperty::Create(node, FbxDoubleDT, "UserValue")
          .ModifyFlag(FbxPropertyFlags::eUserDefined, true);
      break;
    case Fixture::kCamera: {
      auto* camera = FbxNode::Create(scene, "Camera");
      camera->SetNodeAttribute(FbxCamera::Create(scene, "CameraAttribute"));
      scene->GetRootNode()->AddChild(camera);
      break;
    }
    case Fixture::kInstance: {
      auto* another = FbxNode::Create(scene, "Instance");
      another->SetNodeAttribute(mesh);
      group->AddChild(another);
      break;
    }
    case Fixture::kDuplicateNames: {
      auto* another = FbxNode::Create(scene, "Mixed");
      another->SetNodeAttribute(
          static_cast<FbxMesh*>(mesh->Clone(FbxObject::eDeepClone, scene)));
      group->AddChild(another);
      break;
    }
  }
  auto* exporter = FbxExporter::Create(manager.get(), "Exporter");
  int writer = -1;
  if (ascii) {
    auto* registry = manager->GetIOPluginRegistry();
    for (int i = 0; i < registry->GetWriterFormatCount(); ++i) {
      if (registry->WriterIsFBX(i) &&
          std::string(registry->GetWriterFormatDescription(i)).find("ascii") !=
              std::string::npos) {
        writer = i;
        break;
      }
    }
    Check(writer >= 0, "ASCII writer missing");
  }
  Check(exporter->Initialize(path.string().c_str(), writer,
                             manager->GetIOSettings()),
        "Fixture exporter init");
  Check(exporter->Export(scene), "Fixture export");
}

std::vector<std::byte> FileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  const auto size = file.tellg();
  Check(size >= 0, "Fixture byte size");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  Check(static_cast<bool>(file), "Fixture byte read");
  return bytes;
}
void Verify(const interchange::FbxAsset& asset) {
  Check(asset.meshes.size() == 1, "Mesh count");
  const auto& polygon = asset.meshes[0];
  Check(polygon.node_names == std::vector<std::string>{"Group", "Mixed"} &&
            polygon.child_indices == std::vector<std::uint32_t>{0, 0},
        "Node identity");
  const auto& mesh = polygon.mesh;
  Check(mesh.positions.size() == 6 &&
            mesh.face_offsets == std::vector<geometry::index_t>{0, 4, 7, 12},
        "Mixed polygon sizes");
  Check(mesh.corner_vertices ==
            std::vector<geometry::index_t>{0, 1, 2, 3, 0, 3, 4, 0, 1, 2, 4, 5},
        "Corner ordering");
  Check(mesh.positions.Get(4) == std::array<double, 3>{-1, 1, 0},
        "Source coordinates");
  Check(asset.source_space.centimeters_per_unit == 100 &&
            asset.source_space.up_axis == 3,
        "Source unit/axis retention");
  Check(mesh.attributes.size() == 5 && polygon.attribute_sources.size() == 5,
        "All normal/UV elements retained");
  Check(mesh.attributes[0].domain == geometry::AttributeDomain::corner &&
            mesh.attributes[0].components == 3,
        "Normal corner domain");
  const auto& normals =
      std::get<std::vector<double>>(mesh.attributes[0].values);
  Check(normals[2] == 2 && normals[5] == -3, "Normals not normalized");
  const auto& source = polygon.attribute_sources[0];
  Check(source.direct_values ==
            std::vector<double>{0, 0, 2, 0, 0, 0, -3, 0, 0, 4, 0, 0},
        "Unused normal source row and fourth components");
  Check(source.indices.size() == 12 && source.indices[1] == 1,
        "Normal source indices");
  const auto& uv = std::get<std::vector<double>>(mesh.attributes[1].values);
  Check(uv[0] == 0 && uv[8] == 1 && uv[14] == 1.75,
        "Independent UV corner seams");
  Check(polygon.attribute_sources[1].direct_values.size() == 26 &&
            polygon.attribute_sources[1].indices.size() == 12,
        "Unused UV source row");
  Check(mesh.attributes[2].domain == geometry::AttributeDomain::vertex &&
            mesh.attributes[3].domain == geometry::AttributeDomain::face,
        "Vertex/face attribute domains");
  Check(std::get<std::vector<double>>(mesh.attributes[4].values).size() == 6 &&
            polygon.attribute_sources[4].mapping ==
                interchange::FbxMapping::kAllSame,
        "All-same attribute expansion");
  Check(geometry::inspect_storage(mesh).empty(), "Storage validation");
}
std::vector<std::pair<std::filesystem::path, std::vector<std::byte>>>
DirectoryEntries(const std::filesystem::path& path) {
  std::vector<std::pair<std::filesystem::path, std::vector<std::byte>>> entries;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
    entries.emplace_back(entry.path(), entry.is_regular_file()
                                           ? FileBytes(entry.path())
                                           : std::vector<std::byte>{});
  std::sort(entries.begin(), entries.end());
  return entries;
}
void Reject(const std::filesystem::path& path, const std::string& code) {
  const auto result = interchange::ReadFbxFile(path);
  if (result.asset || result.diagnostics.empty() ||
      result.diagnostics[0].code != code) {
    std::cerr << path << " expected " << code << "; actual ";
    for (const auto& diagnostic : result.diagnostics)
      std::cerr << diagnostic.code << ' ' << diagnostic.subject;
    std::cerr << '\n';
    throw std::runtime_error("Unsupported input diagnostic");
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Check(argc == 2, "Fixture directory required");
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    for (bool ascii : {false, true}) {
      const auto path = root / (ascii ? "mixed-ascii.fbx" : "mixed.fbx");
      WriteFixture(path, Fixture::kPlain, ascii);
      const auto result = interchange::ReadFbxFile(path);
      if (!result.asset) {
        for (const auto& d : result.diagnostics)
          std::cerr << d.code << ' ' << d.subject << '\n';
      }
      Check(result.asset.has_value(), "Supported binary/ASCII import");
      Verify(*result.asset);
      Check(result.asset->source_bytes == FileBytes(path),
            "Exact original file byte ownership");
    }
    const auto good = root / "mixed.fbx";
    const auto ascii_bytes = FileBytes(root / "mixed-ascii.fbx");
    std::string direct_text(reinterpret_cast<const char*>(ascii_bytes.data()),
                            ascii_bytes.size());
    std::string normal_text = direct_text;
    const auto normal_begin = normal_text.find("LayerElementNormal: 0 {");
    const auto normal_reference = normal_text.find(
        "ReferenceInformationType: \"IndexToDirect\"", normal_begin);
    Check(normal_begin != std::string::npos &&
              normal_reference != std::string::npos,
          "Normal ASCII fixture");
    normal_text.replace(
        normal_reference,
        std::string("ReferenceInformationType: \"IndexToDirect\"").size(),
        "ReferenceInformationType: \"Direct\"");
    const auto normal_values = normal_text.find("Normals: ", normal_begin);
    const auto normal_end = normal_text.find('}', normal_values);
    Check(normal_values != std::string::npos && normal_end != std::string::npos,
          "Normal ASCII values");
    std::string normals_text = "Normals: *36 {\n\t\t\t\ta: ";
    for (int i = 0; i < 12; ++i) {
      if (i) normals_text += ',';
      normals_text += i % 2 ? "0,0,-3" : "0,0,2";
    }
    normals_text += "\n\t\t\t}";
    normal_text.replace(normal_values, normal_end - normal_values + 1,
                        normals_text);
    const auto normal_w = normal_text.find("NormalsW:", normal_values);
    const auto normal_w_end = normal_text.find('}', normal_w);
    Check(normal_w != std::string::npos && normal_w_end != std::string::npos,
          "Normal fourth-component fixture");
    normal_text.replace(
        normal_w, normal_w_end - normal_w + 1,
        "NormalsW: *12 {\n\t\t\t\ta: 0,0,0,0,0,0,0,0,0,0,0,0\n\t\t\t}");
    const auto normal_index = normal_text.find("NormalsIndex:", normal_values);
    const auto normal_index_end = normal_text.find('}', normal_index);
    Check(normal_index != std::string::npos &&
              normal_index_end != std::string::npos,
          "Normal index fixture");
    normal_text.erase(normal_index, normal_index_end - normal_index + 1);
    const auto direct_normals = root / "direct-normals.fbx";
    {
      std::ofstream file(direct_normals, std::ios::binary);
      file << normal_text;
    }
    const auto direct_normal_result = interchange::ReadFbxFile(direct_normals);
    Check(direct_normal_result.asset &&
              direct_normal_result.asset->meshes[0]
                      .attribute_sources[0]
                      .reference == interchange::FbxReference::kDirect,
          "Direct normal reference");
    Check(std::get<std::vector<double>>(direct_normal_result.asset->meshes[0]
                                            .mesh.attributes[0]
                                            .values)[5] == -3,
          "Direct normal value preservation");
    // The SDK exporter omits direct UV tables. Derive an original ASCII fixture
    // with valid direct tables from our indexed fixture.
    for (int uv = 1; uv <= 2; ++uv) {
      const auto begin =
          direct_text.find("LayerElementUV: " + std::to_string(uv) + " {");
      Check(begin != std::string::npos, "Original UV fixture element");
      const auto reference = direct_text.find(
          "ReferenceInformationType: \"IndexToDirect\"", begin);
      Check(reference != std::string::npos, "Original UV fixture reference");
      direct_text.replace(
          reference,
          std::string("ReferenceInformationType: \"IndexToDirect\"").size(),
          "ReferenceInformationType: \"Direct\"");
      const auto index = direct_text.find("UVIndex:", begin);
      const auto index_end = direct_text.find('}', index);
      Check(index != std::string::npos && index_end != std::string::npos,
            "UV index fixture");
      direct_text.erase(index, index_end - index + 1);
    }
    const auto direct_uv = root / "direct-uv.fbx";
    {
      std::ofstream file(direct_uv, std::ios::binary);
      file << direct_text;
    }
    Reject(direct_uv, "fbx.unsupported_mapping");
    const auto no_attributes = root / "no-attributes.fbx";
    WriteFixture(no_attributes, Fixture::kNoAttributes);
    const auto no_attribute_result = interchange::ReadFbxFile(no_attributes);
    Check(no_attribute_result.asset &&
              no_attribute_result.asset->meshes[0].mesh.attributes.empty() &&
              no_attribute_result.asset->meshes[0].attribute_sources.empty(),
          "Missing elements stay absent");
    auto retained = interchange::ReadFbxFile(good);
    Check(retained.asset.has_value(), "Owned import before source removal");
    const auto renamed = root / "source-removed.fbx";
    std::filesystem::rename(good, renamed);
    Verify(*retained.asset);
    std::filesystem::rename(renamed, good);
    const std::vector<std::pair<Fixture, std::string>> cases{
        {Fixture::kTransformed, "fbx.unsupported_transform"},
        {Fixture::kGeometric, "fbx.unsupported_transform"},
        {Fixture::kPivot, "fbx.unsupported_transform"},
        {Fixture::kParent, "fbx.unsupported_transform"},
        {Fixture::kAnimation, "fbx.unsupported_animation"},
        {Fixture::kSkin, "fbx.unsupported_deformer"},
        {Fixture::kMaterial, "fbx.unsupported_material"},
        {Fixture::kTexture, "fbx.unsupported_material"},
        {Fixture::kUnsupportedMapping, "fbx.unsupported_mapping"},
        {Fixture::kInvalidIndex, "fbx.malformed_attribute"},
        {Fixture::kShortIndices, "fbx.malformed_attribute"},
        {Fixture::kColor, "fbx.unsupported_attribute"},
        {Fixture::kProperty, "fbx.unsupported_property"},
        {Fixture::kCamera, "fbx.unsupported_geometry"},
        {Fixture::kInstance, "fbx.unsupported_instance"}};
    int case_index = 0;
    for (const auto& [fixture, code] : cases) {
      const auto path =
          root / ("reject-" + std::to_string(case_index++) + ".fbx");
      WriteFixture(path, fixture);
      const auto before = DirectoryEntries(root);
      Reject(path, code);
      Check(DirectoryEntries(root) == before,
            "Import must not extract embedded resources or write source "
            "directory");
    }
    const auto duplicate = root / "duplicate.fbx";
    WriteFixture(duplicate, Fixture::kDuplicateNames);
    const auto names = interchange::ReadFbxFile(duplicate);
    Check(names.asset && names.asset->meshes.size() == 2,
          "Duplicate node names supported");
    Check(names.asset->meshes[0].node_names ==
                  names.asset->meshes[1].node_names &&
              names.asset->meshes[0].child_indices !=
                  names.asset->meshes[1].child_indices,
          "Child path distinguishes duplicate names");
    const auto malformed = root / "malformed.fbx";
    {
      std::ofstream file(malformed);
      file << "not an FBX file";
    }
    Reject(malformed, "fbx.malformed");
    Reject(root / "missing.fbx", "fbx.io");
    for (int limit = 0; limit < 8; ++limit) {
      interchange::FbxImportOptions options;
      switch (limit) {
        case 0:
          options.max_input_bytes = FileBytes(good).size() - 1;
          break;
        case 1:
          options.max_nodes = 2;
          break;
        case 2:
          options.max_node_depth = 1;
          break;
        case 3:
          options.max_meshes = 0;
          break;
        case 4:
          options.max_control_points = 5;
          break;
        case 5:
          options.max_corners = 11;
          break;
        case 6:
          options.max_attribute_table_rows = 12;
          break;
        case 7:
          options.max_attribute_elements = 4;
          break;
      }
      const auto result = interchange::ReadFbxFile(good, options);
      Check(!result.asset && !result.diagnostics.empty() &&
                result.diagnostics[0].code == "fbx.limit",
            "Conversion limit rejection");
    }
    std::array<bool, 3> thread_results{};
    std::vector<std::jthread> threads;
    for (std::size_t i = 0; i < thread_results.size(); ++i)
      threads.emplace_back([&, i] {
        thread_results[i] = interchange::ReadFbxFile(good).asset.has_value();
      });
    threads.clear();
    Check(thread_results[0] && thread_results[1] && thread_results[2],
          "Serialized concurrent imports");
    for (int repeat = 0; repeat < 20; ++repeat) {
      Reject(malformed, "fbx.malformed");
      Check(interchange::ReadFbxFile(good).asset.has_value(),
            "Cleanup after repeated parse failure");
    }
    std::cout
        << "FBX polygon suite: binary/ASCII mixed polygons; 4 UV mappings; "
           "seams/normal source tables; 15 unsupported/malformed feature "
           "cases; 8 limit cases; direct normals/direct UV rejection; absent "
           "attributes; duplicate-name paths; source ownership/no "
           "extraction; 3 callers; 20 failure-cleanup repetitions\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
