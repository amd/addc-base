// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
// Compiled only against a staged install by CI; no source-tree include paths.
#include <addc/lib_c.h>
#include <string.h>

int main(void)
{
    if (addc_api_version() != ADDC_API_VERSION)
        return 1;
    if (strcmp(addc_schema_version(), ADDC_SCHEMA_VERSION) != 0)
        return 2;

    addc_context_t* context = addc_context_create();
    if (context == NULL)
        return 3;
    addc_context_destroy(context);
    return 0;
}
