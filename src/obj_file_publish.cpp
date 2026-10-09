// SPDX-License-Identifier: Apache-2.0
#include "obj_file_detail.h"
#include <algorithm>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <new>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

namespace meshvale::interchange {
namespace {
using namespace detail;
struct Stage {
    fs::path path;
    void cleanup(std::vector<geometry::Diagnostic>* diagnostics = nullptr) noexcept {
        if (path.empty()) return;
        std::error_code error;
        bool threw = false;
        try { fs::remove_all(path, error); } catch (...) { threw = true; }
        if ((error || threw) && diagnostics) {
            try { diagnostics->push_back({"obj.cleanup_failed", "staging", std::nullopt}); } catch (...) {}
        }
        path.clear();
    }
    ~Stage() { cleanup(); }
};
bool present(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        fail("obj.filesystem_error", "destination");
    return status.type() != fs::file_type::not_found;
}
void make_stage(Stage& stage, const fs::path& parent) {
    static std::atomic<unsigned long long> sequence{0};
    const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto path = parent / (".meshvale-stage-" + std::to_string(time) + "-" + std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        if (fs::create_directory(path, error)) { stage.path = std::move(path); return; }
        if (error && error != std::errc::file_exists) fail("obj.staging_create", "staging");
    }
    fail("obj.staging_create", "staging");
}
void write_bytes(const fs::path& root, const ObjResource& file, std::stop_token stop) {
    cancelled(stop);
    const auto path = root / file.path;
    fs::create_directories(path.parent_path());
    if (present(path)) fail("obj.resource_collision", key(file.path));
    std::ofstream out(path, std::ios::binary);
    if (!out) fail("obj.resource_write", key(file.path));
    out.write(file.bytes.data(), static_cast<std::streamsize>(file.bytes.size()));
    out.close();
    if (out.fail()) fail("obj.resource_write", key(file.path));
    cancelled(stop);
}
void finalize(const fs::path& source, const fs::path& destination) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(), 0)) return;
    const auto error = GetLastError();
    fail(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? "obj.destination_exists" : "obj.publication_failed",
         "publication");
#elif defined(__linux__)
    if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) == 0) return;
    fail(errno == EEXIST ? "obj.destination_exists" : "obj.publication_failed", "publication");
#elif defined(__APPLE__)
    if (renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0) return;
    fail(errno == EEXIST ? "obj.destination_exists" : "obj.publication_failed", "publication");
#else
    (void)source; (void)destination;
    fail("obj.publication_unsupported", "platform");
#endif
}
std::string file_obj(const ObjFileAsset& asset, const std::string& canonical) {
    std::string references;
    for (const auto& library : asset.material_libraries) {
        const auto relative = library.lexically_relative(asset.obj_path.parent_path().empty() ? fs::path(".") : asset.obj_path.parent_path());
        const auto name = key(relative);
        if (name.empty() || name.find_first_of(" \t\r\n") != std::string::npos)
            fail("obj.resource_syntax", "mtllib");
        references += "mtllib " + name + "\n";
    }
    std::string text;
    std::istringstream lines(canonical);
    std::string line;
    bool inserted = false;
    while (std::getline(lines, line)) {
        if (line.starts_with("mtllib ")) continue;
        text += line + "\n";
        if (!inserted) { text += references; inserted = true; }
    }
    return text;
}
void verify_inventory(const fs::path& root, const std::map<std::string, fs::path>& expected) {
    std::map<std::string, fs::path> actual;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) fail("obj.staged_inventory", "links");
        if (entry.is_directory()) continue;
        if (!entry.is_regular_file()) fail("obj.staged_inventory", "files");
        const auto relative = entry.path().lexically_relative(root);
        insert_path(actual, relative);
    }
    if (actual != expected) fail("obj.staged_inventory", "files");
}
} // namespace

