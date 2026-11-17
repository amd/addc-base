// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
// Isolated Wave 11 probe; this is not part of the production parser path.
#include "addc/cper/parser.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

int main()
{
    try
    {
        const auto producer = nlohmann::json::parse(std::cin);
        std::cout << addc::cper::normalize_parser_json(producer).dump();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
