// SPDX-License-Identifier: Apache-2.0
#include <meshvale/interchange/stl.h>

#include <type_traits>

static_assert(
    std::is_nothrow_copy_constructible_v<meshvale::interchange::StlDocument>);
static_assert(
    std::is_nothrow_copy_assignable_v<meshvale::interchange::StlDocument>);
static_assert(
    std::is_nothrow_destructible_v<meshvale::interchange::StlDocument>);
static_assert(
    std::is_nothrow_copy_constructible_v<meshvale::interchange::StlOutput>);
static_assert(
    std::is_nothrow_copy_assignable_v<meshvale::interchange::StlOutput>);
static_assert(std::is_nothrow_destructible_v<meshvale::interchange::StlOutput>);

int main() { return 0; }
