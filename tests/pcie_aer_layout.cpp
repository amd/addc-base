// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "../src/addc-cper-parser/sections/cper-section-pcie.hpp"
#include "addc/cper/record.hpp"
#include "addc/decoder/pcie/decoder.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

namespace
{

// Venice: program_gen 1, ras_gen 3. The BMC layout version is the descriptor
// revision minor, not the PCIe spec minor stored in the section body.
uint32_t uncorrStatusFor(uint8_t bmc_minor)
{
    std::vector<uint8_t> body(302U, 0U);
    body[0] = 0x80U; // AER Info valid
    body[12] = 0x0DU; // PCIe spec minor in body, must not affect the layout
    // Uncorrectable status = 0x10 at +4 (UEFI) or +8 (deviated) within AER.
    body[112U + 4U] = 0x10U;
    addc::cper::SectionDescriptor desc{};
    desc.revision_major = 0x31U;
    desc.revision_minor = bmc_minor;
    const auto j = addc::cper::sections::parse_pcie(body, desc);
    return j["aerInfo"]["uncorrectable_error_status"]["value"]
        .get<uint32_t>();
}

nlohmann::json aerBody(uint32_t root_sts)
{
    return {
        {"sectionValidBits", "0x80"},
        {"aerInfo",
         {{"root_error_status", {{"value", root_sts}}},
          {"error_source_id",
           {{"correctable_source_id", 0x0100U},
            {"fatal_non_fatal_source_id", 0x0200U}}}}},
    };
}

} // namespace

int main()
{
    // 0xD and earlier: deviated, status shifted by the 4-byte AMD prefix.
    if (uncorrStatusFor(0x0DU) != 0U)
    {
        return 1;
    }
    // 0xE and later: UEFI layout.
    if (uncorrStatusFor(0x0EU) != 0x10U)
    {
        return 2;
    }

    // Root Error Status bits[8:7] subclass, bits[31:27] MSI/MSI-X vector.
    const auto r = addc::decoder::decode_pcie(
        aerBody((0x5U << 27U) | (0x2U << 7U) | 0x4U), 0x31U);
    const auto& aer = r.event_report["aer_info"];
    if (aer.value("err_cor_subclass", "") != "0x2" ||
        aer.value("msi_msix_vector", "") != "0x5")
    {
        return 3;
    }

    // Only ERR_FATAL/NONFATAL received: fatal half of the source ID.
    if (aer["error_source"]["bus"] != "0x02" ||
        aer.contains("correctable_error_source"))
    {
        return 4;
    }

    // Only ERR_COR received: correctable half of the source ID.
    const auto c = addc::decoder::decode_pcie(aerBody(0x1U), 0x31U);
    if (c.event_report["aer_info"]["error_source"]["bus"] != "0x01")
    {
        return 5;
    }

    // Both received: fatal is primary, correctable reported separately.
    const auto b = addc::decoder::decode_pcie(aerBody(0x5U), 0x31U);
    const auto& both = b.event_report["aer_info"];
    if (both["error_source"]["bus"] != "0x02" ||
        both["correctable_error_source"]["bus"] != "0x01")
    {
        return 6;
    }
    return 0;
}
