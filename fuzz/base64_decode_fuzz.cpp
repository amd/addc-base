// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc-common/base64.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size)
{
    const std::string_view input{reinterpret_cast<const char*>(data), size};
    const auto decoded = addc::detail::base64_decode(input);

    // The decoder accepts canonical RFC 4648 only. Every accepted byte string
    // must therefore round-trip to exactly the same representation.
    if (decoded && addc::detail::base64_encode(*decoded) != input)
    {
        std::abort();
    }
    return 0;
}

#ifdef ADDC_FUZZ_SMOKE
int main()
{
    for (const std::string_view seed :
         {"", "AAAA", "Zg==", "Zm8=", "Zm9v", "A===", "AA=A"})
    {
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(seed.data()),
                               seed.size());
    }

    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/= !\n";
    std::array<uint8_t, 256> input{};
    uint32_t state = 0xaddc2026U;
    for (std::size_t iteration = 0; iteration < 10'000U; ++iteration)
    {
        state = state * 1664525U + 1013904223U;
        const std::size_t size = state % input.size();
        for (std::size_t index = 0; index < size; ++index)
        {
            state = state * 1664525U + 1013904223U;
            input[index] =
                static_cast<uint8_t>(alphabet[state % alphabet.size()]);
        }
        LLVMFuzzerTestOneInput(input.data(), size);
    }
    return 0;
}
#endif
