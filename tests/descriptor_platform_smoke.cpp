// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT

#include "../src/addc-cper-pipeline/sections/gpu_decode_helpers.hpp"
#include "addc/cper/structural.hpp"
#include "addc/pipeline/section_data.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string_view>
#include <variant>

namespace
{

using addc::pipeline::AmdGpuCrashdumpSection;
using addc::pipeline::AmdGpuMi400CrashdumpSection;
using addc::pipeline::AmdGpuMi400RuntimeSection;
using addc::pipeline::AmdGpuRuntimeSection;

nlohmann::json descriptor(std::string_view guid, uint8_t revision_major)
{
    return {
        {"sectionType", {{"data", guid}}},
        {"revision", {{"major", revision_major}, {"minor", 0U}}},
    };
}

nlohmann::json gpuBody(std::string_view body_name,
                       std::string_view pcie_device_id)
{
    return {
        {body_name,
         {
             {"validBits", {{"pcieDevidValid", true}}},
             {"pcieDevId", pcie_device_id},
         }},
    };
}

template <typename Section>
bool hasDescriptorPlatform(const addc::cper::SectionPayload& parsed,
                           std::string_view expected_platform,
                           std::string_view expected_name,
                           std::string_view expected_device_id)
{
    const auto* section = std::get_if<Section>(&parsed);
    if (!section || section->platform != expected_platform ||
        section->platform_name != expected_name ||
        section->pcie_dev_id != expected_device_id)
    {
        return false;
    }

    const auto platform = addc::pipeline::gpu::make_gpu_platform(*section);
    return platform["gpu"]["pcie_device"]["id"].template get<std::string>() ==
               expected_device_id &&
           platform["gpu"]["pcie_device"]["name"].is_null();
}

} // namespace

int main()
{
    // These device IDs formerly mapped to the opposite generation. They must
    // now be treated as payload only; descriptor revision owns dispatch and
    // platform metadata.
    const auto mi300_runtime = addc::cper::convert_section(
        addc::cper::SectionKind::AmdMi300Runtime,
        descriptor(AmdGpuRuntimeSection::kGuid, 0x22U),
        gpuBody("AmdMi300Runtime", "75C1"), {}, {});
    if (!hasDescriptorPlatform<AmdGpuRuntimeSection>(
            mi300_runtime, "mi300x", "AMD Instinct MI300X", "75C1"))
    {
        return 1;
    }

    const auto mi400_runtime = addc::cper::convert_section(
        addc::cper::SectionKind::AmdMi400Runtime,
        descriptor(AmdGpuRuntimeSection::kGuid, 0x32U),
        gpuBody("AmdMi300Runtime", "74A5"), {}, {});
    if (!hasDescriptorPlatform<AmdGpuMi400RuntimeSection>(
            mi400_runtime, "mi400", "AMD Instinct MI400 Series", "74A5"))
    {
        return 2;
    }

    const auto mi300_crashdump = addc::cper::convert_section(
        addc::cper::SectionKind::AmdMi300Crashdump,
        descriptor(AmdGpuCrashdumpSection::kGuid, 0x22U),
        gpuBody("AmdMi300Crashdump", "75D1"), {}, {});
    if (!hasDescriptorPlatform<AmdGpuCrashdumpSection>(
            mi300_crashdump, "mi300x", "AMD Instinct MI300X", "75D1"))
    {
        return 3;
    }

    const auto mi400_crashdump = addc::cper::convert_section(
        addc::cper::SectionKind::AmdMi400Crashdump,
        descriptor(AmdGpuCrashdumpSection::kGuid, 0x32U),
        gpuBody("AmdMi450Crashdump", "74A6"), {}, {});
    if (!hasDescriptorPlatform<AmdGpuMi400CrashdumpSection>(
            mi400_crashdump, "mi400", "AMD Instinct MI400 Series", "74A6"))
    {
        return 4;
    }

    AmdGpuRuntimeSection timestamp_section;
    addc::pipeline::GpuErrorInfoStructure error_info;
    error_info.redfish_event_log_id_valid = true;
    error_info.redfish_event_log_id = "0_fixture";
    const auto timestamp_platform = addc::pipeline::gpu::make_gpu_platform(
        timestamp_section, true, &error_info);
    if (timestamp_platform["gpu"]["redfish_event_log_timestamp"] !=
        "1970-01-01T00:00:00Z")
    {
        return 5;
    }

    return 0;
}
