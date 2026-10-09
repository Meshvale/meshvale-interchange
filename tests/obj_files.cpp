// SPDX-License-Identifier: Apache-2.0
#include "meshvale/interchange/obj_files.h"
#include <exception>
#include <filesystem>
#include <ios>
#include <iterator>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace meshvale::interchange;
namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Result> bool has(const Result& result, const std::string& code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&](const auto& d) { return d.code == code; });
}
struct Scratch {
    fs::path root;
    Scratch() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto path = fs::current_path() / (".meshvale-files-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) { root = std::move(path); return; }
        }
        throw std::runtime_error("test scratch creation failed");
    }
    ~Scratch() { std::error_code error; if (!root.empty()) fs::remove_all(root, error); }
};
void put(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary); out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close(); require(!out.fail(), "fixture write failed");
}
std::string get(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    require(static_cast<bool>(input), "fixture read failed");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
const std::string quad = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";
void resource_snapshot() {
    Scratch s;
    const auto first = std::string("newmtl red\r\nKd 0.1 0.2 0.3\r\nmap_Kd -s 1 1 1 image file.bin  \r\n");
    const auto second = std::string("newmtl blue\nPr 0.2\nmap_Pr -clamp on image.bin\nnorm image.bin\n");
    const std::string bytes{"a\0b", 3};
    put(s.root / "model.obj", "mtllib a/first.mtl b/second.mtl\nmtllib a/first.mtl\n" + quad);
    put(s.root / "a/first.mtl", first); put(s.root / "a/image file.bin", bytes);
    put(s.root / "b/second.mtl", second); put(s.root / "b/image.bin", "second");
    auto input = read_obj_file(s.root / "model.obj");
    require(input.asset.has_value(), "multiple library import failed");
    const auto& asset = *input.asset;
    require(asset.obj_path == "model.obj" && asset.material_libraries.size() == 2 && asset.resources.size() == 4,
            "resource manifest incorrect");
    require(asset.document.material_library == first + second, "raw library bytes changed");
    require(asset.document.material_names == std::vector<std::string>{"red", "blue"}, "material order changed");
    require(asset.document.mesh.corner_vertices == std::vector<meshvale::geometry::index_t>{0,1,2,3}, "quad changed");
    require(asset.resources.front().path == "a/first.mtl" && asset.resources.front().bytes == first, "snapshot changed");
    put(s.root / "a/image file.bin", "new source");
    require(asset.resources[1].bytes == bytes, "resource snapshot borrowed source data");
}
void enclosing_root_and_boundaries() {
    Scratch s;
    put(s.root / "asset/model.obj", "mtllib ../materials/sample.mtl\n" + quad);
    put(s.root / "materials/sample.mtl", "newmtl test\nmap_Kd ../textures/image.bin\n");
    put(s.root / "textures/image.bin", "texture");
    require(!read_obj_file(s.root / "asset/model.obj").asset, "default root allowed escape");
    ObjFileOptions options; options.resource_root = s.root;
    auto input = read_obj_file(s.root / "asset/model.obj", options);
    require(input.asset && input.asset->obj_path == "asset/model.obj", "explicit root failed");
    require(input.asset->resources.size() == 2, "relative texture not loaded");
    auto publication = publish_obj_bundle(*input.asset, s.root / "nested-export");
    require(publication.outcome == ObjBundleOutcome::published && publication.entry == fs::path("asset/model.obj"),
            "enclosing root publication failed");
    ObjFileOptions published_options; published_options.resource_root = s.root / "nested-export";
    auto published = read_obj_file(s.root / "nested-export/asset/model.obj", published_options);
    require(published.asset && published.asset->resources == input.asset->resources, "parent-relative library changed");
    std::stop_source stop; stop.request_stop(); options.stop = stop.get_token();
    auto cancelled = read_obj_file(s.root / "asset/model.obj", options);
    require(!cancelled.asset && has(cancelled, "obj.cancelled"), "import cancellation ignored");
    options.stop = {};
    // Conditional filesystem link test: creating links may require host permissions.
    put(s.root / "outside.bin", "outside");
    fs::create_directories(s.root / "limited");
    put(s.root / "limited/model.obj", "mtllib test.mtl\n" + quad);
    put(s.root / "limited/test.mtl", "newmtl t\nmap_Kd link.bin\n");
    std::error_code error;
    fs::create_symlink(s.root / "outside.bin", s.root / "limited/link.bin", error);
    if (!error) {
        auto linked = read_obj_file(s.root / "limited/model.obj");
        require(!linked.asset && has(linked, "obj.resource_outside_root"), "link escaped resource root");
    } else std::cout << "Link fixture unavailable on this host\n";
}
void explicit_failures() {
    Scratch s;
    put(s.root / "model.obj", "mtllib missing.mtl\n" + quad);
    auto missing = read_obj_file(s.root / "model.obj");
    require(!missing.asset && has(missing, "obj.resource_missing"), "missing MTL accepted");
    put(s.root / "missing.mtl", "newmtl m\nmap_Kd absent.bin\n");
    missing = read_obj_file(s.root / "model.obj");
    require(!missing.asset && has(missing, "obj.resource_missing"), "missing texture accepted");
    put(s.root / "missing.mtl", "newmtl m\nunknown_resource other.bin\n");
    auto unsupported = read_obj_file(s.root / "model.obj");
    require(!unsupported.asset && has(unsupported, "obj.unsupported_mtl_directive"), "unknown MTL ignored");
    for (const auto& name : {"../outside.bin", "/absolute.bin", "NUL.bin", "a:b.bin", "\"quoted.bin\""}) {
        put(s.root / "missing.mtl", "newmtl m\nmap_Kd " + std::string(name) + "\n");
        require(!read_obj_file(s.root / "model.obj").asset, "unsafe texture reference accepted");
    }
    fs::create_directory(s.root / "directory.bin");
    put(s.root / "missing.mtl", "newmtl m\nmap_Kd directory.bin\n");
    auto directory = read_obj_file(s.root / "model.obj");
    require(!directory.asset && has(directory, "obj.resource_not_file"), "directory treated as texture");
    put(s.root / "missing.mtl", "newmtl m\nmap_Kd same.bin\nmap_Ks SAME.bin\n");
    put(s.root / "same.bin", "a"); put(s.root / "SAME.bin", "a");
    auto alias = read_obj_file(s.root / "model.obj");
    require(!alias.asset && has(alias, "obj.resource_collision"), "case alias accepted");
    put(s.root / "bad.obj", "v 0 0 0\nf 1 99 1\n");
    auto raw = read_obj_file(s.root / "bad.obj");
    require(raw.asset && has(raw, "mesh.vertex_range"), "raw defect hidden by filesystem adapter");
    put(s.root / "invalid.obj", "mtllib\n" + quad);
    require(!read_obj_file(s.root / "invalid.obj").asset, "empty library directive accepted");
}
fs::path stage_in(const fs::path& parent) {
    for (const auto& entry : fs::directory_iterator(parent))
        if (entry.path().filename().string().starts_with(".meshvale-stage-")) return entry.path();
    throw std::runtime_error("staging directory missing");
}
void no_stage(const fs::path& parent) {
    for (const auto& entry : fs::directory_iterator(parent))
        require(!entry.path().filename().string().starts_with(".meshvale-stage-"), "staging cleanup failed");
}
ObjFileAsset fixture(const fs::path& root) {
    put(root / "source/model.obj", "mtllib materials/one.mtl materials/two.mtl\n" + quad);
    put(root / "source/materials/one.mtl", "newmtl a\nmap_Kd ../textures/image file.bin\n");
    put(root / "source/materials/two.mtl", "newmtl b\nmap_Ke ../textures/image file.bin\n");
    put(root / "source/textures/image file.bin", std::string("a\0b", 3));
    ObjFileOptions options; options.resource_root = root / "source";
    auto imported = read_obj_file(root / "source/model.obj", options);
    require(imported.asset.has_value(), "publication fixture import failed");
    return std::move(*imported.asset);
}
void verified_publication() {
    Scratch s;
    auto asset = fixture(s.root);
    const auto source_obj = get(s.root / "source/model.obj");
    const auto original_texture = asset.resources.back().bytes;
    // Publication consumes snapshots rather than changed or missing original resources.
    put(s.root / "source/textures/image file.bin", "changed after import");
    ObjBundleOptions options;
    options.supplemental_files.push_back({"report.json", "{\"outcome\":\"accepted\"}\n"});
    std::vector<ObjBundlePhase> phases;
    options.on_phase = [&](auto phase) { phases.push_back(phase); };
    auto result = publish_obj_bundle(asset, s.root / "bundle", options);
    require(result.outcome == ObjBundleOutcome::published && result.entry == fs::path("model.obj"), "bundle not published");
    require(phases == std::vector<ObjBundlePhase>{ObjBundlePhase::preflight, ObjBundlePhase::staging,
            ObjBundlePhase::verification, ObjBundlePhase::publication}, "phase sequence incorrect");
    require(get(s.root / "bundle/textures/image file.bin") == original_texture, "snapshot texture changed");
    require(get(s.root / "bundle/materials/one.mtl") == asset.resources.front().bytes, "library bytes changed");
    require(get(s.root / "bundle/report.json") == options.supplemental_files.front().bytes, "supplemental file changed");
    auto reloaded = read_obj_file(s.root / "bundle/model.obj");
    require(reloaded.asset && reloaded.asset->resources == asset.resources &&
            reloaded.asset->document.mesh.corner_vertices == asset.document.mesh.corner_vertices, "bundle reload failed");
    require(get(s.root / "source/model.obj") == source_obj &&
            get(s.root / "source/textures/image file.bin") == "changed after import", "source overwritten");
    no_stage(s.root);
    auto existing = publish_obj_bundle(asset, s.root / "bundle");
    require(existing.outcome == ObjBundleOutcome::failed && has(existing, "obj.destination_exists"), "existing bundle overwritten");
    fs::create_directory(s.root / "empty");
    require(publish_obj_bundle(asset, s.root / "empty").outcome == ObjBundleOutcome::failed, "empty destination replaced");
    put(s.root / "file", "retained");
    require(publish_obj_bundle(asset, s.root / "file").outcome == ObjBundleOutcome::failed &&
            get(s.root / "file") == "retained", "destination file overwritten");
    std::error_code error;
    fs::create_symlink(s.root / "absent", s.root / "dangling", error);
    if (!error) require(publish_obj_bundle(asset, s.root / "dangling").outcome == ObjBundleOutcome::failed &&
                        fs::is_symlink(fs::symlink_status(s.root / "dangling")), "dangling destination replaced");
    // Empty MTL still remains a referenced resource in the file bundle.
    put(s.root / "empty-source/model.obj", "mtllib empty.mtl\n" + quad);
    put(s.root / "empty-source/empty.mtl", "");
    auto empty = read_obj_file(s.root / "empty-source/model.obj");
    require(empty.asset && publish_obj_bundle(*empty.asset, s.root / "empty-mtl-bundle").outcome == ObjBundleOutcome::published,
            "empty library lost during publication");
}
void publication_failures_and_cancellation() {
    Scratch s;
    const auto asset = fixture(s.root);
    const auto source = get(s.root / "source/model.obj");
    for (auto boundary : {ObjBundlePhase::preflight, ObjBundlePhase::staging,
                          ObjBundlePhase::verification, ObjBundlePhase::publication}) {
        std::stop_source stop;
        ObjBundleOptions options; options.stop = stop.get_token();
        options.on_phase = [&](auto phase) { if (phase == boundary) stop.request_stop(); };
        auto result = publish_obj_bundle(asset, s.root / "cancelled", options);
        require(result.outcome == ObjBundleOutcome::cancelled && result.phase == boundary && !result.entry &&
                !fs::exists(s.root / "cancelled"), "cancellation published output");
        no_stage(s.root);
    }
    ObjBundleOptions race;
    race.on_phase = [&](auto phase) {
        if (phase == ObjBundlePhase::publication) fs::create_directory(s.root / "race");
    };
    auto result = publish_obj_bundle(asset, s.root / "race", race);
    require(result.outcome == ObjBundleOutcome::failed && fs::is_empty(s.root / "race"), "racing empty directory replaced");
    no_stage(s.root);
    race.on_phase = [&](auto phase) {
        if (phase == ObjBundlePhase::publication) put(s.root / "race-file", "other writer");
    };
    result = publish_obj_bundle(asset, s.root / "race-file", race);
    require(result.outcome == ObjBundleOutcome::failed && get(s.root / "race-file") == "other writer", "racing file replaced");
    no_stage(s.root);
    for (int mode = 0; mode < 5; ++mode) {
        ObjBundleOptions broken;
        broken.supplemental_files.push_back({"report.json", "original report"});
        broken.on_phase = [&](auto phase) {
            if (phase != ObjBundlePhase::verification) return;
            const auto staging = stage_in(s.root);
            if (mode == 0) fs::remove(staging / "textures/image file.bin");
            if (mode == 1) put(staging / "textures/image file.bin", "corrupt");
            if (mode == 2) put(staging / "model.obj", "invalid directive\n");
            if (mode == 3) put(staging / "unexpected.bin", "extra");
            if (mode == 4) put(staging / "report.json", "corrupt report");
        };
        result = publish_obj_bundle(asset, s.root / "broken", broken);
        require(result.outcome == ObjBundleOutcome::failed && result.phase == ObjBundlePhase::verification &&
                !fs::exists(s.root / "broken"), "failed staged verification published output");
        no_stage(s.root);
    }
    ObjBundleOptions callback;
    callback.on_phase = [](auto phase) { if (phase == ObjBundlePhase::verification) throw std::runtime_error("caller"); };
    result = publish_obj_bundle(asset, s.root / "callback", callback);
    require(result.outcome == ObjBundleOutcome::failed && has(result, "obj.progress_callback_failed") &&
            !fs::exists(s.root / "callback"), "callback failure published output");
    no_stage(s.root);
    ObjBundleOptions write_failure;
    write_failure.supplemental_files.push_back({std::string(300, 'x') + ".json", "report"});
    result = publish_obj_bundle(asset, s.root / "write-failure", write_failure);
    require(result.outcome == ObjBundleOutcome::failed && result.phase == ObjBundlePhase::staging &&
            !fs::exists(s.root / "write-failure"), "file write failure published output");
    no_stage(s.root);
    auto invalid = asset; invalid.document.mesh.corner_vertices[0] = 999;
    require(publish_obj_bundle(invalid, s.root / "invalid").outcome == ObjBundleOutcome::failed, "invalid storage exported");
    invalid = asset; invalid.resources.front().bytes += "# changed\n";
    result = publish_obj_bundle(invalid, s.root / "incoherent");
    require(result.outcome == ObjBundleOutcome::failed && has(result, "obj.material_library_mismatch"), "incoherent MTL exported");
    for (auto name : {"model.obj", "MODEL.OBJ", "materials", "../outside.json"}) {
        ObjBundleOptions collision; collision.supplemental_files.push_back({name, "report"});
        result = publish_obj_bundle(asset, s.root / "collision", collision);
        require(result.outcome == ObjBundleOutcome::failed && !fs::exists(s.root / "collision"), "colliding supplemental file exported");
    }
    require(get(s.root / "source/model.obj") == source, "failure changed source");
}
void unicode_and_separator_references() {
    Scratch s;
    const fs::path texture{u8"r\u00e9source/\u56fe\u50cf.bin"};
    const auto utf8 = texture.generic_u8string();
    const std::string name{utf8.begin(), utf8.end()};
    put(s.root / "source/model.obj", "mtllib materials" + std::string(1, '\\') + "sample.mtl\n" + quad);
    put(s.root / "source/materials/sample.mtl", "newmtl t\nmap_Kd ../" + name + "\n");
    put(s.root / "source" / texture, "unicode texture");
    auto input = read_obj_file(s.root / "source/model.obj");
    require(input.asset.has_value(), "Unicode/separator reference import failed");
    auto result = publish_obj_bundle(*input.asset, s.root / "bundle");
    require(result.outcome == ObjBundleOutcome::published && get(s.root / "bundle" / texture) == "unicode texture",
            "Unicode resource publication failed");
}
void verified_supplements() {
    Scratch s;
    const auto asset = fixture(s.root);
    ObjBundleOptions options;
    options.supplemental_files.push_back({"static.bin", "static"});
    bool called = false;
    options.on_verified = [&](const auto& loaded, const auto& files) {
        called = true;
        require(loaded.document.mesh.corner_vertices == asset.document.mesh.corner_vertices, "verified mesh changed");
        require(files.size() == asset.resources.size() + 2, "verified inventory incomplete");
        for (const auto& file : files)
            require(file.bytes == get(stage_in(s.root) / file.path), "callback bytes not staged bytes");
        return std::vector<ObjResource>{{"receipt.json", "verified receipt\n"}};
    };
    auto result = publish_obj_bundle(asset, s.root / "verified", options);
    require(called && result.outcome == ObjBundleOutcome::published &&
            get(s.root / "verified/receipt.json") == "verified receipt\n", "verified supplement missing");
    no_stage(s.root);
    for (auto name : {"model.obj", "MODEL.OBJ", "materials", "../outside.json"}) {
        options.on_verified = [=](const auto&, const auto&) { return std::vector<ObjResource>{{name, "receipt"}}; };
        result = publish_obj_bundle(asset, s.root / "collision", options);
        require(result.outcome == ObjBundleOutcome::failed && !fs::exists(s.root / "collision"), "dynamic collision published");
        no_stage(s.root);
    }
    options.on_verified = [](const ObjFileAsset&, const std::vector<ObjResource>&) -> std::vector<ObjResource> {
        throw std::runtime_error("caller");
    };
    result = publish_obj_bundle(asset, s.root / "callback", options);
    require(result.outcome == ObjBundleOutcome::failed && has(result, "obj.verified_callback_failed"), "callback exception hidden");
    no_stage(s.root);
    options.on_verified = [&](const auto&, const auto&) {
        put(stage_in(s.root) / "model.obj", "changed after verification");
        return std::vector<ObjResource>{{"receipt.json", "receipt"}};
    };
    result = publish_obj_bundle(asset, s.root / "tampered", options);
    require(result.outcome == ObjBundleOutcome::failed && !fs::exists(s.root / "tampered"), "old verified bytes not rechecked");
    no_stage(s.root);
    std::stop_source stop;
    options.stop = stop.get_token();
    options.on_verified = [&](const auto&, const auto&) { stop.request_stop(); return std::vector<ObjResource>{}; };
    result = publish_obj_bundle(asset, s.root / "cancelled", options);
    require(result.outcome == ObjBundleOutcome::cancelled && !fs::exists(s.root / "cancelled"), "callback cancellation published");
    no_stage(s.root);
}
int main() {
    try { resource_snapshot(); enclosing_root_and_boundaries(); explicit_failures();
          verified_publication(); publication_failures_and_cancellation(); unicode_and_separator_references(); verified_supplements(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "OBJ resource snapshot checks passed\n";
}
