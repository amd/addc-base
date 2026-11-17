// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/common.hpp"
#include "addc/decoder/outbound/msg.hpp"

#include <array>

int main()
{
    std::array<uint32_t, 8> raw{};
    raw[0] = 0xA1U;
    raw[1] = 0xA4U; // socket-down=1, format=1, error type=4
    raw[5] = 0x5AU;

    const auto decoded = addc::decoder::decode_outbound(raw);
    if (!decoded.code_type_name.empty())
    {
        return 1;
    }
    if (!decoded.sub_type_name.empty())
    {
        return 2;
    }
    if (!decoded.severity.empty())
    {
        return 3;
    }
    if (!decoded.description.empty())
    {
        return 4;
    }
    if (decoded.corrupted)
    {
        return 5;
    }
    if (!decoded.runtime_error_bits.empty())
    {
        return 6;
    }
    if (decoded.afid != std::vector<int>{25192})
    {
        return 7;
    }
    if (decoded.format_version != 1U)
    {
        return 8;
    }
    if (decoded.socket_down_config != 1U)
    {
        return 9;
    }
    if (decoded.smn_node_id != 0x5AU)
    {
        return 10;
    }
    if (!decoded.data_array.is_null())
    {
        return 11;
    }

    raw[0] = 0U;
    const auto unknown = addc::decoder::decode_outbound(raw);
    if (unknown.afid != std::vector<int>{addc::kAfidSentinel})
    {
        return 12;
    }
    return 0;
}
