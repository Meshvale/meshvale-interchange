// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <thread>

#include "gltf_metadata.h"

namespace meshvale::interchange::gltf_detail {
namespace {
constexpr std::string_view kBuffersKey = "buffers";
constexpr std::string_view kImagesKey = "images";
constexpr std::string_view kUriKey = "uri";
constexpr std::string_view kByteLengthKey = "byteLength";
constexpr std::string_view kBufferViewKey = "bufferView";
constexpr std::string_view kBufferViewsKey = "bufferViews";
constexpr std::string_view kBufferKey = "buffer";
constexpr std::string_view kByteOffsetKey = "byteOffset";
constexpr std::string_view kMimeTypeKey = "mimeType";
constexpr std::string_view kDataScheme = "data:";
constexpr std::string_view kPngMime = "image/png";
constexpr std::string_view kJpegMime = "image/jpeg";
struct EmbeddedRepresentation {
  std::string_view prefix;
  GltfResourceKind kind;
  std::string_view mime;
};
constexpr std::array<EmbeddedRepresentation, 4> kEmbeddedRepresentations = {
    {{"data:application/octet-stream;base64,", GltfResourceKind::kBuffer, {}},
     {"data:application/gltf-buffer;base64,", GltfResourceKind::kBuffer, {}},
     {"data:image/png;base64,", GltfResourceKind::kImage, kPngMime},
     {"data:image/jpeg;base64,", GltfResourceKind::kImage, kJpegMime}}};
constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t kBase64GroupCharacters = 4;
constexpr std::size_t kBase64GroupBytes = 3;
constexpr unsigned kBase64CharacterBits = 6;
constexpr std::size_t kCheckpointCopyBytes = 4096;
void ResourceFailure(GltfStatus status, GltfDiagnosticCode code,
                     GltfResourceKind kind, std::uint64_t index) {
  throw GltfFailure({status, code, std::nullopt, kind, index, std::nullopt, 0});
}
void BoundExtent(AssetState& state, std::uint64_t bytes, std::uint64_t& total) {
  if (bytes > state.limits.max_resource_bytes ||
      total > state.limits.max_owned_source_resource_bytes ||
      bytes > state.limits.max_owned_source_resource_bytes - total)
    Fail(GltfStatus::kUnsupportedProfile, GltfDiagnosticCode::kLimitExceeded);
  total += bytes;
}
void CopyBytes(AssetState& state, std::span<const std::byte> source,
               std::span<std::byte> destination) {
  const auto partition =
      static_cast<std::size_t>(state.work_policy.resource_partition_bytes);
  const auto jobs =
      source.size() / partition + (source.size() % partition != 0);
  auto lease = source.size() >= state.work_policy.parallel_min_resource_bytes
                   ? geometry::TryReserveWorkers(state.context, jobs)
                   : geometry::WorkerLease{};
  auto copy = [&](std::size_t start, std::size_t end) {
    while (start != end) {
      state.allocation.Checkpoint();
      const auto count = std::min(end - start, kCheckpointCopyBytes);
      std::memcpy(destination.data() + start, source.data() + start, count);
      start += count;
    }
  };
  if (lease.Count() < 2) {
    copy(0, source.size());
    return;
  }
  OwnedVector<std::exception_ptr> failures(
      jobs, std::exception_ptr{},
      Allocator<std::exception_ptr>(state.allocation));
  OwnedVector<std::jthread> workers{Allocator<std::jthread>(state.allocation)};
  workers.reserve(lease.Count());
  try {
    for (std::size_t worker = 0; worker < lease.Count(); ++worker)
      workers.emplace_back([&, worker] {
        for (auto job = worker; job < jobs; job += lease.Count()) {
          const auto start = job * partition;
          const auto end = start + std::min(partition, source.size() - start);
          try {
            copy(start, end);
          } catch (...) {
            failures[job] = std::current_exception();
          }
        }
      });
  } catch (...) {
    for (auto& worker : workers) worker.join();
    throw;
  }
  for (auto& worker : workers) worker.join();
  // Actual exceptions precede discovered expected cancellation failures.
  for (const auto& failure : failures) {
    if (!failure) continue;
    try {
      std::rethrow_exception(failure);
    } catch (const AdmissionFailure&) {
      continue;
    } catch (...) {
      throw;
    }
  }
  for (const auto& failure : failures)
    if (failure) std::rethrow_exception(failure);
}
bool DataLike(std::string_view uri) {
  if (uri.size() < kDataScheme.size()) return false;
  for (std::size_t i = 0; i < kDataScheme.size(); ++i) {
    auto c = uri[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != kDataScheme[i]) return false;
  }
  return true;
}
std::size_t Base64Size(std::string_view text,
                       const AllocationContext& allocation) {
  if (text.find('%') != std::string_view::npos)
    Fail(GltfStatus::kUnsupportedProfile,
         GltfDiagnosticCode::kUnsupportedResourceRepresentation);
  if (text.size() % kBase64GroupCharacters != 0)
    Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kEmbeddedUriSyntax);
  std::size_t padding = 0;
  if (!text.empty() && text.back() == '=') ++padding;
  if (text.size() > 1 && text[text.size() - 2] == '=') ++padding;
  for (std::size_t at = 0; at < text.size(); ++at) {
    allocation.Checkpoint();
    if (at >= text.size() - padding) continue;
    if (kBase64Alphabet.find(text[at]) == std::string_view::npos)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kEmbeddedUriSyntax);
  }
  constexpr unsigned kTwoPadLowBits = 0xf;
  constexpr unsigned kOnePadLowBits = 3;
  if (padding != 0) {
    const auto last = kBase64Alphabet.find(text[text.size() - padding - 1]);
    if ((last & (padding == 2 ? kTwoPadLowBits : kOnePadLowBits)) != 0)
      Fail(GltfStatus::kMalformed, GltfDiagnosticCode::kEmbeddedUriSyntax);
  }
  return text.size() / kBase64GroupCharacters * kBase64GroupBytes - padding;
}
void DecodeBase64(std::string_view text, std::span<std::byte> destination,
                  const AllocationContext& allocation) {
  std::size_t output = 0;
  for (std::size_t at = 0; at < text.size(); at += kBase64GroupCharacters) {
    allocation.Checkpoint();
    std::uint32_t bits = 0;
    for (std::size_t digit = 0; digit < kBase64GroupCharacters; ++digit)
      bits =
          (bits << kBase64CharacterBits) |
          static_cast<std::uint32_t>(
              text[at + digit] == '=' ? 0
                                      : kBase64Alphabet.find(text[at + digit]));
    constexpr unsigned kByteBits = 8;
    for (std::size_t byte = 0;
         byte < kBase64GroupBytes && output < destination.size(); ++byte)
      destination[output++] = static_cast<std::byte>(
          (bits >> ((kBase64GroupBytes - byte - 1) * kByteBits)) & 0xffu);
  }
}
void ImageSignature(const GltfResourceView& resource, std::string_view mime) {
  constexpr std::array<unsigned char, 8> kPngSignature = {137, 80, 78, 71,
                                                          13,  10, 26, 10};
  constexpr std::array<unsigned char, 3> kJpegSignature = {255, 216, 255};
  auto matches = [&](auto signature) {
    if (resource.bytes.size() < signature.size()) return false;
    for (std::size_t i = 0; i < signature.size(); ++i)
      if (std::to_integer<unsigned char>(resource.bytes[i]) != signature[i])
        return false;
    return true;
  };
  if ((mime == kPngMime && !matches(kPngSignature)) ||
      (mime == kJpegMime && !matches(kJpegSignature)))
    ResourceFailure(GltfStatus::kMalformed,
                    GltfDiagnosticCode::kEmbeddedMediaMismatch, resource.kind,
                    resource.table_index);
}
}  // namespace
void AdmitResources(AssetState& state,
                    std::span<const SuppliedGltfResource> supplied) {
  const auto& document = state.document;
  std::uint64_t total = document.Original().size();
  const auto buffers = FindMember(document, 0, kBuffersKey);
  const auto images = FindMember(document, 0, kImagesKey);
  auto table = [&](GltfResourceKind kind) {
    return kind == GltfResourceKind::kBuffer ? buffers : images;
  };
  for (std::size_t i = 0; i < supplied.size(); ++i) {
    const auto& binding = supplied[i];
    const auto rows = table(binding.kind);
    if (!rows || !ChildAt(document, *rows, binding.table_index))
      ResourceFailure(GltfStatus::kResourceBindingMismatch,
                      GltfDiagnosticCode::kExtraResource, binding.kind,
                      binding.table_index);
    for (std::size_t previous = 0; previous < i; ++previous)
      if (supplied[previous].kind == binding.kind &&
          supplied[previous].table_index == binding.table_index)
        ResourceFailure(GltfStatus::kResourceBindingMismatch,
                        GltfDiagnosticCode::kDuplicateResource, binding.kind,
                        binding.table_index);
  }
  constexpr std::array<GltfResourceKind, 2> kResourceOrder = {
      GltfResourceKind::kBuffer, GltfResourceKind::kImage};
  for (const auto kind : kResourceOrder) {
    const auto rows = table(kind);
    if (!rows) continue;
    std::uint64_t index = 0;
    for (auto node = document.FirstChild(*rows); node;
         node = document.NextSibling(*node), ++index) {
      state.allocation.Checkpoint();
      const auto uri_node = FindMember(document, *node, kUriKey);
      const auto uri =
          uri_node ? document.String(*uri_node) : std::string_view{};
      const auto view = FindMember(document, *node, kBufferViewKey);
      const SuppliedGltfResource* binding = nullptr;
      for (const auto& candidate : supplied)
        if (candidate.kind == kind && candidate.table_index == index)
          binding = &candidate;
      const auto length_node = FindMember(document, *node, kByteLengthKey);
      const auto logical =
          length_node ? ReadExactUnsigned(document, *length_node) : 0;
      GltfResourceView resource{kind, index, GltfResourceOrigin::kCallerBinding,
                                uri,  {},    logical,
                                {},   {}};
      std::string_view embedded_mime;
      if (view) {
        if (binding)
          ResourceFailure(GltfStatus::kResourceBindingMismatch,
                          GltfDiagnosticCode::kExtraResource, kind, index);
        const auto views = FindMember(document, 0, kBufferViewsKey);
        const auto view_index = ReadExactUnsigned(document, *view);
        const auto view_node =
            views ? ChildAt(document, *views, view_index) : std::nullopt;
        if (!view_node)
          ResourceFailure(GltfStatus::kMalformed,
                          GltfDiagnosticCode::kCoreReference, kind, index);
        const auto buffer_index = ReadExactUnsigned(
            document, *FindMember(document, *view_node, kBufferKey));
        const GltfResourceView* buffer = nullptr;
        for (const auto& candidate : state.resources)
          if (candidate.kind == GltfResourceKind::kBuffer &&
              candidate.table_index == buffer_index)
            buffer = &candidate;
        if (!buffer)
          ResourceFailure(GltfStatus::kMalformed,
                          GltfDiagnosticCode::kCoreReference, kind, index);
        const auto offset_node =
            FindMember(document, *view_node, kByteOffsetKey);
        const auto offset =
            offset_node ? ReadExactUnsigned(document, *offset_node) : 0;
        const auto extent = ReadExactUnsigned(
            document, *FindMember(document, *view_node, kByteLengthKey));
        if (offset > buffer->logical_bytes ||
            extent > buffer->logical_bytes - offset)
          ResourceFailure(GltfStatus::kMalformed,
                          GltfDiagnosticCode::kBinaryLayout, kind, index);
        resource.origin = GltfResourceOrigin::kBufferView;
        resource.bytes = buffer->bytes.subspan(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(extent));
        resource.logical_bytes = extent;
        resource.buffer_index = buffer_index;
        resource.buffer_view_index = view_index;
      } else {
        std::string_view encoded;
        std::optional<std::size_t> prefix;
        if (DataLike(uri)) {
          for (std::size_t i = 0; i < kEmbeddedRepresentations.size(); ++i)
            if (uri.starts_with(kEmbeddedRepresentations[i].prefix)) prefix = i;
          if (!prefix)
            ResourceFailure(
                GltfStatus::kUnsupportedProfile,
                GltfDiagnosticCode::kUnsupportedResourceRepresentation, kind,
                index);
          if (kind != kEmbeddedRepresentations[*prefix].kind)
            ResourceFailure(GltfStatus::kMalformed,
                            GltfDiagnosticCode::kEmbeddedMediaMismatch, kind,
                            index);
          if (binding)
            ResourceFailure(GltfStatus::kResourceBindingMismatch,
                            GltfDiagnosticCode::kExtraResource, kind, index);
          encoded = uri.substr(kEmbeddedRepresentations[*prefix].prefix.size());
          embedded_mime = kEmbeddedRepresentations[*prefix].mime;
          resource.origin = GltfResourceOrigin::kDecodedDataUri;
        } else if (!binding) {
          ResourceFailure(GltfStatus::kResourceBindingMismatch,
                          GltfDiagnosticCode::kMissingResource, kind, index);
        }
        const auto bytes = prefix ? Base64Size(encoded, state.allocation)
                                  : binding->bytes.size();
        BoundExtent(state, bytes, total);
        if (logical > bytes)
          ResourceFailure(GltfStatus::kResourceBindingMismatch,
                          GltfDiagnosticCode::kResourceExtent, kind, index);
        state.resource_storage.emplace_back(
            Allocator<std::byte>(state.allocation));
        auto& destination = state.resource_storage.back();
        destination.resize(bytes);
        if (prefix)
          DecodeBase64(encoded, destination, state.allocation);
        else
          CopyBytes(state, binding->bytes, destination);
        resource.bytes = destination;
        if (kind == GltfResourceKind::kImage) resource.logical_bytes = bytes;
      }
      if (kind == GltfResourceKind::kImage) {
        const auto mime = FindMember(document, *node, kMimeTypeKey);
        if (mime && !embedded_mime.empty() &&
            document.String(*mime) != embedded_mime)
          ResourceFailure(GltfStatus::kMalformed,
                          GltfDiagnosticCode::kEmbeddedMediaMismatch, kind,
                          index);
        if (mime)
          ImageSignature(resource, document.String(*mime));
        else if (!embedded_mime.empty())
          ImageSignature(resource, embedded_mime);
      }
      state.resources.push_back(resource);
    }
  }
}
}  // namespace meshvale::interchange::gltf_detail
