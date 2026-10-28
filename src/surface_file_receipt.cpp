// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/surface_files.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "surface_files_internal.h"
#include "surface_output_internal.h"
namespace meshvale::interchange::surface_files_detail {
namespace {
using Status = SurfaceFileStatus;
using Stage = SurfaceFileStage;
using Code = SurfaceFileCode;
constexpr std::string_view kHex = "0123456789abcdef";
constexpr unsigned char kAsciiLimit = 0x80;
constexpr unsigned char kTwoLeadMin = 0xc2, kTwoLeadMax = 0xdf;
constexpr unsigned char kThreeLeadMin = 0xe0, kThreeLeadMax = 0xef;
constexpr unsigned char kFourLeadMin = 0xf0, kFourLeadMax = 0xf4;
constexpr unsigned char kTwoLeadMask = 0x1f, kThreeLeadMask = 0x0f;
constexpr unsigned char kFourLeadMask = 0x07;
constexpr unsigned char kContinuationMask = 0xc0, kContinuationTag = 0x80;
constexpr unsigned char kContinuationPayloadMask = 0x3f;
constexpr unsigned kContinuationBits = 6;
constexpr std::uint32_t kTwoScalarMin = 0x80, kThreeScalarMin = 0x800;
constexpr std::uint32_t kFourScalarMin = 0x10000, kUnicodeMax = 0x10ffff;
constexpr std::uint32_t kSurrogateBegin = 0xd800, kSurrogateEnd = 0xdfff;
constexpr unsigned char kJsonControlLimit = 0x20;
constexpr unsigned kNibbleBits = 4, kNibbleMask = 0x0f;
constexpr std::size_t kDecimalU64Digits = 20;
void Utf8(std::span<const std::byte> bytes,
          const allocation::AllocationContext& a) {
  std::size_t i = 0;
  while (i < bytes.size()) {
    a.Checkpoint();
    auto c = std::to_integer<unsigned char>(bytes[i++]);
    if (c < kAsciiLimit) continue;
    std::size_t tails;
    std::uint32_t value, minimum;
    if (c >= kTwoLeadMin && c <= kTwoLeadMax) {
      tails = 1;
      value = c & kTwoLeadMask;
      minimum = kTwoScalarMin;
    } else if (c >= kThreeLeadMin && c <= kThreeLeadMax) {
      tails = 2;
      value = c & kThreeLeadMask;
      minimum = kThreeScalarMin;
    } else if (c >= kFourLeadMin && c <= kFourLeadMax) {
      tails = 3;
      value = c & kFourLeadMask;
      minimum = kFourScalarMin;
    } else
      Fail(Status::kInvalidStorage, Stage::kReceipt,
           Code::kCallerReportEncoding);
    if (tails > bytes.size() - i)
      Fail(Status::kInvalidStorage, Stage::kReceipt,
           Code::kCallerReportEncoding);
    for (std::size_t t = 0; t < tails; ++t) {
      auto b = std::to_integer<unsigned char>(bytes[i++]);
      if ((b & kContinuationMask) != kContinuationTag)
        Fail(Status::kInvalidStorage, Stage::kReceipt,
             Code::kCallerReportEncoding);
      value = (value << kContinuationBits) | (b & kContinuationPayloadMask);
    }
    if (value < minimum || value > kUnicodeMax ||
        (value >= kSurrogateBegin && value <= kSurrogateEnd))
      Fail(Status::kInvalidStorage, Stage::kReceipt,
           Code::kCallerReportEncoding);
  }
}
class Json {
 public:
  Json(PublishedState& s, std::byte* out) : state_(s), out_(out) {}
  void Raw(std::string_view text) {
    for (char c : text) Put(c);
  }
  void Text(std::string_view text) {
    Put('"');
    for (unsigned char c : text) {
      state_.allocation.Checkpoint();
      if (c == '"' || c == '\\') {
        Put('\\');
        Put(static_cast<char>(c));
      } else if (c < kJsonControlLimit) {
        Raw("\\u00");
        Put(kHex[c >> kNibbleBits]);
        Put(kHex[c & kNibbleMask]);
      } else
        Put(static_cast<char>(c));
    }
    Put('"');
  }
  void Number(std::uint64_t n) {
    std::array<char, kDecimalU64Digits> digits{};
    auto end = std::to_chars(digits.data(), digits.data() + digits.size(), n);
    Text({digits.data(), static_cast<std::size_t>(end.ptr - digits.data())});
  }
  void Hash(const SurfaceDigest& digest) {
    Put('"');
    for (auto b : digest) {
      auto n = std::to_integer<unsigned char>(b);
      Put(kHex[n >> kNibbleBits]);
      Put(kHex[n & kNibbleMask]);
    }
    Put('"');
  }
  std::size_t Size() const noexcept { return size_; }

