// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/python/record.hpp>
#include <meshvale/interchange/obj_files.hpp>
#include <nanobind/stl/filesystem.h>
#include <stdexcept>

namespace nb = nanobind;
namespace geo = meshvale::geometry;
namespace io = meshvale::interchange;
namespace fs = std::filesystem;

namespace {
std::string bytes(nb::handle value) {
    if (!PyBytes_Check(value.ptr())) throw nb::type_error("expected bytes");
    return {PyBytes_AS_STRING(value.ptr()),static_cast<std::size_t>(PyBytes_GET_SIZE(value.ptr()))};
}
std::string text(nb::handle value) {
    if (PyBytes_Check(value.ptr())) return bytes(value);
    if (!PyUnicode_Check(value.ptr())) throw nb::type_error("text must be str or bytes");
    auto encoded = nb::steal<nb::object>(PyUnicode_AsEncodedString(value.ptr(),"utf-8","surrogateescape"));
    if (!encoded.is_valid()) throw nb::python_error();
    return bytes(encoded);
}
nb::object string(const std::string& value) {
    auto decoded = nb::steal<nb::object>(PyUnicode_DecodeUTF8(value.data(),
        static_cast<Py_ssize_t>(value.size()),"surrogateescape"));
    if (!decoded.is_valid()) throw nb::python_error();
    return decoded;
}
nb::bytes binary(const std::string& value) { return nb::bytes(value.data(),value.size()); }
fs::path path(nb::handle value) {
    auto resolved = nb::steal<nb::object>(PyOS_FSPath(value.ptr()));
    if (!resolved.is_valid()) throw nb::python_error();
    if (PyUnicode_Check(resolved.ptr())) {
        const auto found = PyUnicode_FindChar(resolved.ptr(),0,0,PyUnicode_GetLength(resolved.ptr()),1);
        if (found == -2) throw nb::python_error();
        if (found >= 0) throw nb::value_error("path contains NUL");
    } else if (PyBytes_Check(resolved.ptr())) {
        if (std::memchr(PyBytes_AS_STRING(resolved.ptr()),0,
                static_cast<std::size_t>(PyBytes_GET_SIZE(resolved.ptr()))))
            throw nb::value_error("path contains NUL");
    } else throw nb::type_error("path must resolve to str or bytes");
    return nb::cast<fs::path>(resolved);
}
nb::list list(nb::handle value) {
    if (!PyList_Check(value.ptr())) throw nb::type_error("expected list");
    return nb::borrow<nb::list>(value);
}
nb::list diagnostics(const std::vector<geo::Diagnostic>& values) {
    nb::list result;
    for (const auto& value : values) {
        nb::dict item;
        item["code"] = string(value.code); item["subject"] = string(value.subject);
        item["element"] = value.element ? nb::cast(*value.element) : nb::none();
        result.append(item);
    }
    return result;
}
io::ObjDocument document(nb::handle value) {
    const auto record = geo::python::exact_dict(value,{"mesh_record","parts","material_names","material_library"});
    io::ObjDocument result;
    result.mesh = geo::python::from_record(record["mesh_record"]);
    for (auto item : list(record["parts"])) {
        const auto part = geo::python::exact_dict(item,{"object","groups"});
        io::ObjPart native{ text(part["object"]),{} };
        for (auto group : list(part["groups"])) native.groups.push_back(text(group));
        result.parts.push_back(std::move(native));
    }
    for (auto name : list(record["material_names"])) result.material_names.push_back(text(name));
    result.material_library = bytes(record["material_library"]);
    return result;
}
nb::dict document(const io::ObjDocument& value) {
    nb::dict result;
    result["mesh_record"] = geo::python::to_record(value.mesh);
    nb::list parts;
    for (const auto& part : value.parts) {
        nb::dict item; nb::list groups;
        for (const auto& group : part.groups) groups.append(string(group));
        item["object"] = string(part.object); item["groups"] = groups; parts.append(item);
    }
    nb::list names;
    for (const auto& name : value.material_names) names.append(string(name));
    result["parts"] = parts; result["material_names"] = names;
    result["material_library"] = binary(value.material_library);
    return result;
}
io::ObjResource resource(nb::handle value) {
    const auto record = geo::python::exact_dict(value,{"path","bytes"});
    return {path(record["path"]),bytes(record["bytes"])};
}
io::ObjFileAsset asset(nb::handle value) {
    const auto record = geo::python::exact_dict(value,{"document","obj_path","material_libraries","resources"});
    io::ObjFileAsset result;
    result.document = document(record["document"]); result.obj_path = path(record["obj_path"]);
    for (auto item : list(record["material_libraries"])) result.material_libraries.push_back(path(item));
    for (auto item : list(record["resources"])) result.resources.push_back(resource(item));
    return result;
}
nb::dict asset(const io::ObjFileAsset& value) {
    nb::dict result;
    result["document"] = document(value.document); result["obj_path"] = nb::cast(value.obj_path);
    nb::list libraries, resources;
    for (const auto& library : value.material_libraries) libraries.append(nb::cast(library));
    for (const auto& file : value.resources) {
        nb::dict item; item["path"] = nb::cast(file.path); item["bytes"] = binary(file.bytes);
        resources.append(item);
    }
    result["material_libraries"] = libraries; result["resources"] = resources;
    return result;
}
class Cancellation {
    std::stop_source source_;
public:
    bool request_stop() { return source_.request_stop(); }
    bool stop_requested() const { return source_.stop_requested(); }
    std::stop_token token() const { return source_.get_token(); }
};
std::stop_token stop(Cancellation* cancellation) {
    return cancellation ? cancellation->token() : std::stop_token{};
}
const char* phase(io::ObjBundlePhase value) {
    switch (value) {
        case io::ObjBundlePhase::preflight: return "preflight";
        case io::ObjBundlePhase::staging: return "staging";
        case io::ObjBundlePhase::verification: return "verification";
        case io::ObjBundlePhase::publication: return "publication";
    }
    throw std::logic_error("unknown native publication phase");
}
nb::dict read_text(nb::handle obj, nb::handle mtl) {
    const auto obj_text = text(obj), mtl_text = text(mtl);
    io::ObjImportResult native;
    { nb::gil_scoped_release release; native = io::read_obj(obj_text,mtl_text); }
    nb::dict result; result["document"] = nb::none();
    if (native.document) result["document"] = document(*native.document);
    result["diagnostics"] = diagnostics(native.diagnostics);
    return result;
}
nb::dict write_text(nb::handle value) {
    const auto input = document(value);
    io::ObjExportResult native;
    { nb::gil_scoped_release release; native = io::write_obj(input); }
    nb::dict result; result["text"] = nb::none();
    if (native.text) {
        nb::dict output; output["obj"] = string(native.text->obj); output["mtl"] = binary(native.text->mtl);
        result["text"] = output;
    }
    result["diagnostics"] = diagnostics(native.diagnostics);
    return result;
}
nb::dict read_file(nb::handle input, nb::handle root, Cancellation* cancellation) {
    const auto source = path(input);
    io::ObjFileOptions options;
    if (!root.is_none()) options.resource_root = path(root);
    options.stop = stop(cancellation);
    io::ObjFileResult native;
    { nb::gil_scoped_release release; native = io::read_obj_file(source,options); }
    nb::dict result; result["asset"] = nb::none();
    if (native.asset) result["asset"] = asset(*native.asset);
    result["diagnostics"] = diagnostics(native.diagnostics);
    return result;
}
nb::dict publish(nb::handle value, nb::handle destination, Cancellation* cancellation,
                 nb::object callback, const nb::list& supplemental, nb::object verified_callback) {
    const auto input = asset(value); const auto output = path(destination);
    io::ObjBundleOptions options; options.stop = stop(cancellation);
    for (auto item : supplemental) options.supplemental_files.push_back(resource(item));
    if (!verified_callback.is_none()) {
        if (!PyCallable_Check(verified_callback.ptr())) throw nb::type_error("on_verified must be callable or None");
        options.on_verified = [&verified_callback](const io::ObjFileAsset& value, const std::vector<io::ObjResource>& files) {
            nb::gil_scoped_acquire acquire;
            nb::list payload;
            for (const auto& file : files) {
                nb::dict item; item["path"] = nb::cast(file.path); item["bytes"] = binary(file.bytes);
                payload.append(item);
            }
            try {
                auto returned = verified_callback(asset(value), payload);
                std::vector<io::ObjResource> result;
                for (auto item : list(returned)) result.push_back(resource(item));
                return result;
            } catch (const nb::python_error&) { throw std::runtime_error("Python verified callback raised"); }
        };
    }
    if (!callback.is_none()) {
        if (!PyCallable_Check(callback.ptr())) throw nb::type_error("on_phase must be callable or None");
        options.on_phase = [&callback](io::ObjBundlePhase value) {
            nb::gil_scoped_acquire acquire;
            try { callback(phase(value)); }
            catch (const nb::python_error&) { throw std::runtime_error("Python progress callback raised"); }
        };
    }
    // Prepare Python success storage before the native no-replace filesystem commit.
    nb::dict success;
    success["outcome"] = "published"; success["phase"] = "publication";
    success["entry"] = nb::cast(input.obj_path); success["diagnostics"] = nb::list();
    io::ObjBundleResult native;
    { nb::gil_scoped_release release; native = io::publish_obj_bundle(input,output,options); }
    if (native.outcome == io::ObjBundleOutcome::published) return success;
    nb::dict result;
    result["outcome"] = native.outcome == io::ObjBundleOutcome::cancelled ? "cancelled" : "failed";
    result["phase"] = phase(native.phase); result["entry"] = nb::none();
    result["diagnostics"] = diagnostics(native.diagnostics);
    return result;
}
}

NB_MODULE(_interchange, module) {
    nb::class_<Cancellation>(module,"Cancellation")
        .def(nb::init<>()).def("request_stop",&Cancellation::request_stop)
        .def_prop_ro("stop_requested",&Cancellation::stop_requested);
    module.def("read_text",&read_text);
    module.def("write_text",&write_text);
    module.def("read_file",&read_file,nb::arg("input"),nb::arg("root").none(),nb::arg("cancellation").none());
    module.def("publish",&publish,nb::arg("asset"),nb::arg("destination"),nb::arg("cancellation").none(),
        nb::arg("callback").none(),nb::arg("supplemental"),nb::arg("verified_callback").none());
}
