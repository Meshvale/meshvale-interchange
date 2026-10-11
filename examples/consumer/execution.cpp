// SPDX-License-Identifier: Apache-2.0
#include <meshvale/geometry/execution.h>
#include <meshvale/interchange/obj.h>

#include <stdexcept>
#include <stop_token>
#include <utility>

int main() {
  using namespace meshvale::geometry;
  ExecutionContext context({2, 1, 4096});
  auto copy = context;
  if (!SharesExecutionBudget(context, copy))
    throw std::runtime_error("installed ledger comparison");
  auto allocation = TryReservePayload(context, 64);
  if (allocation.status != ReservationStatus::kAccepted ||
      copy.ActiveTrackedPayload() != 64)
    throw std::runtime_error("installed payload acquisition");
  std::stop_source stop;
  stop.request_stop();
  if (allocation.lease.TryResize(65, stop.get_token()) !=
          ReservationStatus::kCanceled ||
      allocation.lease.Bytes() != 64)
    throw std::runtime_error("installed canceled growth");
  allocation.lease.Reset();
  auto workers = TryReserveWorkers(context, 2);
  if (workers.Count() != 2 || copy.ActiveWorkers() != 2)
    throw std::runtime_error("installed shared worker slots");
  workers.Reset();
  if (context.ActiveTrackedPayload() != 0 || context.ActiveWorkers() != 0)
    throw std::runtime_error("installed cleanup");
  const auto imported =
      meshvale::interchange::read_obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  return imported.document ? 0 : 1;
}
