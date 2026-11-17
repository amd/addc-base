// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
// An amdsmi_* wrapper over the addc C API, compiled with AMD SMI's flags.
#include <addc/lib_c.h>

#include <cstdint>

extern "C" __attribute__((visibility("default"))) int amdsmi_addc_probe(void)
{
    if (addc_api_version() != ADDC_API_VERSION)
        return 1;
    addc_context_t* ctx = addc_context_create();
    if (ctx == nullptr)
        return 2;
    // A truncated record must be rejected by the parser, not crash.
    const std::uint8_t truncated[] = {'C', 'P', 'E', 'R'};
    std::size_t entry_count = 0;
    const addc_status_t status = addc_cper_get_summary(
        ctx, truncated, sizeof(truncated), nullptr, &entry_count);
    addc_context_destroy(ctx);
    if (status == ADDC_STATUS_SUCCESS || addc_status_string(status) == nullptr)
        return 3;
    return 0;
}
