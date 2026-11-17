// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/cper/record.hpp"
#include "addc/options.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    const std::span<const uint8_t> bytes{data, size};
    addc::cper::ParseFailure tolerant_failure;
    addc::cper::ParseFailure strict_failure;

    const auto tolerant = addc::cper::CperRecord::parse_detailed(
        bytes, addc::DecodeOptions{.parse_mode = addc::ParseMode::Tolerant},
        &tolerant_failure);
    const auto strict = addc::cper::CperRecord::parse_detailed(
        bytes, addc::DecodeOptions{.parse_mode = addc::ParseMode::Strict},
        &strict_failure);

    // Tolerant mode may accept more records, but it must never reject a record
    // that passed the stricter parser using the same hard bounds.
    if (strict && !tolerant)
    {
        std::abort();
    }
    return 0;
}

#ifdef ADDC_FUZZ_SMOKE
int main()
{
    constexpr std::array<uint8_t, 4> signature{'C', 'P', 'E', 'R'};
    LLVMFuzzerTestOneInput(nullptr, 0U);
    LLVMFuzzerTestOneInput(signature.data(), signature.size());

    std::array<uint8_t, 2048> input{};
    uint32_t state = 0x43504552U;
    for (std::size_t iteration = 0; iteration < 10'000U; ++iteration)
    {
        state = state * 1664525U + 1013904223U;
        const std::size_t size = state % input.size();
        for (std::size_t index = 0; index < size; ++index)
        {
            state = state * 1664525U + 1013904223U;
            input[index] = static_cast<uint8_t>(state >> 24U);
        }
        LLVMFuzzerTestOneInput(input.data(), size);
    }
    return 0;
}
#endif
