// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/cper/section_dispatch.hpp"

int main()
{
    const addc::cper::RecordHeader header{};

    addc::cper::SectionDescriptor unknown{};
    if (addc::cper::classify_section(header, unknown).kind !=
        addc::cper::SectionKind::Opaque)
    {
        return 1;
    }

    addc::cper::SectionDescriptor pcie{};
    pcie.section_type = addc::cper::Guid{
        0xd995e954U,
        0xbbc1U,
        0x430fU,
        {0xadU, 0x91U, 0xb4U, 0x4dU, 0xcbU, 0x3cU, 0x6fU, 0x35U}};
    const auto pcie_result = addc::cper::classify_section(header, pcie);
    if (pcie_result.kind != addc::cper::SectionKind::Pcie ||
        pcie_result.json_key != "PCIe" ||
        pcie_result.type_name != "PCIe SECTION")
    {
        return 2;
    }

#if defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                  \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350) || defined(ADDC_HAS_MI400)
    // The descriptor revision selects the GPU runtime layout in every build
    // that contains the runtime parser.
    addc::cper::SectionDescriptor runtime{};
    runtime.section_type = addc::cper::Guid{
        0x32ac0c78U,
        0x2623U,
        0x48f6U,
        {0x81U, 0xa2U, 0xacU, 0x69U, 0x17U, 0x80U, 0x55U, 0x1dU}};
    runtime.revision_major = 0x32U;
    if (addc::cper::classify_section(header, runtime).kind !=
        addc::cper::SectionKind::AmdMi400Runtime)
    {
        return 5;
    }
    runtime.revision_major = 0x22U;
    if (addc::cper::classify_section(header, runtime).kind !=
        addc::cper::SectionKind::AmdMi300Runtime)
    {
        return 6;
    }
#endif

#ifdef ADDC_HAS_MI400
    // Early MI400 Series firmware uses the broad Venice-compatible 0x31
    // revision. The explicit classifier must select the MI400 Series
    // discriminator first.
    addc::cper::SectionDescriptor overlapping_mtl{};
    overlapping_mtl.section_type.d1 = 0x00000800U;
    overlapping_mtl.section_type.d2 = 0x1022U;
    overlapping_mtl.revision_major = 0x31U;
    overlapping_mtl.section_length = 8U * 1024U;
    overlapping_mtl.fru_text = "GPU_MID0";
    if (addc::cper::classify_section(header, overlapping_mtl).kind !=
        addc::cper::SectionKind::AmdMi400Mtl)
    {
        return 3;
    }
    overlapping_mtl.fru_text.clear();
    if (addc::cper::classify_section(header, overlapping_mtl).kind !=
        addc::cper::SectionKind::AmdEpycMtl)
    {
        return 4;
    }
#endif
    return 0;
}
