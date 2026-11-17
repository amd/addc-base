// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/mca/decoder.hpp"
#include "addc/product.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    std::vector<std::string> expected;
    std::istringstream input{ADDC_EXPECTED_PROJECTS};
    for (std::string name; std::getline(input, name, ',');)
    {
        expected.push_back(std::move(name));
    }
    std::sort(expected.begin(), expected.end());

    std::vector<std::string> definitions;
    for (const auto& product : addc::product::compiled_products())
    {
        if (!product.capabilities.mca ||
            addc::product::find_compiled_product(product.identity.id) !=
                &product ||
            addc::product::find_compiled_product(
                product.identity.canonical_name) != &product)
        {
            return 1;
        }
        definitions.emplace_back(product.identity.canonical_name);
    }
    std::sort(definitions.begin(), definitions.end());

    for (const auto& name : expected)
    {
        if (!addc::mca::try_decode_project_event(
                name, addc::mca::BankCapture{addc::mca::McaRegisters{}}))
        {
            return 1;
        }
    }
    return definitions == expected ? 0 : 1;
}
