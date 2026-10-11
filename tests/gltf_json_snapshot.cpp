// SPDX-License-Identifier: Apache-2.0
#include "gltf_json_snapshot.h"

#include <meshvale/geometry/execution.h>

#include <atomic>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace meshvale::interchange::gltf_detail;
using meshvale::geometry::ExecutionContext;
constexpr std::string_view kRich =
    R"({"asset":{"version":"2.0"},"extras":{"":"empty","numbers":[1,1.0,1e0,-0,1e-9999,1E+9999,18446744073709551616],"name":"\uD83D\uDE00\u0000","looks":"-12.3e4","flag":true,"nothing":null},"extensions":{"FUTURE":{"count":1.9}}})";
JsonCaptureLimits Limits() { return {8192, 256, 8192, 32}; }
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct Counter {
  const ExecutionContext& context;
  std::atomic<std::size_t> requests{0};
  std::atomic<std::size_t> live{0};
  std::atomic<std::size_t> peak{0};
  std::atomic<bool> ordering_error{false};
  std::size_t fault_at = 0;
  std::size_t cancel_at = 0;
  std::stop_source* stop = nullptr;
};
void* Allocate(std::size_t bytes, std::size_t alignment, void* state) {
  auto& counter = *static_cast<Counter*>(state);
  const auto request = ++counter.requests;
  if (counter.context.ActiveTrackedPayload() < bytes)
    counter.ordering_error = true;
  if (request == counter.fault_at) throw std::bad_alloc();
  auto* memory = ::operator new(bytes, std::align_val_t(alignment));
  const auto live = counter.live.fetch_add(bytes) + bytes;
  auto peak = counter.peak.load();
  while (peak < live && !counter.peak.compare_exchange_weak(peak, live)) {
  }
  if (counter.stop && request == counter.cancel_at)
    counter.stop->request_stop();
  return memory;
}
void Free(void* pointer, std::size_t bytes, std::size_t alignment,
          void* state) noexcept {
  auto& counter = *static_cast<Counter*>(state);
  if (counter.context.ActiveTrackedPayload() < bytes)
    counter.ordering_error = true;
  ::operator delete(pointer, std::align_val_t(alignment));
  counter.live -= bytes;
}
AllocationBackend Backend(Counter& counter) {
  return {Allocate, Free, &counter};
}
void Released(const Counter& counter) {
  Require(counter.live == 0 && counter.context.ActiveTrackedPayload() == 0,
          "partial storage/charge escaped");
  Require(!counter.ordering_error, "storage freed after charge");
}
std::size_t Child(const JsonSnapshot& snapshot, std::size_t parent,
                  std::string_view key) {
  for (auto node = snapshot.FirstChild(parent); node;
       node = snapshot.NextSibling(*node))
    if (snapshot.Key(*node) == key) return *node;
  throw std::runtime_error("missing child");
}
void OwnedRetention() {
  ExecutionContext context({2, 1, 1 << 20});
  Counter counter{context};
  std::optional<JsonSnapshot> survivor;
  {
    std::string input(kRich);
    const auto unchanged = input;
    auto result = CaptureJsonSnapshot(
        input, Limits(), AllocationContext(context, {}, Backend(counter)));
    Require(result.code == JsonCaptureCode::kAccepted && result.snapshot,
            "rich capture rejected");
    Require(input == unchanged && result.snapshot->Original() == input,
            "source changed");
    survivor = *result.snapshot;
    input.assign(input.size(), 'x');
  }
  Require(survivor->Original() == kRich && counter.live > 0,
          "owner/source lifetime lost");
  const auto extras = Child(*survivor, 0, "extras");
  Require(survivor->String(Child(*survivor, extras, "")) == "empty",
          "empty key lost");
  Require(survivor->String(Child(*survivor, extras, "name")) ==
              std::string("\xf0\x9f\x98\x80\0", 5),
          "Unicode scalar/NUL lost");
  const std::vector<std::string_view> spellings{
      "1", "1.0", "1e0", "-0", "1e-9999", "1E+9999", "18446744073709551616"};
  auto number = survivor->FirstChild(Child(*survivor, extras, "numbers"));
  for (const auto spelling : spellings) {
    Require(number && survivor->Number(*number) == spelling,
            "raw numeric lexeme changed");
    number = survivor->NextSibling(*number);
  }
  Require(!number, "numeric looking string became number");
  Require(survivor->Boolean(Child(*survivor, extras, "flag")), "boolean lost");
  const auto retained = context.ActiveTrackedPayload();
  std::stop_source stopped;
  stopped.request_stop();
  Require(CaptureJsonSnapshot(kRich, Limits(),
                              AllocationContext(context, stopped.get_token()))
                  .code == JsonCaptureCode::kCanceled,
          "pre-cancel missed");
  Require(context.ActiveTrackedPayload() == retained,
          "later cancellation changed retained owner");
  auto copy = *survivor;
  survivor.reset();
  Require(copy.Original() == kRich, "copied owner lost");
  auto moved = std::move(copy);
  try {
    (void)copy.NodeCount();
    throw std::runtime_error("moved owner accepted");
  } catch (const AdmissionFailure& failure) {
    Require(failure.Code() == AdmissionCode::kInvalidObject,
            "wrong moved code");
  }
  Require(moved.Original() == kRich, "move lost owner");
  try {
    (void)moved.Kind(moved.NodeCount());
    throw std::runtime_error("bad index accepted");
  } catch (const AdmissionFailure& failure) {
    Require(failure.Code() == AdmissionCode::kInvalidObject,
            "wrong range code");
  }
  try {
    (void)moved.Number(0);
    throw std::runtime_error("wrong kind accepted");
  } catch (const AdmissionFailure& failure) {
    Require(failure.Code() == AdmissionCode::kInvalidObject, "wrong kind code");
  }
}
void RetainedContext() {
  auto snapshot = [] {
    ExecutionContext context({2, 1, 1 << 20});
    auto result =
        CaptureJsonSnapshot(kRich, Limits(), AllocationContext(context, {}));
    Require(result.snapshot.has_value(), "context fixture failed");
    return *result.snapshot;
  }();
  Require(snapshot.Original() == kRich, "context destruction lost data");
}
void RejectCases() {
  ExecutionContext context({2, 1, 1 << 20});
  Counter counter{context};
  const AllocationContext allocation(context, {}, Backend(counter));
  const std::vector<std::string> malformed{
      "",           "{",     "[1,]", "/*x*/{}", "true false",
      "{\"a\":01}", "[NaN]", "[1e]", "[--1]"};
  const std::vector<std::string> unicode_invalid{
      "{\"x\":\"\\uD800\"}",
      "{\"x\":\"\\uDC00\"}",
      "{\"x\":\"\\uD800x\\uDC00\"}",
      std::string("[\"") + "\xed\xa0\x80" + "\"]",
      std::string("[\"") + "\xc0\x80" + "\"]",
      std::string("[\"") + "\xf4\x90\x80\x80" + "\"]"};
  for (const auto& input : malformed) {
    const auto result = CaptureJsonSnapshot(input, Limits(), allocation);
    Require(result.code == JsonCaptureCode::kMalformed && !result.snapshot,
            "malformed JSON accepted");
    Released(counter);
  }
  for (const auto& input : unicode_invalid) {
    const auto result = CaptureJsonSnapshot(input, Limits(), allocation);
    Require(result.code == JsonCaptureCode::kUnicodeScalar && !result.snapshot,
            "non-scalar Unicode accepted or mislabeled");
    Released(counter);
  }
  for (const auto input : {R"({"a":1,"\u0061":2})", R"({"":{},"":null})",
                           R"({"nested":{"a":{},"a":[]}})"}) {
    const auto result = CaptureJsonSnapshot(input, Limits(), allocation);
    Require(result.code == JsonCaptureCode::kDuplicateKey && !result.snapshot,
            "decoded duplicate accepted");
    Released(counter);
  }
  for (int field = 0; field < 4; ++field) {
    auto limits = Limits();
    if (field == 0) limits.source_bytes = 0;
    if (field == 1) limits.nodes = 0;
    if (field == 2) limits.decoded_bytes = 0;
    if (field == 3) limits.nesting_depth = 0;
    const auto before = counter.requests.load();
    Require(CaptureJsonSnapshot(kRich, limits, allocation).code ==
                JsonCaptureCode::kInvalidLimits,
            "zero limit accepted");
    Require(counter.requests == before, "invalid limits allocated");
  }
  for (int field = 0; field < 3; ++field) {
    auto limits = Limits();
    if (field == 0)
      limits.source_bytes = std::numeric_limits<std::size_t>::max();
    if (field == 1) limits.nodes = std::numeric_limits<std::size_t>::max();
    if (field == 2)
      limits.decoded_bytes = std::numeric_limits<std::size_t>::max();
    const auto before = counter.requests.load();
    Require(CaptureJsonSnapshot(kRich, limits, allocation).code ==
                JsonCaptureCode::kSizeLimit,
            "host extent limit misclassified");
    Require(counter.requests == before, "host extent rejection allocated");
  }
  for (int field = 0; field < 4; ++field) {
    auto limits = Limits();
    if (field == 0) limits.source_bytes = kRich.size() - 1;
    if (field == 1) limits.nodes = 1;
    if (field == 2) limits.decoded_bytes = 1;
    if (field == 3) limits.nesting_depth = 1;
    const auto result = CaptureJsonSnapshot(kRich, limits, allocation);
    Require(result.code == JsonCaptureCode::kSizeLimit && !result.snapshot,
            "limit misclassified");
    Released(counter);
  }
  auto result =
      CaptureJsonSnapshot(R"({"a":{"x":1},"b":{"x":2}})", Limits(), allocation);
  Require(result.snapshot.has_value(), "unrelated keys rejected");
  result.snapshot.reset();
  Released(counter);
}
std::size_t ConstructionBoundaries() {
  ExecutionContext context({2, 1, 1 << 20});
  Counter measured{context};
  {
    auto result = CaptureJsonSnapshot(
        kRich, Limits(), AllocationContext(context, {}, Backend(measured)));
    Require(result.snapshot.has_value(), "measure capture failed");
  }
  Released(measured);
  const auto requests = measured.requests.load();
  for (std::size_t boundary = 1; boundary <= requests; ++boundary) {
    Counter fault{context};
    fault.fault_at = boundary;
    try {
      (void)CaptureJsonSnapshot(kRich, Limits(),
                                AllocationContext(context, {}, Backend(fault)));
      throw std::runtime_error("fault hidden");
    } catch (const std::bad_alloc&) {
    }
    Released(fault);
    Counter cancel{context};
    std::stop_source stop;
    cancel.stop = &stop;
    cancel.cancel_at = boundary;
    const auto result = CaptureJsonSnapshot(
        kRich, Limits(),
        AllocationContext(context, stop.get_token(), Backend(cancel)));
    Require(result.code == JsonCaptureCode::kCanceled && !result.snapshot,
            "partial cancellation missed");
    Released(cancel);
  }
  const auto peak = measured.peak.load();
  ExecutionContext exact({2, 1, peak});
  Counter exact_counter{exact};
  {
    auto result = CaptureJsonSnapshot(
        kRich, Limits(), AllocationContext(exact, {}, Backend(exact_counter)));
    Require(result.snapshot.has_value(), "exact peak rejected");
  }
  Released(exact_counter);
  ExecutionContext short_context({2, 1, peak - 1});
  Counter short_counter{short_context};
  const auto rejected = CaptureJsonSnapshot(
      kRich, Limits(),
      AllocationContext(short_context, {}, Backend(short_counter)));
  Require(
      rejected.code == JsonCaptureCode::kBudgetExceeded && !rejected.snapshot,
      "peak minus one accepted");
  Released(short_counter);
  std::cout << "Partial capture allocations: " << requests
            << "; peak requested bytes: " << peak << '\n';
  return requests;
}
void MalformedConstructionBoundaries() {
  const std::string input = std::string(kRich) + " false";
  ExecutionContext context({2, 1, 1 << 20});
  Counter measured{context};
  Require(CaptureJsonSnapshot(input, Limits(),
                              AllocationContext(context, {}, Backend(measured)))
                  .code == JsonCaptureCode::kMalformed,
          "late malformed fixture accepted");
  Released(measured);
  const auto requests = measured.requests.load();
  for (std::size_t boundary = 1; boundary <= requests; ++boundary) {
    Counter fault{context};
    fault.fault_at = boundary;
    try {
      (void)CaptureJsonSnapshot(input, Limits(),
                                AllocationContext(context, {}, Backend(fault)));
      throw std::runtime_error("malformed partial fault hidden");
    } catch (const std::bad_alloc&) {
    }
    Released(fault);
    Counter cancel{context};
    std::stop_source stop;
    cancel.stop = &stop;
    cancel.cancel_at = boundary;
    const auto result = CaptureJsonSnapshot(
        input, Limits(),
        AllocationContext(context, stop.get_token(), Backend(cancel)));
    Require(result.code == JsonCaptureCode::kCanceled && !result.snapshot,
            "malformed partial cancellation missed");
    Released(cancel);
  }
  std::cout << "Late malformed partial allocations: " << requests << '\n';
}
void SharedPayloadContention() {
  ExecutionContext context({2, 1, 1 << 20});
  Counter counter{context};
  const auto copy = context;
  auto result = CaptureJsonSnapshot(
      kRich, Limits(), AllocationContext(copy, {}, Backend(counter)));
  Require(result.snapshot.has_value(), "contention capture failed");
  const auto retained = context.ActiveTrackedPayload();
  auto lease =
      meshvale::geometry::TryReservePayload(context, (1 << 20) - retained);
  Require(lease.status == meshvale::geometry::ReservationStatus::kAccepted,
          "shared charge failed");
  const auto rejected = CaptureJsonSnapshot(
      kRich, Limits(), AllocationContext(copy, {}, Backend(counter)));
  Require(
      rejected.code == JsonCaptureCode::kBudgetExceeded && !rejected.snapshot,
      "retained owner ignored shared cap");
  Require(result.snapshot->Original() == kRich &&
              context.ActiveTrackedPayload() == (1 << 20),
          "failed overlap changed existing owner");
  lease.lease.Reset();
  result.snapshot.reset();
  Released(counter);
}
void ConcurrentOwners() {
  ExecutionContext context({4, 1, 1 << 20});
  Counter counter{context};
  std::atomic<bool> failed{false};
  std::vector<std::thread> workers;
  for (int i = 0; i < 3; ++i)
    workers.emplace_back([&] {
      auto result = CaptureJsonSnapshot(
          kRich, Limits(), AllocationContext(context, {}, Backend(counter)));
      if (!result.snapshot || result.snapshot->Original() != kRich)
        failed = true;
    });
  for (auto& worker : workers) worker.join();
  Require(!failed, "concurrent capture differed");
  Released(counter);
}
void Hex(std::string_view text) {
  constexpr char digits[] = "0123456789abcdef";
  for (const auto c : text) {
    const auto byte = static_cast<unsigned char>(c);
    std::cout << digits[byte >> 4] << digits[byte & 15];
  }
}
void Dump(const char* path) {
  std::ifstream file(path, std::ios::binary);
  Require(file.good(), "fixture open failed");
  const std::string input{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
  ExecutionContext context({2, 1, 1 << 24});
  const auto result = CaptureJsonSnapshot(
      input, {1 << 20, 1 << 16, 1 << 20, 128}, AllocationContext(context, {}));
  std::cout << "code\t" << static_cast<int>(result.code) << '\n';
  if (!result.snapshot) return;
  const auto& snapshot = *result.snapshot;
  std::cout << "source\t";
  Hex(snapshot.Original());
  std::cout << '\n';
  for (std::size_t i = 0; i < snapshot.NodeCount(); ++i) {
    std::cout << static_cast<int>(snapshot.Kind(i)) << '\t';
    if (const auto parent = snapshot.Parent(i))
      std::cout << *parent;
    else
      std::cout << '-';
    std::cout << '\t';
    if (const auto child = snapshot.FirstChild(i))
      std::cout << *child;
    else
      std::cout << '-';
    std::cout << '\t';
    if (const auto next = snapshot.NextSibling(i))
      std::cout << *next;
    else
      std::cout << '-';
    std::cout << '\t';
    Hex(snapshot.Key(i));
    std::cout << '\t';
    if (snapshot.Kind(i) == JsonKind::kNumber) Hex(snapshot.Number(i));
    if (snapshot.Kind(i) == JsonKind::kString) Hex(snapshot.String(i));
    if (snapshot.Kind(i) == JsonKind::kBool)
      std::cout << (snapshot.Boolean(i) ? "1" : "0");
    std::cout << '\n';
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--dump") {
      Dump(argv[2]);
      return 0;
    }
    OwnedRetention();
    RetainedContext();
    RejectCases();
    (void)ConstructionBoundaries();
    MalformedConstructionBoundaries();
    SharedPayloadContention();
    ConcurrentOwners();
    std::cout << "Owned JSON retention, guards, limits, fault/cancel and "
                 "shared ledger passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
