// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
//
// Every build that parses the EPYC-format crashdump must also decode the
// records it owns: CPU projects own EPYC records, and the MI300 family owns
// Instinct (program 2, RAS gen 1) records that use this format. A parsed
// section that is not decoded produces no events and no error.
#include "addc/pipeline/section_dispatch.hpp"

namespace
{

[[nodiscard]] bool handled(uint8_t revision_major)
{
    const addc::pipeline::AmdEpycCrashdumpSection section{};
    const addc::cper::SectionPayload payload = section;
    addc::pipeline::SectionDescriptor descriptor{};
    descriptor.revision_major = revision_major;
    const addc::pipeline::DecodeContext context{};
    return addc::pipeline::decode_base_section(descriptor, payload, context)
        .handled;
}

} // namespace

int main()
{
#if defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                  \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350)
    if (!handled(0x12U))
    {
        return 1; // MI300A/C crashdump
    }
#endif
    // An EPYC record is decoded only when the build has a CPU project.
#if defined(ADDC_HAS_VENICE) || defined(ADDC_HAS_TURIN) ||                    \
    defined(ADDC_HAS_GENOA) || defined(ADDC_HAS_EMBEDDED_2005)
    constexpr bool kHasCpuProject = true;
#else
    constexpr bool kHasCpuProject = false;
#endif
    if (handled(0x31U) != kHasCpuProject)
    {
        return 2;
    }
    return 0;
}
