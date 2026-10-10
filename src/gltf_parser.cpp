// SPDX-License-Identifier: Apache-2.0
#include "gltf_parser.h"

#define JSONCONS_NO_DEPRECATED
#include <cstdint>
#include <jsoncons/json_parser.hpp>
#include <jsoncons/json_visitor.hpp>
#include <system_error>

namespace meshvale::interchange::gltf_detail {
namespace {
class Counter : public jsoncons::basic_default_json_visitor<char> {
 public:
  [[nodiscard]] std::size_t Events() const noexcept { return events_; }

 private:
  bool Increment() {
    ++events_;
    return true;
  }
  bool visit_begin_object(jsoncons::semantic_tag, const jsoncons::ser_context&,
                          std::error_code&) override {
    return Increment();
  }
  bool visit_end_object(const jsoncons::ser_context&,
                        std::error_code&) override {
    return Increment();
  }
  bool visit_begin_array(jsoncons::semantic_tag, const jsoncons::ser_context&,
                         std::error_code&) override {
    return Increment();
  }
  bool visit_end_array(const jsoncons::ser_context&,
                       std::error_code&) override {
    return Increment();
  }
  bool visit_key(const jsoncons::string_view&, const jsoncons::ser_context&,
                 std::error_code&) override {
    return Increment();
  }
  bool visit_string(const jsoncons::string_view&, jsoncons::semantic_tag,
                    const jsoncons::ser_context&, std::error_code&) override {
    return Increment();
  }
  bool visit_uint64(std::uint64_t, jsoncons::semantic_tag,
                    const jsoncons::ser_context&, std::error_code&) override {
    return Increment();
  }
  bool visit_int64(std::int64_t, jsoncons::semantic_tag,
                   const jsoncons::ser_context&, std::error_code&) override {
    return Increment();
  }
  bool visit_double(double, jsoncons::semantic_tag,
                    const jsoncons::ser_context&, std::error_code&) override {
    return Increment();
  }
  bool visit_bool(bool, jsoncons::semantic_tag, const jsoncons::ser_context&,
                  std::error_code&) override {
    return Increment();
  }
  bool visit_null(jsoncons::semantic_tag, const jsoncons::ser_context&,
                  std::error_code&) override {
    return Increment();
  }
  std::size_t events_ = 0;
};
}  // namespace
JsonSyntaxSummary InspectJsonSyntax(std::string_view input, int max_depth,
                                    const AllocationContext& allocation) {
  if (max_depth <= 0) throw AdmissionFailure(AdmissionCode::kInvalidObject);
  jsoncons::json_options options;
  options.lossless_number(true)
      .lossless_bignum(true)
      .allow_comments(false)
      .allow_trailing_comma(false)
      .max_nesting_depth(max_depth);
  Counter visitor;
  jsoncons::basic_json_parser<char, Allocator<char>> parser(
      options, Allocator<char>(allocation));
  std::error_code error;
  parser.update(input.data(), input.size());
  parser.parse_some(visitor, error);
  if (!error) parser.finish_parse(visitor, error);
  if (!error) parser.check_done(error);
  return {!error, visitor.Events()};
}
}  // namespace meshvale::interchange::gltf_detail
