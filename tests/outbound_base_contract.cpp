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
    if (decoded.afid != std::vector<int>{25189})
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

    const auto afidOf =
        [](uint32_t code, uint32_t sub_type, uint32_t brk = 0U) {
            std::array<uint32_t, 8> r{};
            r[0] = code;
            r[1] = sub_type;
            r[3] = brk & 0xFFU;
            r[4] = (brk >> 8U) & 0xFFU;
            r[5] = (brk >> 16U) & 0xFFU;
            r[6] = (brk >> 24U) & 0xFFU;
            return addc::decoder::decode_outbound(r).afid;
        };
    const std::vector<int> none{addc::kAfidSentinel};
    if (afidOf(0xA1U, 0x01U) != std::vector<int>{25188} ||
        afidOf(0xA1U, 0x04U) != std::vector<int>{25189} ||
        afidOf(0xA1U, 0x0EU) != std::vector<int>{25199} ||
        afidOf(0xA1U, 0x12U) != std::vector<int>{25203} ||
        afidOf(0xA1U, 0x02U) != none || afidOf(0xA1U, 0x15U) != none)
    {
        return 13;
    }
    if (afidOf(0xBFU, 0x01U, 0x0U) != none ||
        afidOf(0xBFU, 0x01U, 0x1U) != std::vector<int>{25252} ||
        afidOf(0xBFU, 0x01U, 0x402U) != std::vector<int>{25253, 25262} ||
        afidOf(0xBFU, 0x01U, 0x800U) != none ||
        afidOf(0xBFU, 0x02U, 0x1U) != none)
    {
        return 14;
    }
    return 0;
}
