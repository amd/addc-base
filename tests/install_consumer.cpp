// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
// Compiled only against a staged install by CI; no source-tree include paths.
#include <addc/lib.hpp>

#include <array>
#include <optional>
#include <string>

int main()
{
    const std::array<std::uint8_t, 0> empty{};
    std::string error;
    const std::optional<addc::ordered_json_value> result =
        addc::analyze_cper_json(empty, {}, &error);
    return !result && !error.empty() ? 0 : 1;
}
