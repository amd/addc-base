// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc-common/base64.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

bool decodesTo(std::string_view encoded, std::string_view expected)
{
    const auto decoded = addc::detail::base64_decode(encoded);
    return decoded &&
           std::string_view{reinterpret_cast<const char*>(decoded->data()),
                            decoded->size()} == expected;
}

} // namespace

int main()
{
    if (!decodesTo("", ""))
    {
        return 1;
    }
    if (!decodesTo("Zg==", "f"))
    {
        return 2;
    }
    if (!decodesTo("Zm8=", "fo"))
    {
        return 3;
    }
    if (!decodesTo("Zm9v", "foo"))
    {
        return 4;
    }

    const std::array<uint8_t, 5> bytes{0U, 1U, 2U, 0xfeU, 0xffU};
    const auto encoded = addc::detail::base64_encode(bytes);
    const auto decoded = addc::detail::base64_decode(encoded);
    if (!decoded ||
        *decoded != std::vector<uint8_t>{bytes.begin(), bytes.end()})
    {
        return 5;
    }

    for (const auto invalid : {"A", "AAA", "A===", "=AAA", "AA=A",
                               "AAAA=", "AA A", "AA\nA", "Zh==", "Zm9="})
    {
        if (addc::detail::base64_decode(invalid))
        {
            return 6;
        }
    }

    const std::string long_input(4096U, 'A');
    const auto long_output = addc::detail::base64_decode(long_input);
    if (!long_output || long_output->size() != 3072U)
    {
        return 7;
    }

    return 0;
}
