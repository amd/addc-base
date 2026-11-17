// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc-common/base64.hpp"
#include "addc/cper/parser.hpp"
#include "addc/cper/record.hpp"
#include "addc/pipeline/detect.hpp"

#include <cstdlib>
#include <new>
#include <span>
#include <utility>

namespace
{

bool fail_allocation = false;

} // namespace

void* operator new(std::size_t size)
{
    if (fail_allocation)
    {
        throw std::bad_alloc{};
    }
    // This replacement operator cannot allocate through operator new.
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    if (void* memory = std::malloc(size))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept
{
    std::free(memory); // NOLINT(cppcoreguidelines-no-malloc)
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory); // NOLINT(cppcoreguidelines-no-malloc)
}

int main()
{
    using addc::cper::CperRecord;
    using addc::cper::ParseError;

    // Allocation-capable internals must be allowed to propagate so the outer
    // C boundary can translate std::bad_alloc into OUT_OF_RESOURCES.
    static_assert(!noexcept(CperRecord::parse(
        std::span<const uint8_t>{}, static_cast<ParseError*>(nullptr))));
    static_assert(!noexcept(addc::cper::parse_native_json(
        std::span<const uint8_t>{})));
    static_assert(!noexcept(addc::pipeline::detect_platform(0x31U)));
    static_assert(!noexcept(addc::detail::base64_decode("AAAA")));

    fail_allocation = true;
    try
    {
        (void)addc::detail::base64_decode("AAAA");
    }
    catch (const std::bad_alloc&)
    {
        fail_allocation = false;
        return 0;
    }
    fail_allocation = false;
    return 1;
}
