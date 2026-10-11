// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "fixture_support.h"

namespace gltf_topology_write_semantics {
namespace {
constexpr std::size_t kFixtureDepthLimit = 64;
constexpr std::size_t kUnicodeHexDigits = 4;
constexpr unsigned kAsciiLimit = 128;
constexpr int kHexRadix = 16;
constexpr std::string_view kBuffers = "buffers";
constexpr std::string_view kViews = "bufferViews";
constexpr std::string_view kAccessors = "accessors";
constexpr std::string_view kMeshes = "meshes";
constexpr std::string_view kPrimitives = "primitives";
constexpr std::string_view kIndices = "indices";
constexpr std::string_view kMode = "mode";

bool Whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
struct Key {
  std::string decoded;
  std::size_t begin;
};
struct Node {
  enum class Kind { kObject, kArray, kLeaf };
  Kind kind;
  std::size_t begin;
  std::size_t end;
  std::vector<Key> keys;
  std::vector<Node> children;
};

// Independent fixture-only scanner. It records byte spans and ASCII core keys;
// public reader admission remains the authority for complete JSON validity.
class Scanner {
 public:
  explicit Scanner(std::string_view text) : text_(text) {}
  Node Root() {
    auto result = Value(0);
    Skip();
    Require(cursor_ == text_.size(), "Unexpected trailing fixture token");
    return result;
  }

