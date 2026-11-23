// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/pipeline/mtl_decoder.hpp"
#include "addc/pipeline/output.hpp"

#ifdef ADDC_HAS_MI400
#include "addc/pipeline/gpu_mi400_runtime_decoder.hpp"
#endif

#include <string>

#ifdef ADDC_HAS_MI400
namespace
{

std::optional<addc::mca::McaResult> fakeDecode(
    std::string_view, const addc::mca::BankCapture& bank)
{
    addc::mca::McaResult result;
    result.decoded.bank = "UMC";
    result.decoded.error_type = "Other";
    result.decoded.severity = "Corrected";
    result.decoded.instance = "inst";
    result.decoded.regs = bank.regs;
    result.event_report = addc::mca::to_json(result.decoded);
    return result;
}

std::vector<addc::pipeline::PipelineEvent> decodeMi400Bank(bool threshold)
{
    addc::pipeline::AmdGpuMi400RuntimeSection section;
    section.platform = "mi400";
    section.descriptor_flags.error_threshold_exceeded = threshold;

    addc::pipeline::GpuMi400RuntimeContext context_info;
    addc::pipeline::McaBankDump bank;
    bank.status = (uint64_t{1U} << 63U) | (uint64_t{0xFU} << 16U);
    bank.ipid = uint64_t{0x96U} << 32U;
    context_info.bank = bank;
    section.contexts.push_back(context_info);

    addc::pipeline::DecodeContext context;
    context.decode_mca = fakeDecode;
    return addc::pipeline::decode_gpu_mi400_runtime({}, section, context);
}

} // namespace
#endif

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

#if defined(ADDC_HAS_VENICE) || defined(ADDC_HAS_MI400)
    addc::pipeline::SectionDescriptor descriptor;
    addc::pipeline::DecodeContext context;
    addc::pipeline::AmdEpycMtlSection mtl;
#endif
#ifdef ADDC_HAS_VENICE
    mtl.platform = "venice";
    const auto venice = addc::pipeline::decode_mtl(descriptor, mtl, context);
    if (venice.size() != 1U ||
        venice[0].analysis["afid"] != nlohmann::json::array({25764}))
    {
        return 6;
    }
#endif
#ifdef ADDC_HAS_MI400
    mtl.platform = "mi400";
    const auto mi400 = addc::pipeline::decode_mtl(descriptor, mtl, context);
    if (mi400.size() != 1U || mi400[0].analysis["afid"] !=
                                  nlohmann::json::array({addc::kAfidSentinel}))
    {
        return 7;
    }

    const auto bad_page = decodeMi400Bank(true);
    if (bad_page.size() != 1U ||
        bad_page[0].analysis["afid"] != nlohmann::json::array({47873}) ||
        bad_page[0].event_report["error_type"] != "BadPageRetirement" ||
        bad_page[0].event_report["error_description"] !=
            "Bad page retirement threshold exceeded" ||
        bad_page[0].event_report["error_severity"] != "Fatal" ||
        !bad_page[0].event_report["error_location"]["instance"].is_null())
    {
        return 8;
    }

    const auto no_flag = decodeMi400Bank(false);
    if (no_flag.size() != 1U ||
        no_flag[0].analysis["afid"] !=
            nlohmann::json::array({addc::kAfidSentinel}) ||
        no_flag[0].event_report["error_type"] != "Other" ||
        no_flag[0].event_report["error_severity"] != "Corrected")
    {
        return 9;
    }
#endif
    return 0;
}