 private:
  void Put(char c) {
    if (size_ >= state_.limits.files.max_receipt_bytes)
      Fail(Status::kLimitExceeded, Stage::kReceipt, Code::kReceiptSize);
    if (out_)
      out_[size_] = static_cast<std::byte>(static_cast<unsigned char>(c));
    ++size_;
  }
  PublishedState& state_;
  std::byte* out_;
  std::size_t size_ = 0;
};
std::string_view Encoding(SurfaceEncoding e) {
  switch (e) {
    case SurfaceEncoding::kGltfJson:
      return "gltf-json";
    case SurfaceEncoding::kGlb:
      return "glb";
    case SurfaceEncoding::kStlAscii:
      return "stl-ascii";
    case SurfaceEncoding::kStlBinary:
      return "stl-binary";
    default:
      Fail(Status::kInvalidStorage, Stage::kReceipt, Code::kSourceContract);
  }
}
std::string_view Kind(SurfaceMemberKind k) {
  switch (k) {
    case SurfaceMemberKind::kGltf:
      return "gltf";
    case SurfaceMemberKind::kGlb:
      return "glb";
    case SurfaceMemberKind::kStl:
      return "stl";
    case SurfaceMemberKind::kGeometryBuffer:
      return "geometry-buffer";
    case SurfaceMemberKind::kImage:
      return "image";
    case SurfaceMemberKind::kCapsuleJson:
      return "capsule-json";
    case SurfaceMemberKind::kCapsuleBinary:
      return "capsule-binary";
  }
  Fail(Status::kInvalidStorage, Stage::kReceipt, Code::kSourceContract);
}
void Emit(Json& j, PublishedState& s, bool report,
          const SurfaceDigest& report_hash) {
  j.Raw("{\"schema\":");
  j.Text(kSurfacePublicationSchema);
  j.Raw(",\"phase\":\"staged-payload-verified\",\"encoding\":");
  j.Text(Encoding(s.input.Expected().Encoding()));
  j.Raw(",\"representation\":");
  j.Text(s.input.Expected().Representation() ==
                 SurfaceRepresentation::kStrictBare
             ? "strict-bare"
             : "annotated-bundle-v1");
  j.Raw(",\"entry_path\":");
  j.Text(EntryPath(s.input.Expected().Encoding()));
  j.Raw(",\"payload_members\":[");
  bool comma = false;
  for (const auto& m : s.members) {
    if (comma) j.Raw(",");
    comma = true;
    j.Raw("{\"kind\":");
    j.Text(Kind(m.kind));
    j.Raw(",\"path\":");
    j.Text(m.logical);
    j.Raw(",\"byte_length\":");
    j.Number(m.expected.size());
    j.Raw(",\"sha256\":");
    j.Hash(m.digest);
    j.Raw("}");
  }
  j.Raw(
      "],\"observations\":{\"payload_write\":{\"status\":\"complete\",\"bytes_"
      "written\":");
  j.Number(s.facts.payload_write->bytes_written);
  j.Raw(",\"closed_members\":");
  j.Number(s.facts.payload_write->completed_files);
  j.Raw("},\"payload_reread\":{\"status\":\"complete\",\"bytes_read\":");
  j.Number(s.facts.payload_reread->bytes_read);
  j.Raw(",\"members\":");
  j.Number(s.facts.payload_reread->completed_files);
  j.Raw("},\"fresh_inspection\":{\"status\":\"success\",\"members\":");
  j.Number(s.inspection->CapturedMembers().size());
  j.Raw("}},\"caller_report\":");
  if (!report)
    j.Raw("null");
  else {
    j.Raw("{\"encoding\":\"utf-8\",\"byte_length\":");
    j.Number(s.report.size());
    j.Raw(",\"sha256\":");
    j.Hash(report_hash);
    j.Raw(",\"text\":");
    j.Text(
        {s.report.size() ? reinterpret_cast<const char*>(s.report.data()) : "",
         s.report.size()});
    j.Raw("}");
  }
  j.Raw("}\n");
}
}  // namespace
std::string_view EntryPath(SurfaceEncoding encoding) {
  switch (encoding) {
    case SurfaceEncoding::kInvalid:
      break;
    case SurfaceEncoding::kGltfJson:
      return "surface.gltf";
    case SurfaceEncoding::kGlb:
      return "surface.glb";
    case SurfaceEncoding::kStlAscii:
    case SurfaceEncoding::kStlBinary:
      return "surface.stl";
  }
  Fail(Status::kInvalidStorage, Stage::kReceipt, Code::kSourceContract);
}
SurfaceDigest Digest(std::span<const std::byte> bytes, PublishedState& s) {
  surface_detail::FactsState facts(s.allocation);
  const surface_detail::TestOptions testing;
  surface_detail::Run run(s.context, s.allocation, s.limits.inspection.counts,
                          s.work.inspection, facts, s.stop, testing);
  run.stage = SurfaceStage::kCapture;
  try {
    return surface_detail::Digest(bytes, run);
  } catch (const surface_detail::Failure& e) {
    Fail(e.Status() == SurfaceStatus::kCanceled ? Status::kCanceled
                                                : Status::kLimitExceeded,
         Stage::kReceipt,
         e.Status() == SurfaceStatus::kCanceled ? Code::kStopRequested
                                                : Code::kCountLimit);
  }
}
void Receipt(PublishedState& s, bool report_present) {
  SurfaceDigest report_hash{};
  if (report_present) {
    Utf8(s.report, s.allocation);
    report_hash = Digest(s.report, s);
    s.facts.report_import->bytes_hashed = s.report.size();
  }
  // Digests/roles come from the genuine fresh physical proof, not a caller
  // report.
  for (auto& m : s.members) {
    bool found = false;
    for (const auto& actual : s.inspection->CapturedMembers())
      if (actual.path == m.logical) {
        m.digest = actual.sha256;
        m.kind = actual.kind;
        found = true;
        break;
      }
    if (!found)
      Fail(Status::kVerificationFailed, Stage::kReceipt,
           Code::kMemberInventory);
  }
  Json count(s, nullptr);
  Emit(count, s, report_present, report_hash);
  s.receipt.resize(count.Size());
  Json output(s, s.receipt.data());
  Emit(output, s, report_present, report_hash);
}
}  // namespace meshvale::interchange::surface_files_detail
