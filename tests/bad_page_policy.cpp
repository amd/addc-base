// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/mca/policy.hpp"

#include <cstdint>

namespace
{

constexpr uint64_t statusWithExtension(uint8_t extension)
{
    return uint64_t{extension} << 16U;
}

constexpr uint64_t ipid(uint16_t hardware_id, uint16_t mca_type)
{
    return (uint64_t{mca_type} << 48U) | (uint64_t{hardware_id} << 32U);
}

} // namespace

int main()
{
    using addc::mca::is_bad_page_retirement;
    using addc::mca::McaRegisters;

    const McaRegisters match0{statusWithExtension(0x0U), ipid(0x96U, 0U), 0U,
                              0U};
    const McaRegisters matchf{statusWithExtension(0x0fU), ipid(0x96U, 0U), 0U,
                              0U};

    if (!is_bad_page_retirement("mi300x", true, match0))
    {
        return 1;
    }
    if (!is_bad_page_retirement("mi350", true, matchf))
    {
        return 2;
    }
    if (is_bad_page_retirement("mi300x", false, match0))
    {
        return 3;
    }
    if (is_bad_page_retirement("mi450", true, match0))
    {
        return 4;
    }
    if (is_bad_page_retirement(
            "mi300x", true,
            McaRegisters{statusWithExtension(0x1U), ipid(0x96U, 0U), 0U, 0U}))
    {
        return 5;
    }
    if (is_bad_page_retirement(
            "mi300x", true,
            McaRegisters{statusWithExtension(0x0U), ipid(0x95U, 0U), 0U, 0U}))
    {
        return 6;
    }

    return 0;
}
