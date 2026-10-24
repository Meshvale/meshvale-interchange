// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/off.h>

#include <type_traits>

static_assert(
    std::is_nothrow_copy_constructible_v<meshvale::interchange::OffDocument>);
static_assert(
    std::is_nothrow_copy_assignable_v<meshvale::interchange::OffDocument>);
static_assert(
    std::is_nothrow_destructible_v<meshvale::interchange::OffDocument>);
static_assert(
    std::is_nothrow_copy_constructible_v<meshvale::interchange::OffOutput>);
static_assert(
    std::is_nothrow_copy_assignable_v<meshvale::interchange::OffOutput>);
static_assert(std::is_nothrow_destructible_v<meshvale::interchange::OffOutput>);
