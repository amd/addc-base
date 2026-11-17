// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/pipeline/output.hpp"

#include <string>

int main()
{
    addc::pipeline::PipelineOutput output;
    auto json = addc::pipeline::to_json(output);
    if (json.contains(addc::kDiagnosticsKey))
    {
        return 1;
    }

    output.diagnostics.debug("parser_recovery",
                             "Parser repaired malformed input",
                             {{"field", "section_offset"}});
    json = addc::pipeline::to_json(output);
    if (!json.contains(addc::kDiagnosticsKey))
    {
        return 2;
    }
    if (json[addc::kDiagnosticsKey].size() != 1U)
    {
        return 3;
    }
    if (json[addc::kDiagnosticsKey][0]["level"] != "debug")
    {
        return 4;
    }
    if (json[addc::kDiagnosticsKey][0]["code"] != "parser_recovery")
    {
        return 5;
    }
    return 0;
}
