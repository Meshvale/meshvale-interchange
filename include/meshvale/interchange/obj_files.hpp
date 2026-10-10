// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "obj.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>

namespace meshvale::interchange {

struct ObjResource {
    std::filesystem::path path; // Relative to the asset root.
    std::string bytes; // Binary-safe, owned snapshot.
    bool operator==(const ObjResource&) const = default;
};
struct ObjFileAsset {
    ObjDocument document;
    std::filesystem::path obj_path;
    std::vector<std::filesystem::path> material_libraries;
    std::vector<ObjResource> resources;
};
struct ObjFileOptions {
    std::filesystem::path resource_root; // Empty means the input OBJ's directory.
    std::stop_token stop;
};
struct ObjFileResult {
    std::optional<ObjFileAsset> asset;
    std::vector<geometry::Diagnostic> diagnostics;
};
enum class ObjBundlePhase { preflight, staging, verification, publication };
enum class ObjBundleOutcome { failed, cancelled, published };
struct ObjBundleOptions {
    std::stop_token stop;
    // Synchronous progress callback. Exceptions fail publication; no callback after commit.
    std::function<void(ObjBundlePhase)> on_phase;
    std::vector<ObjResource> supplemental_files;
};
struct ObjBundleResult {
    ObjBundleOutcome outcome{ObjBundleOutcome::failed};
    ObjBundlePhase phase{ObjBundlePhase::preflight};
    std::optional<std::filesystem::path> entry;
    std::vector<geometry::Diagnostic> diagnostics;
};

[[nodiscard]] ObjFileResult read_obj_file(const std::filesystem::path& input,
                                        const ObjFileOptions& options = {});
[[nodiscard]] ObjBundleResult publish_obj_bundle(const ObjFileAsset& asset,
                                                const std::filesystem::path& destination,
                                                const ObjBundleOptions& options = {});

} // namespace meshvale::interchange
