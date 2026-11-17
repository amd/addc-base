// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/common.hpp"
#include "addc/decoder/dbglog/decoder.hpp"
#include "addc/decoder/wdt/decoder.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

int main()
{
    const addc::pipeline::DbgLogGroup generic_group{
        .block_id = 2U,
        .block_size = 0U,
        .instances = {},
    };
    const auto generic =
        addc::decoder::decode_base_dbglog("venice", {generic_group});
    if (generic.size() != 1U)
    {
        return 2;
    }
    if (!generic[0].key_name.empty() || !generic[0].data_array.is_null())
    {
        return 3;
    }
    if (generic[0].afid != std::vector<int>{25510})
    {
        return 4;
    }

    const std::array<uint8_t, 128> zero_bank{};
    const addc::pipeline::DbgLogGroup mca_group{
        .block_id = 0x20U,
        .block_size = 128U,
        .instances = {{
            .data = std::vector<uint8_t>{zero_bank.begin(), zero_bank.end()},
        }},
    };
    const auto mi450 =
        addc::decoder::decode_base_dbglog("mi450", {mca_group});
    if (mi450.size() != 1U || !mi450[0].mca_bank)
    {
        return 5;
    }
    if (!mi450[0].key_name.empty() || !mi450[0].data_array.is_null())
    {
        return 6;
    }
    if (mi450[0].afid != std::vector<int>{25540})
    {
        return 7;
    }

    const std::array<uint8_t, 128> zero_wdt{};
    const auto venice = addc::decoder::decode_base_wdt("venice", zero_wdt);
    if (venice.afid != std::vector<int>{25316})
    {
        return 9;
    }
    if (!venice.event_report["severity"].is_null() ||
        !venice.event_report["description"].is_null() ||
        !venice.event_report["wdt_entries"].is_null())
    {
        return 10;
    }
    if (!venice.data_array.is_array() || venice.data_array.size() != 8U)
    {
        return 11;
    }

    const auto turin = addc::decoder::decode_base_wdt("turin", zero_wdt);
    if (turin.afid != std::vector<int>{addc::kAfidSentinel})
    {
        return 12;
    }
    return 0;
}
