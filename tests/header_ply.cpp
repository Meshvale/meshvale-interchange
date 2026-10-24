// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/ply.h>

#include <type_traits>

static_assert(
    std::is_nothrow_copy_constructible_v<meshvale::interchange::PlyDocument>);
static_assert(
    std::is_nothrow_copy_assignable_v<meshvale::interchange::PlyDocument>);
static_assert(
    std::is_nothrow_destructible_v<meshvale::interchange::PlyDocument>);
static_assert(
    std::is_nothrow_copy_constructible_v<meshvale::interchange::PlyOutput>);
static_assert(
    std::is_nothrow_copy_assignable_v<meshvale::interchange::PlyOutput>);
static_assert(std::is_nothrow_destructible_v<meshvale::interchange::PlyOutput>);
