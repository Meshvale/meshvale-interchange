// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_topology_write.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include "gltf_allocation.h"
#include "gltf_json_snapshot.h"
#include "gltf_topology_write_internal.h"

namespace meshvale::interchange::gltf_topology_write_detail {
namespace {
using gltf_detail::JsonKind;
constexpr char kObjectOpen = '{';
constexpr char kObjectClose = '}';
constexpr char kArrayOpen = '[';
constexpr char kArrayClose = ']';
constexpr char kStringQuote = '"';
constexpr char kEscape = '\\';
constexpr char kMemberSeparator = ':';
constexpr char kEntrySeparator = ',';
constexpr std::string_view kJsonWhitespace = " \t\r\n";
constexpr std::string_view kTrue = "true";
constexpr std::string_view kFalse = "false";
constexpr std::string_view kNull = "null";
enum class FramePhase { kFirstChild, kDelimiter };
struct Frame {
  std::size_t node;
  std::optional<std::size_t> next_child;
  FramePhase phase = FramePhase::kFirstChild;
};
[[noreturn]] void InvalidLexical(std::size_t offset) {
  Reject(GltfTopologyWriteStatus::kSourceContractFailure,
         GltfTopologyWriteStage::kLexicalPlanning,
         GltfTopologyWriteDiagnosticCode::kLexicalSource, {}, {},
         GltfSourceSpan{offset, 0});
}
class Scanner {
 public:
  Scanner(const gltf_detail::JsonSnapshot& document,
          const gltf_detail::AllocationContext& allocation,
          const GltfTopologyWriteLimits& limits,
          const GltfTopologyWriteWorkPolicy& work)
      : document_(document),
        allocation_(allocation),
        limits_(limits),
        work_(work),
        text_(document.Original()),
        spans_(gltf_detail::Allocator<LexicalSpan>(allocation)),
        stack_(gltf_detail::Allocator<Frame>(allocation)) {}
  Vector<LexicalSpan> Run() {
    spans_.resize(document_.NodeCount());
    stack_.reserve(std::min<std::size_t>(document_.NodeCount(),
                                         limits_.max_source_json_depth));
    Space();
    Value(0, {});
    while (!stack_.empty()) {
      Space();
      auto& frame = stack_.back();
      const auto kind = document_.Kind(frame.node);
      const auto closing =
          kind == JsonKind::kObject ? kObjectClose : kArrayClose;
      if (Peek() == closing) {
        if (frame.next_child) InvalidLexical(position_);
        spans_[frame.node].closing = position_;
        Take();
        spans_[frame.node].end = position_;
        stack_.pop_back();
        continue;
      }
      if (frame.phase == FramePhase::kDelimiter) {
        if (Take() != kEntrySeparator) InvalidLexical(position_);
        Space();
      }
      if (!frame.next_child) InvalidLexical(position_);
      if (kind == JsonKind::kObject) {
        String();
        Space();
        if (Take() != kMemberSeparator) InvalidLexical(position_);
        Space();
      }
      const auto child = *frame.next_child;
      const auto parent = frame.node;
      frame.next_child = document_.NextSibling(child);
      frame.phase = FramePhase::kDelimiter;
      Value(child, parent);
    }
    Space();
    if (position_ != text_.size() || next_node_ != spans_.size())
      InvalidLexical(position_);
    return std::move(spans_);
  }

 private:
  char Peek() const {
    if (position_ == text_.size()) InvalidLexical(position_);
    return text_[position_];
  }
  char Take() {
    if (until_checkpoint_ == 0) {
      allocation_.Checkpoint();
      until_checkpoint_ =
          static_cast<std::size_t>(work_.cancellation_block_bytes);
    }
    --until_checkpoint_;
    const auto value = Peek();
    ++position_;
    return value;
  }
  void Space() {
    while (position_ < text_.size() &&
           kJsonWhitespace.find(text_[position_]) != std::string_view::npos)
      Take();
  }
  void String() {
    if (Take() != kStringQuote) InvalidLexical(position_);
    while (true) {
      const auto value = Take();
      if (value == kStringQuote) return;
      // The original was already validated. Consuming escaped characters one
      // at a time preserves bounded checkpoints even for long escaped strings.
      if (value == kEscape) Take();
    }
  }
  void Value(std::size_t node, std::optional<std::size_t> parent) {
    if (node != next_node_ || node >= spans_.size() ||
        document_.Parent(node) != parent)
      InvalidLexical(position_);
    ++next_node_;
    auto& span = spans_[node];
    span.begin = position_;
    const auto kind = document_.Kind(node);
    if (kind == JsonKind::kObject || kind == JsonKind::kArray) {
      const auto opening = kind == JsonKind::kObject ? kObjectOpen : kArrayOpen;
      if (Take() != opening) InvalidLexical(position_);
      if (stack_.size() >= limits_.max_source_json_depth)
        Reject(GltfTopologyWriteStatus::kLimitExceeded,
               GltfTopologyWriteStage::kLexicalPlanning,
               GltfTopologyWriteDiagnosticCode::kCountLimit);
      stack_.push_back({node, document_.FirstChild(node)});
      return;
    }
    if (kind == JsonKind::kString) {
      String();
    } else {
      while (position_ < text_.size()) {
        const auto c = text_[position_];
        if (c == kEntrySeparator || c == kArrayClose || c == kObjectClose ||
            kJsonWhitespace.find(c) != std::string_view::npos)
          break;
        Take();
      }
      const auto token = text_.substr(span.begin, position_ - span.begin);
      if (token.empty()) InvalidLexical(span.begin);
      if (kind == JsonKind::kNumber) {
        const auto retained = document_.Number(node);
        if (retained.data() != token.data() || retained.size() != token.size())
          InvalidLexical(span.begin);
      } else if (kind == JsonKind::kBool) {
        if (token != (document_.Boolean(node) ? kTrue : kFalse))
          InvalidLexical(span.begin);
      } else if (kind != JsonKind::kNull || token != kNull) {
        InvalidLexical(span.begin);
      }
    }
    span.end = position_;
  }
  const gltf_detail::JsonSnapshot& document_;
  const gltf_detail::AllocationContext& allocation_;
  const GltfTopologyWriteLimits& limits_;
  const GltfTopologyWriteWorkPolicy& work_;
  std::string_view text_;
  Vector<LexicalSpan> spans_;
  Vector<Frame> stack_;
  std::size_t position_ = 0;
  std::size_t next_node_ = 0;
  std::size_t until_checkpoint_ = 0;
};
}  // namespace
Vector<LexicalSpan> BuildLexicalIndex(
    const gltf_detail::JsonSnapshot& document,
    const gltf_detail::AllocationContext& allocation,
    const GltfTopologyWriteLimits& limits,
    const GltfTopologyWriteWorkPolicy& work) {
  return Scanner(document, allocation, limits, work).Run();
}
}  // namespace meshvale::interchange::gltf_topology_write_detail
