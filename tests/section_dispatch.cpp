// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/cper/structural.hpp"
#include "addc/pipeline/section_dispatch.hpp"

int main()
{
    const addc::pipeline::SectionDescriptor descriptor{};
    const addc::pipeline::DecodeContext context{};

    const addc::cper::SectionPayload cdd =
        addc::pipeline::AmdEpycCddSection{};
    if (!addc::pipeline::decode_base_section(descriptor, cdd, context).handled)
    {
        return 1;
    }

    const addc::cper::SectionPayload pmic =
        addc::pipeline::AmdEpycPmicSection{};
    if (!addc::pipeline::decode_base_section(descriptor, pmic, context).handled)
    {
        return 2;
    }
    return 0;
}