 private:
  void Skip() {
    while (cursor_ < text_.size() && Whitespace(text_[cursor_])) ++cursor_;
  }
  char Take() {
    Require(cursor_ < text_.size(), "Unexpected fixture JSON extent");
    return text_[cursor_++];
  }
  std::string String(bool decode_key) {
    Require(Take() == '"', "Expected fixture JSON string");
    std::string decoded;
    while (true) {
      auto c = Take();
      if (c == '"') break;
      if (c == '\\') {
        c = Take();
        if (c == 'u') {
          Require(text_.size() - cursor_ >= kUnicodeHexDigits,
                  "Truncated fixture Unicode escape");
          if (decode_key) {
            unsigned code = 0;
            const auto parsed = std::from_chars(
                text_.data() + cursor_,
                text_.data() + cursor_ + kUnicodeHexDigits, code, kHexRadix);
            Require(parsed.ec == std::errc{} && code < kAsciiLimit,
                    "Fixture keys must decode to ASCII");
            decoded.push_back(static_cast<char>(code));
          }
          cursor_ += kUnicodeHexDigits;
          continue;
        }
      }
      if (decode_key) decoded.push_back(c);
    }
    return decoded;
  }
  Node Value(std::size_t depth) {
    Require(depth <= kFixtureDepthLimit, "Fixture scanner depth exceeded");
    Skip();
    Require(cursor_ < text_.size(), "Missing fixture JSON value");
    Node node{Node::Kind::kLeaf, cursor_, cursor_, {}, {}};
    const auto start = text_[cursor_];
    if (start == '{' || start == '[') {
      node.kind = start == '{' ? Node::Kind::kObject : Node::Kind::kArray;
      const auto closing = start == '{' ? '}' : ']';
      ++cursor_;
      Skip();
      if (cursor_ < text_.size() && text_[cursor_] != closing) {
        while (true) {
          if (node.kind == Node::Kind::kObject) {
            Skip();
            const auto begin = cursor_;
            auto decoded = String(true);
            node.keys.push_back({std::move(decoded), begin});
            Skip();
            Require(Take() == ':', "Missing fixture member colon");
          }
          node.children.push_back(Value(depth + 1));
          Skip();
          Require(cursor_ < text_.size(), "Missing fixture container end");
          if (text_[cursor_] == closing) break;
          Require(Take() == ',', "Missing fixture member separator");
        }
      }
      Require(Take() == closing, "Wrong fixture container end");
    } else if (start == '"') {
      (void)String(false);
    } else {
      while (cursor_ < text_.size() && !Whitespace(text_[cursor_]) &&
             text_[cursor_] != ',' && text_[cursor_] != ']' &&
             text_[cursor_] != '}')
        ++cursor_;
      Require(cursor_ != node.begin, "Empty fixture scalar");
    }
    node.end = cursor_;
    return node;
  }
  std::string_view text_;
  std::size_t cursor_ = 0;
};

const Node* Member(const Node& node, std::string_view key) {
  Require(node.kind == Node::Kind::kObject, "Fixture path is not an object");
  for (std::size_t i = 0; i < node.keys.size(); ++i)
    if (node.keys[i].decoded == key) return &node.children[i];
  return nullptr;
}
const Node& Required(const Node& node, std::string_view key) {
  const auto* result = Member(node, key);
  Require(result != nullptr, "Required fixture path is absent");
  return *result;
}
struct Patch {
  std::size_t begin;
  std::size_t end;
  std::string_view original;
};
std::string_view Bytes(std::string_view text, std::size_t begin,
                       std::size_t end) {
  return text.substr(begin, end - begin);
}
void CheckGap(std::string_view original, std::string_view remainder) {
  std::size_t next = 0;
  for (const auto c : remainder) {
    Require(Whitespace(c) || c == ',',
            "Unexpected text outside an allowed insertion");
    if (next < original.size() && original[next] == c) ++next;
  }
  Require(next == original.size(),
          "Insertion lost original spacing or separator bytes");
}
void RemoveInsertedMembers(const Node& before, const Node& after,
                           std::span<const std::string_view> allowed,
                           std::string_view source, std::string_view output,
                           std::vector<Patch>& patches) {
  Require(
      before.kind == Node::Kind::kObject && after.kind == Node::Kind::kObject,
      "Expected preserved fixture object");
  std::vector<std::size_t> old_members;
  std::size_t old = 0;
  for (std::size_t i = 0; i < after.keys.size(); ++i) {
    if (old < before.keys.size() &&
        after.keys[i].decoded == before.keys[old].decoded) {
      old_members.push_back(i);
      ++old;
    } else {
      Require(std::find(allowed.begin(), allowed.end(),
                        after.keys[i].decoded) != allowed.end(),
              "Writer inserted/reordered an undeclared object member");
      Require(Member(before, after.keys[i].decoded) == nullptr,
              "Writer reordered or duplicated an existing object member");
    }
  }
  Require(old == before.keys.size(),
          "Writer removed an original object member");
  for (std::size_t gap = 0; gap <= old_members.size(); ++gap) {
    const auto first_new = gap == 0 ? 0 : old_members[gap - 1] + 1;
    const auto last_new =
        gap == old_members.size() ? after.keys.size() : old_members[gap];
    if (first_new == last_new) continue;
    const auto source_begin =
        gap == 0 ? before.begin + 1 : before.children[gap - 1].end;
    const auto source_end =
        gap == before.keys.size() ? before.end - 1 : before.keys[gap].begin;
    const auto output_begin =
        gap == 0 ? after.begin + 1 : after.children[old_members[gap - 1]].end;
    const auto output_end = gap == old_members.size()
                                ? after.end - 1
                                : after.keys[old_members[gap]].begin;
    std::string remainder;
    auto cursor = output_begin;
    for (auto i = first_new; i < last_new; ++i) {
      remainder.append(Bytes(output, cursor, after.keys[i].begin));
      cursor = after.children[i].end;
    }
    remainder.append(Bytes(output, cursor, output_end));
    const auto original = Bytes(source, source_begin, source_end);
    CheckGap(original, remainder);
    patches.push_back({output_begin, output_end, original});
  }
}
void RemoveAppendedRecord(const Node& before, const Node& after,
                          std::string_view source, std::string_view output,
                          std::vector<Patch>& patches) {
  Require(before.kind == Node::Kind::kArray &&
              after.kind == Node::Kind::kArray &&
              after.children.size() == before.children.size() + 1,
          "Changed output must append exactly one table record");
  const auto source_begin =
      before.children.empty() ? before.begin + 1 : before.children.back().end;
  const auto output_begin =
      before.children.empty() ? after.begin + 1
                              : after.children[before.children.size() - 1].end;
  const auto& appended = after.children.back();
  std::string remainder(Bytes(output, output_begin, appended.begin));
  remainder.append(Bytes(output, appended.end, after.end - 1));
  const auto original = Bytes(source, source_begin, before.end - 1);
  CheckGap(original, remainder);
  patches.push_back({output_begin, after.end - 1, original});
}
}  // namespace

void CheckLexicalPreservation(const Fixture& fixture,
                              std::span<const std::byte> output_bytes) {
  const auto source = std::string_view(fixture.json);
  const auto output = std::string_view(
      reinterpret_cast<const char*>(output_bytes.data()), output_bytes.size());
  const auto before = Scanner(source).Root();
  const auto after = Scanner(output).Root();
  std::vector<Patch> patches;
  constexpr std::array root_insertions{kBuffers, kViews};
  RemoveInsertedMembers(before, after, root_insertions, source, output,
                        patches);
  for (const auto key : {kBuffers, kViews, kAccessors}) {
    const auto* old_table = Member(before, key);
    const auto& new_table = Required(after, key);
    if (old_table)
      RemoveAppendedRecord(*old_table, new_table, source, output, patches);
    else
      Require(new_table.kind == Node::Kind::kArray &&
                  new_table.children.size() == 1,
              "An absent table must contain exactly one appended record");
  }
  const auto& old_primitive =
      Required(Required(before, kMeshes).children.at(0), kPrimitives)
          .children.at(fixture.primitive_ordinal);
  const auto& new_primitive =
      Required(Required(after, kMeshes).children.at(0), kPrimitives)
          .children.at(fixture.primitive_ordinal);
  constexpr std::array binding_insertions{kIndices, kMode};
  RemoveInsertedMembers(old_primitive, new_primitive, binding_insertions,
                        source, output, patches);
  for (const auto key : binding_insertions)
    if (const auto* original = Member(old_primitive, key)) {
      const auto& replacement = Required(new_primitive, key);
      Require(original->kind == Node::Kind::kLeaf &&
                  replacement.kind == Node::Kind::kLeaf,
              "Primitive binding must remain a scalar token");
      patches.push_back({replacement.begin, replacement.end,
                         Bytes(source, original->begin, original->end)});
    }
  std::sort(patches.begin(), patches.end(),
            [](const auto& first, const auto& second) {
              return first.begin < second.begin;
            });
  std::string projected;
  std::size_t cursor = 0;
  for (const auto& patch : patches) {
    Require(patch.begin >= cursor && patch.end >= patch.begin,
            "Independent allowed-edit projection overlaps");
    projected.append(Bytes(output, cursor, patch.begin));
    projected.append(patch.original);
    cursor = patch.end;
  }
  projected.append(output.substr(cursor));
  Require(
      projected == source,
      "Output changed bytes outside declared table append/binding insertions");
}
}  // namespace gltf_topology_write_semantics
