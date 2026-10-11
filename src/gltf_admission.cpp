// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <string_view>

#include "gltf_metadata.h"

namespace meshvale::interchange::gltf_detail {
namespace {
enum class GuardKind {
  kObject,
  kArray,
  kString,
  kNumber,
  kInteger,
  kBoolean,
  kOpaque
};
constexpr std::uint32_t kNoGuard = std::numeric_limits<std::uint32_t>::max();
struct CoreRule {
  std::string_view path;
  std::string_view key;
  std::uint32_t parent;
  GuardKind kind;
  bool required;
  std::string_view minimum;
  std::string_view maximum;
  bool exclusive_minimum;
  std::uint64_t min_items;
  std::uint64_t max_items;
  std::uint64_t min_properties;
  std::uint64_t multiple_of;
  bool unique;
  std::string_view allowed;
};
// Original static guard vocabulary. Empty ranges have no intrinsic ceiling.
// Extensible enumeration syntax is separately classified as unsupported.
constexpr CoreRule kCoreRules[] = {
    {"", "", kNoGuard, GuardKind::kObject, false, "", "", false, 0, 0, 0, 0,
     false, ""},
    {"/extensions", "extensions", 0, GuardKind::kObject, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/extras", "extras", 0, GuardKind::kOpaque, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/extensionsUsed", "extensionsUsed", 0, GuardKind::kArray, false, "", "",
     false, 1, 0, 0, 0, true, ""},
    {"/extensionsUsed/*", "*", 3, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/extensionsRequired", "extensionsRequired", 0, GuardKind::kArray, false,
     "", "", false, 1, 0, 0, 0, true, ""},
    {"/extensionsRequired/*", "*", 5, GuardKind::kString, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/accessors", "accessors", 0, GuardKind::kArray, false, "", "", false, 1,
     0, 0, 0, false, ""},
    {"/accessors/*", "*", 7, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/accessors/*/extensions", "extensions", 8, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/extras", "extras", 8, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/name", "name", 8, GuardKind::kString, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/accessors/*/bufferView", "bufferView", 8, GuardKind::kInteger, false,
     "0", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/byteOffset", "byteOffset", 8, GuardKind::kInteger, false,
     "0", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/componentType", "componentType", 8, GuardKind::kInteger,
     true, "", "", false, 0, 0, 0, 0, false, "5120|5121|5122|5123|5125|5126"},
    {"/accessors/*/normalized", "normalized", 8, GuardKind::kBoolean, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/count", "count", 8, GuardKind::kInteger, true, "1", "",
     false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/type", "type", 8, GuardKind::kString, true, "", "", false, 0,
     0, 0, 0, false, "SCALAR|VEC2|VEC3|VEC4|MAT2|MAT3|MAT4"},
    {"/accessors/*/max", "max", 8, GuardKind::kArray, false, "", "", false, 1,
     16, 0, 0, false, ""},
    {"/accessors/*/max/*", "*", 18, GuardKind::kNumber, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/accessors/*/min", "min", 8, GuardKind::kArray, false, "", "", false, 1,
     16, 0, 0, false, ""},
    {"/accessors/*/min/*", "*", 20, GuardKind::kNumber, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/accessors/*/sparse", "sparse", 8, GuardKind::kObject, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/extensions", "extensions", 22, GuardKind::kObject,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/extras", "extras", 22, GuardKind::kOpaque, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/count", "count", 22, GuardKind::kInteger, true, "1",
     "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/indices", "indices", 22, GuardKind::kObject, true, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/indices/extensions", "extensions", 26,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/indices/extras", "extras", 26, GuardKind::kOpaque,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/indices/bufferView", "bufferView", 26,
     GuardKind::kInteger, true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/indices/byteOffset", "byteOffset", 26,
     GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/indices/componentType", "componentType", 26,
     GuardKind::kInteger, true, "", "", false, 0, 0, 0, 0, false,
     "5121|5123|5125"},
    {"/accessors/*/sparse/values", "values", 22, GuardKind::kObject, true, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/values/extensions", "extensions", 32,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/values/extras", "extras", 32, GuardKind::kOpaque,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/values/bufferView", "bufferView", 32,
     GuardKind::kInteger, true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/accessors/*/sparse/values/byteOffset", "byteOffset", 32,
     GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/animations", "animations", 0, GuardKind::kArray, false, "", "", false, 1,
     0, 0, 0, false, ""},
    {"/animations/*", "*", 37, GuardKind::kObject, false, "", "", false, 0, 0,
     0, 0, false, ""},
    {"/animations/*/extensions", "extensions", 38, GuardKind::kObject, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/extras", "extras", 38, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/animations/*/name", "name", 38, GuardKind::kString, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/animations/*/channels", "channels", 38, GuardKind::kArray, true, "", "",
     false, 1, 0, 0, 0, false, ""},
    {"/animations/*/channels/*", "*", 42, GuardKind::kObject, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/extensions", "extensions", 43,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/extras", "extras", 43, GuardKind::kOpaque, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/sampler", "sampler", 43, GuardKind::kInteger,
     true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/target", "target", 43, GuardKind::kObject, true,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/target/extensions", "extensions", 47,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/target/extras", "extras", 47, GuardKind::kOpaque,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/target/node", "node", 47, GuardKind::kInteger,
     false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/channels/*/target/path", "path", 47, GuardKind::kString,
     true, "", "", false, 0, 0, 0, 0, false,
     "translation|rotation|scale|weights"},
    {"/animations/*/samplers", "samplers", 38, GuardKind::kArray, true, "", "",
     false, 1, 0, 0, 0, false, ""},
    {"/animations/*/samplers/*", "*", 52, GuardKind::kObject, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/animations/*/samplers/*/extensions", "extensions", 53,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/samplers/*/extras", "extras", 53, GuardKind::kOpaque, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/samplers/*/input", "input", 53, GuardKind::kInteger, true,
     "0", "", false, 0, 0, 0, 0, false, ""},
    {"/animations/*/samplers/*/interpolation", "interpolation", 53,
     GuardKind::kString, false, "", "", false, 0, 0, 0, 0, false,
     "LINEAR|STEP|CUBICSPLINE"},
    {"/animations/*/samplers/*/output", "output", 53, GuardKind::kInteger, true,
     "0", "", false, 0, 0, 0, 0, false, ""},
    {"/asset", "asset", 0, GuardKind::kObject, true, "", "", false, 0, 0, 0, 0,
     false, ""},
    {"/asset/extensions", "extensions", 59, GuardKind::kObject, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/asset/extras", "extras", 59, GuardKind::kOpaque, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/asset/copyright", "copyright", 59, GuardKind::kString, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/asset/generator", "generator", 59, GuardKind::kString, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/asset/version", "version", 59, GuardKind::kString, true, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/asset/minVersion", "minVersion", 59, GuardKind::kString, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/buffers", "buffers", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0,
     0, false, ""},
    {"/buffers/*", "*", 66, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/buffers/*/extensions", "extensions", 67, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/buffers/*/extras", "extras", 67, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/buffers/*/name", "name", 67, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/buffers/*/uri", "uri", 67, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/buffers/*/byteLength", "byteLength", 67, GuardKind::kInteger, true, "1",
     "", false, 0, 0, 0, 0, false, ""},
    {"/bufferViews", "bufferViews", 0, GuardKind::kArray, false, "", "", false,
     1, 0, 0, 0, false, ""},
    {"/bufferViews/*", "*", 73, GuardKind::kObject, false, "", "", false, 0, 0,
     0, 0, false, ""},
    {"/bufferViews/*/extensions", "extensions", 74, GuardKind::kObject, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/bufferViews/*/extras", "extras", 74, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/bufferViews/*/name", "name", 74, GuardKind::kString, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/bufferViews/*/buffer", "buffer", 74, GuardKind::kInteger, true, "0", "",
     false, 0, 0, 0, 0, false, ""},
    {"/bufferViews/*/byteOffset", "byteOffset", 74, GuardKind::kInteger, false,
     "0", "", false, 0, 0, 0, 0, false, ""},
    {"/bufferViews/*/byteLength", "byteLength", 74, GuardKind::kInteger, true,
     "1", "", false, 0, 0, 0, 0, false, ""},
    {"/bufferViews/*/byteStride", "byteStride", 74, GuardKind::kInteger, false,
     "4", "252", false, 0, 0, 0, 4, false, ""},
    {"/bufferViews/*/target", "target", 74, GuardKind::kInteger, false, "", "",
     false, 0, 0, 0, 0, false, "34962|34963"},
    {"/cameras", "cameras", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0,
     0, false, ""},
    {"/cameras/*", "*", 83, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/cameras/*/extensions", "extensions", 84, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/extras", "extras", 84, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/name", "name", 84, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/cameras/*/orthographic", "orthographic", 84, GuardKind::kObject, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/orthographic/extensions", "extensions", 88, GuardKind::kObject,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/orthographic/extras", "extras", 88, GuardKind::kOpaque, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/orthographic/xmag", "xmag", 88, GuardKind::kNumber, true, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/orthographic/ymag", "ymag", 88, GuardKind::kNumber, true, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/orthographic/zfar", "zfar", 88, GuardKind::kNumber, true,
     "0.0", "", true, 0, 0, 0, 0, false, ""},
    {"/cameras/*/orthographic/znear", "znear", 88, GuardKind::kNumber, true,
     "0.0", "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective", "perspective", 84, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective/extensions", "extensions", 95, GuardKind::kObject,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective/extras", "extras", 95, GuardKind::kOpaque, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective/aspectRatio", "aspectRatio", 95,
     GuardKind::kNumber, false, "0.0", "", true, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective/yfov", "yfov", 95, GuardKind::kNumber, true, "0.0",
     "", true, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective/zfar", "zfar", 95, GuardKind::kNumber, false,
     "0.0", "", true, 0, 0, 0, 0, false, ""},
    {"/cameras/*/perspective/znear", "znear", 95, GuardKind::kNumber, true,
     "0.0", "", true, 0, 0, 0, 0, false, ""},
    {"/cameras/*/type", "type", 84, GuardKind::kString, true, "", "", false, 0,
     0, 0, 0, false, "perspective|orthographic"},
    {"/images", "images", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0,
     0, false, ""},
    {"/images/*", "*", 103, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/images/*/extensions", "extensions", 104, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/images/*/extras", "extras", 104, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/images/*/name", "name", 104, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/images/*/uri", "uri", 104, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/images/*/mimeType", "mimeType", 104, GuardKind::kString, false, "", "",
     false, 0, 0, 0, 0, false, "image/jpeg|image/png"},
    {"/images/*/bufferView", "bufferView", 104, GuardKind::kInteger, false, "0",
     "", false, 0, 0, 0, 0, false, ""},
    {"/materials", "materials", 0, GuardKind::kArray, false, "", "", false, 1,
     0, 0, 0, false, ""},
    {"/materials/*", "*", 111, GuardKind::kObject, false, "", "", false, 0, 0,
     0, 0, false, ""},
    {"/materials/*/extensions", "extensions", 112, GuardKind::kObject, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/extras", "extras", 112, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/materials/*/name", "name", 112, GuardKind::kString, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness", "pbrMetallicRoughness", 112,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/extensions", "extensions", 116,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/extras", "extras", 116,
     GuardKind::kOpaque, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorFactor", "baseColorFactor",
     116, GuardKind::kArray, false, "", "", false, 4, 4, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorFactor/*", "*", 119,
     GuardKind::kNumber, false, "0.0", "1.0", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorTexture", "baseColorTexture",
     116, GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorTexture/extensions",
     "extensions", 121, GuardKind::kObject, false, "", "", false, 0, 0, 0, 0,
     false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorTexture/extras", "extras", 121,
     GuardKind::kOpaque, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorTexture/index", "index", 121,
     GuardKind::kInteger, true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/baseColorTexture/texCoord", "texCoord",
     121, GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/metallicFactor", "metallicFactor", 116,
     GuardKind::kNumber, false, "0.0", "1.0", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/roughnessFactor", "roughnessFactor",
     116, GuardKind::kNumber, false, "0.0", "1.0", false, 0, 0, 0, 0, false,
     ""},
    {"/materials/*/pbrMetallicRoughness/metallicRoughnessTexture",
     "metallicRoughnessTexture", 116, GuardKind::kObject, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/materials/*/pbrMetallicRoughness/metallicRoughnessTexture/extensions",
     "extensions", 128, GuardKind::kObject, false, "", "", false, 0, 0, 0, 0,
     false, ""},
    {"/materials/*/pbrMetallicRoughness/metallicRoughnessTexture/extras",
     "extras", 128, GuardKind::kOpaque, false, "", "", false, 0, 0, 0, 0, false,
     ""},
    {"/materials/*/pbrMetallicRoughness/metallicRoughnessTexture/index",
     "index", 128, GuardKind::kInteger, true, "0", "", false, 0, 0, 0, 0, false,
     ""},
    {"/materials/*/pbrMetallicRoughness/metallicRoughnessTexture/texCoord",
     "texCoord", 128, GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0,
     false, ""},
    {"/materials/*/normalTexture", "normalTexture", 112, GuardKind::kObject,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/normalTexture/extensions", "extensions", 133,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/normalTexture/extras", "extras", 133, GuardKind::kOpaque,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/normalTexture/index", "index", 133, GuardKind::kInteger,
     true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/normalTexture/texCoord", "texCoord", 133,
     GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/normalTexture/scale", "scale", 133, GuardKind::kNumber,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/occlusionTexture", "occlusionTexture", 112,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/occlusionTexture/extensions", "extensions", 139,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/occlusionTexture/extras", "extras", 139, GuardKind::kOpaque,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/occlusionTexture/index", "index", 139, GuardKind::kInteger,
     true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/occlusionTexture/texCoord", "texCoord", 139,
     GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/occlusionTexture/strength", "strength", 139,
     GuardKind::kNumber, false, "0.0", "1.0", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/emissiveTexture", "emissiveTexture", 112, GuardKind::kObject,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/emissiveTexture/extensions", "extensions", 145,
     GuardKind::kObject, false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/emissiveTexture/extras", "extras", 145, GuardKind::kOpaque,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/emissiveTexture/index", "index", 145, GuardKind::kInteger,
     true, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/emissiveTexture/texCoord", "texCoord", 145,
     GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/emissiveFactor", "emissiveFactor", 112, GuardKind::kArray,
     false, "", "", false, 3, 3, 0, 0, false, ""},
    {"/materials/*/emissiveFactor/*", "*", 150, GuardKind::kNumber, false,
     "0.0", "1.0", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/alphaMode", "alphaMode", 112, GuardKind::kString, false, "",
     "", false, 0, 0, 0, 0, false, "OPAQUE|MASK|BLEND"},
    {"/materials/*/alphaCutoff", "alphaCutoff", 112, GuardKind::kNumber, false,
     "0.0", "", false, 0, 0, 0, 0, false, ""},
    {"/materials/*/doubleSided", "doubleSided", 112, GuardKind::kBoolean, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes", "meshes", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0,
     0, false, ""},
    {"/meshes/*", "*", 155, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/meshes/*/extensions", "extensions", 156, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/extras", "extras", 156, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/name", "name", 156, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/meshes/*/primitives", "primitives", 156, GuardKind::kArray, true, "", "",
     false, 1, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*", "*", 160, GuardKind::kObject, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/extensions", "extensions", 161, GuardKind::kObject,
     false, "", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/extras", "extras", 161, GuardKind::kOpaque, false,
     "", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/attributes", "attributes", 161, GuardKind::kObject,
     true, "", "", false, 0, 0, 1, 0, false, ""},
    {"/meshes/*/primitives/*/attributes/*", "*", 164, GuardKind::kInteger,
     false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/indices", "indices", 161, GuardKind::kInteger,
     false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/material", "material", 161, GuardKind::kInteger,
     false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/mode", "mode", 161, GuardKind::kInteger, false, "",
     "", false, 0, 0, 0, 0, false, "0|1|2|3|4|5|6"},
    {"/meshes/*/primitives/*/targets", "targets", 161, GuardKind::kArray, false,
     "", "", false, 1, 0, 0, 0, false, ""},
    {"/meshes/*/primitives/*/targets/*", "*", 169, GuardKind::kObject, false,
     "", "", false, 0, 0, 1, 0, false, ""},
    {"/meshes/*/primitives/*/targets/*/*", "*", 170, GuardKind::kInteger, false,
     "0", "", false, 0, 0, 0, 0, false, ""},
    {"/meshes/*/weights", "weights", 156, GuardKind::kArray, false, "", "",
     false, 1, 0, 0, 0, false, ""},
    {"/meshes/*/weights/*", "*", 172, GuardKind::kNumber, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/nodes", "nodes", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0, 0,
     false, ""},
    {"/nodes/*", "*", 174, GuardKind::kObject, false, "", "", false, 0, 0, 0, 0,
     false, ""},
    {"/nodes/*/extensions", "extensions", 175, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/nodes/*/extras", "extras", 175, GuardKind::kOpaque, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/nodes/*/name", "name", 175, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/nodes/*/camera", "camera", 175, GuardKind::kInteger, false, "0", "",
     false, 0, 0, 0, 0, false, ""},
    {"/nodes/*/children", "children", 175, GuardKind::kArray, false, "", "",
     false, 1, 0, 0, 0, true, ""},
    {"/nodes/*/children/*", "*", 180, GuardKind::kInteger, false, "0", "",
     false, 0, 0, 0, 0, false, ""},
    {"/nodes/*/skin", "skin", 175, GuardKind::kInteger, false, "0", "", false,
     0, 0, 0, 0, false, ""},
    {"/nodes/*/matrix", "matrix", 175, GuardKind::kArray, false, "", "", false,
     16, 16, 0, 0, false, ""},
    {"/nodes/*/matrix/*", "*", 183, GuardKind::kNumber, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/nodes/*/mesh", "mesh", 175, GuardKind::kInteger, false, "0", "", false,
     0, 0, 0, 0, false, ""},
    {"/nodes/*/rotation", "rotation", 175, GuardKind::kArray, false, "", "",
     false, 4, 4, 0, 0, false, ""},
    {"/nodes/*/rotation/*", "*", 186, GuardKind::kNumber, false, "-1.0", "1.0",
     false, 0, 0, 0, 0, false, ""},
    {"/nodes/*/scale", "scale", 175, GuardKind::kArray, false, "", "", false, 3,
     3, 0, 0, false, ""},
    {"/nodes/*/scale/*", "*", 188, GuardKind::kNumber, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/nodes/*/translation", "translation", 175, GuardKind::kArray, false, "",
     "", false, 3, 3, 0, 0, false, ""},
    {"/nodes/*/translation/*", "*", 190, GuardKind::kNumber, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/nodes/*/weights", "weights", 175, GuardKind::kArray, false, "", "",
     false, 1, 0, 0, 0, false, ""},
    {"/nodes/*/weights/*", "*", 192, GuardKind::kNumber, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/samplers", "samplers", 0, GuardKind::kArray, false, "", "", false, 1, 0,
     0, 0, false, ""},
    {"/samplers/*", "*", 194, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/samplers/*/extensions", "extensions", 195, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/samplers/*/extras", "extras", 195, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/samplers/*/name", "name", 195, GuardKind::kString, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/samplers/*/magFilter", "magFilter", 195, GuardKind::kInteger, false, "",
     "", false, 0, 0, 0, 0, false, "9728|9729"},
    {"/samplers/*/minFilter", "minFilter", 195, GuardKind::kInteger, false, "",
     "", false, 0, 0, 0, 0, false, "9728|9729|9984|9985|9986|9987"},
    {"/samplers/*/wrapS", "wrapS", 195, GuardKind::kInteger, false, "", "",
     false, 0, 0, 0, 0, false, "33071|33648|10497"},
    {"/samplers/*/wrapT", "wrapT", 195, GuardKind::kInteger, false, "", "",
     false, 0, 0, 0, 0, false, "33071|33648|10497"},
    {"/scene", "scene", 0, GuardKind::kInteger, false, "0", "", false, 0, 0, 0,
     0, false, ""},
    {"/scenes", "scenes", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0,
     0, false, ""},
    {"/scenes/*", "*", 204, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/scenes/*/extensions", "extensions", 205, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/scenes/*/extras", "extras", 205, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/scenes/*/name", "name", 205, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/scenes/*/nodes", "nodes", 205, GuardKind::kArray, false, "", "", false,
     1, 0, 0, 0, true, ""},
    {"/scenes/*/nodes/*", "*", 209, GuardKind::kInteger, false, "0", "", false,
     0, 0, 0, 0, false, ""},
    {"/skins", "skins", 0, GuardKind::kArray, false, "", "", false, 1, 0, 0, 0,
     false, ""},
    {"/skins/*", "*", 211, GuardKind::kObject, false, "", "", false, 0, 0, 0, 0,
     false, ""},
    {"/skins/*/extensions", "extensions", 212, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/skins/*/extras", "extras", 212, GuardKind::kOpaque, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/skins/*/name", "name", 212, GuardKind::kString, false, "", "", false, 0,
     0, 0, 0, false, ""},
    {"/skins/*/inverseBindMatrices", "inverseBindMatrices", 212,
     GuardKind::kInteger, false, "0", "", false, 0, 0, 0, 0, false, ""},
    {"/skins/*/skeleton", "skeleton", 212, GuardKind::kInteger, false, "0", "",
     false, 0, 0, 0, 0, false, ""},
    {"/skins/*/joints", "joints", 212, GuardKind::kArray, true, "", "", false,
     1, 0, 0, 0, true, ""},
    {"/skins/*/joints/*", "*", 218, GuardKind::kInteger, false, "0", "", false,
     0, 0, 0, 0, false, ""},
    {"/textures", "textures", 0, GuardKind::kArray, false, "", "", false, 1, 0,
     0, 0, false, ""},
    {"/textures/*", "*", 220, GuardKind::kObject, false, "", "", false, 0, 0, 0,
     0, false, ""},
    {"/textures/*/extensions", "extensions", 221, GuardKind::kObject, false, "",
     "", false, 0, 0, 0, 0, false, ""},
    {"/textures/*/extras", "extras", 221, GuardKind::kOpaque, false, "", "",
     false, 0, 0, 0, 0, false, ""},
    {"/textures/*/name", "name", 221, GuardKind::kString, false, "", "", false,
     0, 0, 0, 0, false, ""},
    {"/textures/*/sampler", "sampler", 221, GuardKind::kInteger, false, "0", "",
     false, 0, 0, 0, 0, false, ""},
    {"/textures/*/source", "source", 221, GuardKind::kInteger, false, "0", "",
     false, 0, 0, 0, 0, false, ""},
};
constexpr std::string_view kExtensionsKey = "extensions";
constexpr std::string_view kWildcardKey = "*";
constexpr std::string_view kVersionPath = "/asset/version";
constexpr std::string_view kMinVersionPath = "/asset/minVersion";
constexpr std::string_view kCurrentVersion = "2.0";
constexpr std::string_view kImageMimePath = "/images/*/mimeType";
constexpr std::string_view kAccessorMinimumItemPath = "/accessors/*/min/";
constexpr std::string_view kAccessorMaximumItemPath = "/accessors/*/max/";
struct PendingGuard {
  std::size_t node;
  std::uint32_t rule;
};
bool Allowed(std::string_view text, std::string_view alternatives) {
  while (!alternatives.empty()) {
    const auto end = alternatives.find('|');
    if (text == alternatives.substr(0, end)) return true;
    if (end == std::string_view::npos) break;
    alternatives.remove_prefix(end + 1);
  }
  return false;
}
bool VersionSpelling(std::string_view value) {
  const auto dot = value.find('.');
  if (dot == 0 || dot == std::string_view::npos || dot + 1 == value.size())
    return false;
  for (std::size_t i = 0; i < value.size(); ++i)
    if (i != dot && (value[i] < '0' || value[i] > '9')) return false;
  return true;
}
void CheckRule(const JsonSnapshot& document, std::size_t node,
               const CoreRule& rule, std::uint32_t rule_id) {
  const auto kind = document.Kind(node);
  const auto reject = [&](GltfDiagnosticCode code) {
    Fail(GltfStatus::kMalformed, code, std::nullopt, rule_id);
  };
  switch (rule.kind) {
    case GuardKind::kOpaque:
      return;
    case GuardKind::kObject:
      if (kind != JsonKind::kObject) reject(GltfDiagnosticCode::kCoreFieldType);
      if (ChildCount(document, node) < rule.min_properties)
        reject(GltfDiagnosticCode::kCoreArrayLength);
      break;
    case GuardKind::kArray: {
      if (kind != JsonKind::kArray) reject(GltfDiagnosticCode::kCoreFieldType);
      const auto count = ChildCount(document, node);
      if (count < rule.min_items ||
          (rule.max_items != 0 && count > rule.max_items))
        reject(GltfDiagnosticCode::kCoreArrayLength);
      if (rule.unique) {
        for (auto a = document.FirstChild(node); a;
             a = document.NextSibling(*a)) {
          for (auto b = document.NextSibling(*a); b;
               b = document.NextSibling(*b)) {
            if (document.Kind(*a) == JsonKind::kString &&
                document.Kind(*b) == JsonKind::kString &&
                document.String(*a) == document.String(*b))
              reject(GltfDiagnosticCode::kCoreRange);
            if (document.Kind(*a) == JsonKind::kNumber &&
                document.Kind(*b) == JsonKind::kNumber &&
                CompareDecimal(document.Number(*a), document.Number(*b)) == 0)
              reject(GltfDiagnosticCode::kCoreRange);
          }
        }
      }
      break;
    }
    case GuardKind::kBoolean:
      if (kind != JsonKind::kBool) reject(GltfDiagnosticCode::kCoreFieldType);
      break;
    case GuardKind::kString: {
      if (kind != JsonKind::kString) reject(GltfDiagnosticCode::kCoreFieldType);
      const auto value = document.String(node);
      if (value.find('\0') != std::string_view::npos)
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kUnsupportedCoreString, std::nullopt, rule_id);
      if (rule.path == kVersionPath || rule.path == kMinVersionPath) {
        if (!VersionSpelling(value)) reject(GltfDiagnosticCode::kCoreRange);
        if (value != kCurrentVersion)
          Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kUnsupportedEnum, std::nullopt, rule_id);
      }
      if (!rule.allowed.empty() && rule.path != kImageMimePath &&
          !Allowed(value, rule.allowed))
        Fail(GltfStatus::kUnsupportedProfile,
             GltfDiagnosticCode::kUnsupportedEnum, std::nullopt, rule_id);
      break;
    }
    case GuardKind::kInteger:
    case GuardKind::kNumber: {
      if (kind != JsonKind::kNumber) reject(GltfDiagnosticCode::kCoreFieldType);
      const auto value = document.Number(node);
      if (!rule.minimum.empty()) {
        const auto order = CompareDecimal(value, rule.minimum);
        if (order < 0 || (order == 0 && rule.exclusive_minimum))
          reject(GltfDiagnosticCode::kCoreRange);
      }
      if (!rule.maximum.empty() && CompareDecimal(value, rule.maximum) > 0)
        reject(GltfDiagnosticCode::kCoreRange);
      if (rule.kind == GuardKind::kInteger) {
        const auto integer = ReadExactUnsigned(document, node);
        if (rule.multiple_of != 0 && integer % rule.multiple_of != 0)
          reject(GltfDiagnosticCode::kCoreRange);
        if (!rule.allowed.empty()) {
          constexpr std::size_t kUint64DecimalCapacity = 20;
          char spelling[kUint64DecimalCapacity];
          const auto result =
              std::to_chars(spelling, spelling + sizeof(spelling), integer);
          if (!Allowed(
                  {spelling, static_cast<std::size_t>(result.ptr - spelling)},
                  rule.allowed))
            Fail(GltfStatus::kUnsupportedProfile,
                 GltfDiagnosticCode::kUnsupportedEnum, std::nullopt, rule_id);
        }
      } else if (!rule.path.starts_with(kAccessorMinimumItemPath) &&
                 !rule.path.starts_with(kAccessorMaximumItemPath)) {
        (void)ReadFloatBits(document, node);
      }
      break;
    }
  }
}
void RequireDependency(const JsonSnapshot& document, std::size_t node,
                       std::string_view dependent, std::string_view required) {
  if (FindMember(document, node, dependent) &&
      !FindMember(document, node, required))
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired);
}
void ObjectRelationships(const JsonSnapshot& document, std::size_t node,
                         std::string_view path) {
  constexpr std::string_view kSceneKey = "scene";
  constexpr std::string_view kScenesKey = "scenes";
  constexpr std::string_view kAccessorPath = "/accessors/*";
  constexpr std::string_view kByteOffsetKey = "byteOffset";
  constexpr std::string_view kBufferViewKey = "bufferView";
  constexpr std::string_view kImagePath = "/images/*";
  constexpr std::string_view kMimeTypeKey = "mimeType";
  constexpr std::string_view kUriKey = "uri";
  constexpr std::string_view kNodePath = "/nodes/*";
  constexpr std::string_view kWeightsKey = "weights";
  constexpr std::string_view kSkinKey = "skin";
  constexpr std::string_view kMeshKey = "mesh";
  constexpr std::string_view kMatrixKey = "matrix";
  constexpr std::array<std::string_view, 3> kTrsKeys = {"translation",
                                                        "rotation", "scale"};
  constexpr std::string_view kMaterialPath = "/materials/*";
  constexpr std::string_view kAlphaCutoffKey = "alphaCutoff";
  constexpr std::string_view kAlphaModeKey = "alphaMode";
  if (path.empty()) RequireDependency(document, node, kSceneKey, kScenesKey);
  if (path == kAccessorPath)
    RequireDependency(document, node, kByteOffsetKey, kBufferViewKey);
  if (path == kImagePath) {
    RequireDependency(document, node, kBufferViewKey, kMimeTypeKey);
    if (FindMember(document, node, kBufferViewKey).has_value() ==
        FindMember(document, node, kUriKey).has_value())
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired);
  }
  if (path == kMaterialPath)
    RequireDependency(document, node, kAlphaCutoffKey, kAlphaModeKey);
  if (path == kNodePath) {
    RequireDependency(document, node, kWeightsKey, kMeshKey);
    RequireDependency(document, node, kSkinKey, kMeshKey);
    if (FindMember(document, node, kMatrixKey))
      for (const auto key : kTrsKeys)
        if (FindMember(document, node, key))
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreRange);
  }
}
}  // namespace
void AdmitMetadata(AssetState& state) {
  auto& document = state.document;
  state.guard_rules.assign(document.NodeCount(), kNoGuard);
  OwnedVector<PendingGuard> pending{Allocator<PendingGuard>(state.allocation)};
  pending.push_back({0, 0});
  while (!pending.empty()) {
    state.allocation.Checkpoint();
    const auto item = pending.back();
    pending.pop_back();
    const auto& rule = kCoreRules[item.rule];
    state.guard_rules[item.node] = item.rule;
    if (rule.key == kWildcardKey && rule.kind == GuardKind::kInteger &&
        document.Key(item.node).find('\0') != std::string_view::npos)
      Fail(GltfStatus::kUnsupportedProfile,
           GltfDiagnosticCode::kUnsupportedCoreString, std::nullopt, item.rule);
    CheckRule(document, item.node, rule, item.rule);
    constexpr std::string_view kAccessorCountPath = "/accessors/*/count";
    if (rule.path == kAccessorCountPath &&
        ReadExactUnsigned(document, item.node) >
            state.limits.max_accessor_elements)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
    if (rule.kind == GuardKind::kArray &&
        ChildCount(document, item.node) > state.limits.max_table_rows)
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
    if (rule.kind == GuardKind::kOpaque) continue;
    if (rule.key == kExtensionsKey) {
      for (auto child = document.FirstChild(item.node); child;
           child = document.NextSibling(*child))
        if (document.Kind(*child) != JsonKind::kObject)
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldType);
      continue;
    }
    if (rule.kind == GuardKind::kObject) {
      for (const auto& child_rule : kCoreRules)
        if (child_rule.parent == item.rule && child_rule.required &&
            !FindMember(document, item.node, child_rule.key))
          Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kCoreFieldRequired);
      ObjectRelationships(document, item.node, rule.path);
    }
    // Stage children in reverse source order, keeping deterministic first
    // error.
    const auto staged_start = pending.size();
    for (auto child = document.FirstChild(item.node); child;
         child = document.NextSibling(*child)) {
      state.allocation.Checkpoint();
      std::optional<std::uint32_t> wildcard;
      std::optional<std::uint32_t> selected;
      for (std::uint32_t index = 0; index < std::size(kCoreRules); ++index) {
        const auto& child_rule = kCoreRules[index];
        if (child_rule.parent != item.rule) continue;
        if (child_rule.key == kWildcardKey) wildcard = index;
        if (child_rule.key == document.Key(*child)) selected = index;
      }
      if (!selected) selected = wildcard;
      if (selected) pending.push_back({*child, *selected});
    }
    std::reverse(pending.begin() + staged_start, pending.end());
  }
  CheckMetadataRelationships(state);
  constexpr std::string_view kRequiredExtensionsKey = "extensionsRequired";
  if (FindMember(document, 0, kRequiredExtensionsKey))
    Fail(GltfStatus::kUnsupportedProfile,
         GltfDiagnosticCode::kRequiredExtension);
}
OwnedVector<char> BuildCoreProjection(const AssetState& state) {
  OwnedVector<char> bytes{Allocator<char>(state.allocation)};
  const auto& document = state.document;
  auto append = [&](std::string_view text) {
    if (bytes.size() > state.limits.max_projection_bytes ||
        text.size() > state.limits.max_projection_bytes - bytes.size())
      Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
    bytes.insert(bytes.end(), text.begin(), text.end());
  };
  auto string = [&](std::string_view text) {
    constexpr std::string_view kHexDigits = "0123456789abcdef";
    constexpr unsigned char kFirstPrintable = 0x20;
    append("\"");
    for (const auto raw : text) {
      state.allocation.Checkpoint();
      const auto character = static_cast<unsigned char>(raw);
      if (character == '"' || character == '\\') {
        const char escaped[] = {'\\', raw};
        append({escaped, sizeof(escaped)});
      } else if (character < kFirstPrintable) {
        const char escaped[] = {'\\',
                                'u',
                                '0',
                                '0',
                                kHexDigits[character >> 4],
                                kHexDigits[character & 0xf]};
        append({escaped, sizeof(escaped)});
      } else {
        append({&raw, 1});
      }
    }
    append("\"");
  };
  struct Frame {
    std::size_t node;
    std::optional<std::size_t> next;
    bool opened;
  };
  OwnedVector<Frame> stack{Allocator<Frame>(state.allocation)};
  stack.push_back({0, {}, false});
  while (!stack.empty()) {
    state.allocation.Checkpoint();
    auto& frame = stack.back();
    const auto kind = document.Kind(frame.node);
    if (kind == JsonKind::kObject || kind == JsonKind::kArray) {
      if (!frame.opened) {
        append(kind == JsonKind::kObject ? "{" : "[");
        frame.opened = true;
        frame.next = document.FirstChild(frame.node);
      }
      if (!frame.next) {
        append(kind == JsonKind::kObject ? "}" : "]");
        stack.pop_back();
        continue;
      }
      const auto child = *frame.next;
      if (document.FirstChild(frame.node) != frame.next) append(",");
      if (kind == JsonKind::kObject) {
        string(document.Key(child));
        append(":");
      }
      frame.next = document.NextSibling(child);
      stack.push_back({child, {}, false});
      continue;
    }
    switch (kind) {
      case JsonKind::kString:
        string(document.String(frame.node));
        break;
      case JsonKind::kNull:
        append("null");
        break;
      case JsonKind::kBool:
        append(document.Boolean(frame.node) ? "true" : "false");
        break;
      case JsonKind::kNumber: {
        const auto rule_index = state.guard_rules[frame.node];
        if (rule_index == kNoGuard) {
          append(document.Number(frame.node));
          break;
        }
        const auto& rule = kCoreRules[rule_index];
        constexpr std::size_t kNumberSpellingCapacity = 64;
        char formatted[kNumberSpellingCapacity];
        std::to_chars_result result{};
        if (rule.kind == GuardKind::kInteger)
          result = std::to_chars(formatted, formatted + sizeof(formatted),
                                 ReadExactUnsigned(document, frame.node));
        else
          result = std::to_chars(
              formatted, formatted + sizeof(formatted),
              std::bit_cast<float>(ReadFloatBits(document, frame.node)),
              std::chars_format::general,
              std::numeric_limits<float>::max_digits10);
        if (result.ec != std::errc{})
          Fail(GltfStatus::kUnsupportedProfile,
               GltfDiagnosticCode::kUnsupportedNumberRepresentation);
        append({formatted, static_cast<std::size_t>(result.ptr - formatted)});
        break;
      }
      case JsonKind::kObject:
      case JsonKind::kArray:
        break;
    }
    stack.pop_back();
  }
  return bytes;
}
std::string_view GuardPath(std::uint32_t rule) {
  return rule < std::size(kCoreRules) ? kCoreRules[rule].path
                                      : std::string_view{};
}
}  // namespace meshvale::interchange::gltf_detail
