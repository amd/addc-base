// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/lib_c.h"

#include "addc/lib.hpp"

#include <cstring>
#include <type_traits>

static_assert(addc::kApiVersion == ADDC_API_VERSION);
static_assert(addc::kUnclassifiedAfid == ADDC_UNCLASSIFIED_AFID);
static_assert(addc::kSchemaVersion == ADDC_SCHEMA_VERSION);
// Changing what the JSON aliases name changes the C++ API and ABI; bump
// kApiVersion and update these assertions together.
static_assert(std::is_same_v<addc::json_value, nlohmann::json>);
static_assert(std::is_same_v<addc::ordered_json_value, nlohmann::ordered_json>);

int main()
{
    if (addc_api_version() != ADDC_API_VERSION)
    {
        return 1;
    }
    if (std::strcmp(addc_schema_version(), ADDC_SCHEMA_VERSION) != 0)
    {
        return 2;
    }
    return 0;
}