ObjBundleResult publish_obj_bundle(const ObjFileAsset& asset, const fs::path& destination,
                                   const ObjBundleOptions& options) {
    ObjBundleResult result;
    Stage stage;
    auto phase = [&](ObjBundlePhase value) {
        result.phase = value;
        cancelled(options.stop);
        if (options.on_phase) {
            try { options.on_phase(value); }
            catch (...) { fail("obj.progress_callback_failed", "callback"); }
        }
        cancelled(options.stop);
    };
    try {
        phase(ObjBundlePhase::preflight);
        std::map<std::string, fs::path> expected;
        insert_path(expected, asset.obj_path);
        for (const auto& resource : asset.resources) insert_path(expected, resource.path);
        for (const auto& file : options.supplemental_files) insert_path(expected, file.path);
        if (combined(asset.material_libraries, asset.resources) != asset.document.material_library)
            fail("obj.material_library_mismatch", "resources");
        auto serialized = write_obj(asset.document);
        result.diagnostics = std::move(serialized.diagnostics);
        if (!serialized.text) return result;
        const auto output_text = file_obj(asset, serialized.text->obj);
        auto requested = fs::absolute(destination).lexically_normal();
        if (requested.filename().empty()) requested = requested.parent_path();
        if (requested.filename().empty() || requested.filename() == "." || requested.filename() == "..")
            fail("obj.destination_path", "destination");
        const auto parent = fs::canonical(requested.parent_path());
        if (!fs::is_directory(parent)) fail("obj.destination_parent", "destination");
        const auto final_path = parent / requested.filename();
        if (present(final_path)) fail("obj.destination_exists", "destination");
        phase(ObjBundlePhase::staging);
        make_stage(stage, parent);
        write_bytes(stage.path, {asset.obj_path, output_text}, options.stop);
        for (const auto& resource : asset.resources) write_bytes(stage.path, resource, options.stop);
        for (const auto& file : options.supplemental_files) write_bytes(stage.path, file, options.stop);
        phase(ObjBundlePhase::verification);
        ObjFileOptions reload_options; reload_options.resource_root = stage.path; reload_options.stop = options.stop;
        const auto reload = read_obj_file(stage.path / asset.obj_path, reload_options);
        if (!reload.asset) {
            result.diagnostics.insert(result.diagnostics.end(), reload.diagnostics.begin(), reload.diagnostics.end());
            cancelled(options.stop);
            fail("obj.reload_failed", "staging");
        }
        const auto checked = write_obj(reload.asset->document);
        if (!checked.text || checked.text->obj != serialized.text->obj || checked.text->mtl != serialized.text->mtl ||
            reload.asset->obj_path != asset.obj_path || reload.asset->material_libraries != asset.material_libraries)
            fail("obj.roundtrip_mismatch", "staging");
        auto resources = asset.resources;
        std::sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) { return key(a.path) < key(b.path); });
        if (reload.asset->resources != resources) fail("obj.resource_mismatch", "staging");
        if (read_bytes(stage.path, asset.obj_path, options.stop) != output_text)
            fail("obj.roundtrip_mismatch", "OBJ bytes");
        for (const auto& file : options.supplemental_files)
            if (read_bytes(stage.path, file.path, options.stop) != file.bytes) fail("obj.resource_mismatch", key(file.path));
        verify_inventory(stage.path, expected);
        if (options.on_verified) {
            std::vector<ObjResource> files{{asset.obj_path, output_text}};
            files.insert(files.end(), resources.begin(), resources.end());
            files.insert(files.end(), options.supplemental_files.begin(), options.supplemental_files.end());
            std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return key(a.path) < key(b.path); });
            std::vector<ObjResource> supplements;
            cancelled(options.stop);
            try { supplements = options.on_verified(*reload.asset, files); }
            catch (const std::bad_alloc&) { throw; }
            catch (...) { fail("obj.verified_callback_failed", "callback"); }
            cancelled(options.stop);
            for (const auto& file : supplements) insert_path(expected, file.path);
            for (const auto& file : supplements) write_bytes(stage.path, file, options.stop);
            files.insert(files.end(), supplements.begin(), supplements.end());
            for (const auto& file : files)
                if (read_bytes(stage.path, file.path, options.stop) != file.bytes)
                    fail("obj.resource_mismatch", key(file.path));
            verify_inventory(stage.path, expected);
        }
        // Allocate the return path before committing; no fallible callback/allocation follows commit.
        result.entry = asset.obj_path;
        phase(ObjBundlePhase::publication);
        finalize(stage.path, final_path);
        stage.path.clear();
        result.outcome = ObjBundleOutcome::published;
        return result;
    } catch (const FileFailure& error) {
        result.diagnostics.push_back(error.diagnostic);
        if (error.diagnostic.code == "obj.cancelled") result.outcome = ObjBundleOutcome::cancelled;
    } catch (const fs::filesystem_error&) {
        result.diagnostics.push_back({"obj.filesystem_error", "bundle", std::nullopt});
    }
    result.entry.reset();
    stage.cleanup(&result.diagnostics);
    return result;
}
} // namespace meshvale::interchange
