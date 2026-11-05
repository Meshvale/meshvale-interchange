// SPDX-License-Identifier: Apache-2.0
#include "surface_python_publication.h"

#include <Python.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "../src/surface_files_internal.h"

namespace meshvale::interchange::surface_python_detail {
namespace nb = nanobind;
namespace alloc = gltf_detail;
namespace files = surface_files_detail;
namespace {
constexpr char kNativeDestination[] = "_native_destination";
constexpr char kReceiptPath[] = "receipt_path";
constexpr Py_UCS4 kAsciiMaximum = 0x7f;
#ifdef _WIN32
constexpr Py_UCS4 kHighSurrogateBegin = 0xd800;
constexpr Py_UCS4 kHighSurrogateEnd = 0xdbff;
constexpr Py_UCS4 kLowSurrogateBegin = 0xdc00;
constexpr Py_UCS4 kLowSurrogateEnd = 0xdfff;
constexpr Py_ssize_t kDriveRootCharacters = 3;
static_assert(sizeof(wchar_t) == 2);
static_assert(std::numeric_limits<Py_ssize_t>::max() ==
              std::numeric_limits<std::ptrdiff_t>::max());
#endif

nb::object Attribute(nb::handle value, const char* name) {
  auto out = nb::steal<nb::object>(PyObject_GetAttrString(value.ptr(), name));
  if (!out.is_valid()) throw nb::python_error();
  return out;
}
std::size_t Required(std::size_t payload) {
  if (payload >=
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    throw std::overflow_error("surface publication terminated extent");
  return payload + 1;
}
std::size_t HostBytes(std::size_t count, std::size_t width) {
  const auto bytes = alloc::CheckedProduct(count, width);
  if (bytes >
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    throw std::overflow_error("surface publication native byte extent");
  return bytes;
}
void Bound(std::size_t count, std::uint64_t cap) {
  if (count > cap)
    throw nb::value_error("surface publication input exceeds selected cap");
}
void ReadyUnicode(nb::handle value) {
  if (!PyUnicode_CheckExact(value.ptr()))
    throw nb::type_error("surface publication requires built-in str");
#if PY_VERSION_HEX < 0x030c0000
  if (!PyUnicode_IS_READY(value.ptr()))
    throw nb::value_error("surface publication snapshot is not ready");
#endif
}
Py_UCS4 Scalar(nb::handle value, Py_ssize_t index) noexcept {
  return PyUnicode_READ(PyUnicode_KIND(value.ptr()),
                        PyUnicode_DATA(value.ptr()), index);
}
std::string_view Receipt(nb::handle value, std::uint64_t cap) {
  ReadyUnicode(value);
  const auto size = PyUnicode_GET_LENGTH(value.ptr());
  Bound(static_cast<std::size_t>(size), cap);
  for (Py_ssize_t i = 0; i < size; ++i)
    if (Scalar(value, i) > kAsciiMaximum)
      throw nb::value_error("surface receipt requires portable ASCII");
  // Canonical all-ASCII Unicode uses one-byte storage. Borrow it without
  // creating/caching a full UTF-8 representation; char may alias its bytes.
  if (size && PyUnicode_KIND(value.ptr()) != PyUnicode_1BYTE_KIND)
    throw std::logic_error("surface ASCII receipt representation");
  return {reinterpret_cast<const char*>(PyUnicode_DATA(value.ptr())),
          static_cast<std::size_t>(size)};
}
void ReceiptSyntax(std::string_view value, std::uint64_t cap) {
  try {
    files::PortablePath(value, cap);
  } catch (const files::Failure&) {
    throw nb::value_error("surface receipt path is not admitted");
  }
}
void NativeSyntax(files::NativeView value, std::uint64_t cap) {
  try {
    files::ValidateNativeDestination(value, cap);
  } catch (const files::Failure&) {
    throw nb::value_error("surface native destination is not admitted");
  }
}
#ifdef _WIN32
bool Separator(Py_UCS4 value) noexcept { return value == '/' || value == '\\'; }
bool DriveLetter(Py_UCS4 value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}
[[noreturn]] void InvalidDestination() {
  throw nb::value_error("surface native destination is not admitted");
}
// Pure scalar admission before native backing exists. ASCII syntax is exactly
// the native DestinationSyntax grammar; UTF-16 pairs are checked without making
// a wchar cache. The actual copied native view is checked by that owner again.
void WindowsSyntax(nb::handle value) {
  const auto size = PyUnicode_GET_LENGTH(value.ptr());
  if (!size) InvalidDestination();
  for (Py_ssize_t i = 0; i < size; ++i) {
    const auto scalar = Scalar(value, i);
    if (!scalar) InvalidDestination();
    if (scalar >= kHighSurrogateBegin && scalar <= kHighSurrogateEnd) {
      if (++i == size) InvalidDestination();
      const auto low = Scalar(value, i);
      if (low < kLowSurrogateBegin || low > kLowSurrogateEnd)
        InvalidDestination();
    } else if (scalar >= kLowSurrogateBegin && scalar <= kLowSurrogateEnd)
      InvalidDestination();
  }
  const bool drive = size >= kDriveRootCharacters &&
                     DriveLetter(Scalar(value, 0)) && Scalar(value, 1) == ':' &&
                     Separator(Scalar(value, 2));
  Py_ssize_t start = drive ? kDriveRootCharacters : 0;
  if ((!drive && Separator(Scalar(value, 0))) || start == size)
    InvalidDestination();
  for (Py_ssize_t i = start; i <= size; ++i) {
    if (i < size && !Separator(Scalar(value, i))) continue;
    const auto count = i - start;
    if (!count || (count == 1 && Scalar(value, start) == '.') ||
        (count == 2 && Scalar(value, start) == '.' &&
         Scalar(value, start + 1) == '.'))
      InvalidDestination();
    const auto last = Scalar(value, i - 1);
    if (last == '.' || last == ' ') InvalidDestination();
    for (auto at = start; at < i; ++at) {
      const auto c = Scalar(value, at);
      if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
          c == '?' || c == '*')
        InvalidDestination();
    }
    start = i + 1;
  }
}
#endif
std::size_t NativeUnits(nb::handle snapshot, std::uint64_t cap) {
#ifdef _WIN32
  ReadyUnicode(snapshot);
  const auto length =
      static_cast<std::size_t>(PyUnicode_GET_LENGTH(snapshot.ptr()));
  const auto minimum = Required(length);
  Bound(minimum, cap);
  // Even one wchar per code point must fit the actual Bridge host extent.
  // This checked lower bound also makes the query's possible two-unit expansion
  // plus terminator representable in Py_ssize_t on the asserted Windows ABI.
  (void)HostBytes(minimum, sizeof(wchar_t));
  const auto required = PyUnicode_AsWideChar(snapshot.ptr(), nullptr, 0);
  if (required < 0) throw nb::python_error();
  if (!required)
    throw std::logic_error("surface wchar query has no terminator");
  (void)HostBytes(static_cast<std::size_t>(required), sizeof(wchar_t));
  WindowsSyntax(snapshot);
  return static_cast<std::size_t>(required - 1);
#else
  if (!PyBytes_CheckExact(snapshot.ptr()))
    throw nb::type_error("surface native snapshot requires built-in bytes");
  const auto size = static_cast<std::size_t>(PyBytes_GET_SIZE(snapshot.ptr()));
  Bound(Required(size), cap);
  return size;
#endif
}
SurfacePublicationPlan Plan(nb::handle snapshot, nb::handle receipt,
                            std::uint64_t native_cap,
                            std::uint64_t receipt_cap) {
  SurfacePublicationPlan plan{NativeUnits(snapshot, native_cap), 0};
  Bound(Required(plan.native_units), native_cap);
#ifndef _WIN32
  NativeSyntax({PyBytes_AS_STRING(snapshot.ptr()), plan.native_units},
               native_cap);
#endif
  const auto text = Receipt(receipt, receipt_cap);
  ReceiptSyntax(text, receipt_cap);
  plan.receipt_bytes = text.size();
  const auto native_bytes = HostBytes(
      Required(plan.native_units), sizeof(std::filesystem::path::value_type));
  const auto maximum =
      static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
  if (plan.receipt_bytes > maximum - native_bytes)
    throw std::overflow_error("surface publication aggregate input extent");
  return plan;
}
}  // namespace

nb::object SnapshotSurfaceDestination(nb::handle resolved) {
  nb::object snapshot;
#ifdef _WIN32
  if (PyBytes_Check(resolved.ptr())) {
    snapshot = nb::steal<nb::object>(PyUnicode_DecodeFSDefaultAndSize(
        PyBytes_AS_STRING(resolved.ptr()), PyBytes_GET_SIZE(resolved.ptr())));
    if (!snapshot.is_valid()) throw nb::python_error();
  } else if (PyUnicode_Check(resolved.ptr()))
    snapshot = nb::borrow<nb::object>(resolved);
  else
    throw nb::type_error("surface destination must resolve to str or bytes");
  snapshot = nb::steal<nb::object>(PyUnicode_FromObject(snapshot.ptr()));
  if (!snapshot.is_valid()) throw nb::python_error();
#if PY_VERSION_HEX < 0x030c0000
  if (PyUnicode_READY(snapshot.ptr()) < 0) throw nb::python_error();
#endif
  const auto size = PyUnicode_GET_LENGTH(snapshot.ptr());
  if (!size) throw nb::value_error("surface native destination is empty");
  for (Py_ssize_t i = 0; i < size; ++i)
    if (!Scalar(snapshot, i))
      throw nb::value_error("surface native destination contains NUL");
#else
  if (PyUnicode_Check(resolved.ptr())) {
    snapshot = nb::steal<nb::object>(PyUnicode_EncodeFSDefault(resolved.ptr()));
    if (!snapshot.is_valid()) throw nb::python_error();
  } else if (PyBytes_Check(resolved.ptr()))
    snapshot = nb::borrow<nb::object>(resolved);
  else
    throw nb::type_error("surface destination must resolve to str or bytes");
  if (!PyBytes_CheckExact(snapshot.ptr())) {
    snapshot = nb::steal<nb::object>(PyBytes_FromStringAndSize(
        PyBytes_AS_STRING(snapshot.ptr()), PyBytes_GET_SIZE(snapshot.ptr())));
    if (!snapshot.is_valid()) throw nb::python_error();
  }
  const auto size = static_cast<std::size_t>(PyBytes_GET_SIZE(snapshot.ptr()));
  if (!size || std::memchr(PyBytes_AS_STRING(snapshot.ptr()), 0, size))
    throw nb::value_error(
        "surface native destination is empty or contains NUL");
#endif
  return snapshot;
}
SurfacePublicationPlan ValidateSurfacePublication(
    nb::handle value, const SurfacePresetProjection& preset,
    SurfaceEncoding encoding) {
  const auto snapshot = Attribute(value, kNativeDestination);
  const auto receipt = Attribute(value, kReceiptPath);
  const auto& limits = preset.For(encoding).publication_limits.files;
  return Plan(snapshot, receipt,
              std::min(preset.ingress_limits.max_destination_native_code_units,
                       limits.max_native_path_code_units),
              limits.max_path_bytes);
}
OwnedSurfacePublication::OwnedSurfacePublication(
    const SurfacePublicationPlan& plan, nb::handle value,
    const alloc::AllocationContext& bridge)
    : destination_(alloc::Allocator<NativeChar>(bridge)),
      receipt_(alloc::Allocator<char>(bridge)) {
  if (!bridge.HasRequestLimit())
    throw std::invalid_argument("surface publication requires a capped bridge");
  const auto snapshot = Attribute(value, kNativeDestination);
  const auto receipt = Attribute(value, kReceiptPath);
  const auto current =
      Plan(snapshot, receipt, Required(plan.native_units), plan.receipt_bytes);
  if (current.native_units != plan.native_units ||
      current.receipt_bytes != plan.receipt_bytes)
    throw std::logic_error("surface publication changed after validation");
  destination_.resize(Required(plan.native_units));
  receipt_.resize(plan.receipt_bytes);
#ifdef _WIN32
  const auto filled =
      PyUnicode_AsWideChar(snapshot.ptr(), destination_.data(),
                           static_cast<Py_ssize_t>(destination_.size()));
  if (filled < 0) throw nb::python_error();
  if (static_cast<std::size_t>(filled) != plan.native_units ||
      destination_.back() != 0)
    throw std::logic_error("surface native path direct fill mismatch");
#else
  std::memcpy(destination_.data(), PyBytes_AS_STRING(snapshot.ptr()),
              plan.native_units);
  destination_.back() = 0;
#endif
  const auto text = Receipt(receipt, plan.receipt_bytes);
  std::memcpy(receipt_.data(), text.data(), text.size());
  NativeSyntax({destination_.data(), plan.native_units}, destination_.size());
  ReceiptSyntax({receipt_.data(), receipt_.size()}, receipt_.size());
}
SurfacePublicationInput OwnedSurfacePublication::Input() const noexcept {
  return {
      SurfaceNativeDestination{{destination_.data(), destination_.size() - 1}},
      {receipt_.data(), receipt_.size()}};
}
}  // namespace meshvale::interchange::surface_python_detail
