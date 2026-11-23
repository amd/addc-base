// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "../src/addc-cper-parser/sections/cper-section-amd-epyc-cdd.hpp"
#include "../src/addc-cper-parser/sections/cper-section-amd-epyc-crashdump.hpp"
#include "../src/addc-cper-parser/sections/cper-section-amd-epyc-mtl.hpp"
#include "../src/addc-cper-parser/sections/cper-section-amd-gpu-runtime.hpp"
#include "../src/addc-cper-parser/sections/cper-section-amd-gpu-crashdump.hpp"
#include "../src/addc-cper-parser/sections/cper-section-pcie.hpp"
#include "addc/cper/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

void writeU16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
}

void writeU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
{
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        bytes[offset + i] = static_cast<uint8_t>(value >> (8U * i));
    }
}

void writeU64(std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value)
{
    for (std::size_t i = 0U; i < 8U; ++i)
    {
        bytes[offset + i] = static_cast<uint8_t>(value >> (8U * i));
    }
}

std::vector<uint8_t> overlappingRecord()
{
    constexpr std::size_t header_size = 128U;
    constexpr std::size_t descriptor_size = 72U;
    constexpr std::size_t first_body = header_size + 2U * descriptor_size;
    std::vector<uint8_t> bytes(first_body + 8U, 0U);

    writeU32(bytes, 0U, 0x52455043U);
    writeU32(bytes, 6U, 0xffffffffU);
    writeU16(bytes, 10U, 2U);
    writeU32(bytes, 20U, static_cast<uint32_t>(bytes.size()));

    for (std::size_t index = 0U; index < 2U; ++index)
    {
        const std::size_t descriptor = header_size + index * descriptor_size;
        writeU32(bytes, descriptor, static_cast<uint32_t>(first_body));
        writeU32(bytes, descriptor + 4U, 4U);
    }

    for (std::size_t i = 0U; i < 8U; ++i)
    {
        bytes[first_body + i] = static_cast<uint8_t>(i + 1U);
    }
    return bytes;
}

template <typename Parse>
bool parserRepairsTolerantAndRejectsStrict(const Parse& parse,
                                           const std::vector<uint8_t>& body)
{
    addc::cper::SectionDescriptor descriptor;

    std::vector<addc::cper::ParseRepair> repairs;
    addc::cper::ParseContext tolerant{
        .mode = addc::ParseMode::Tolerant,
        .repairs = &repairs,
    };
    if (parse(body, descriptor, tolerant).is_null() ||
        repairs.empty())
    {
        return false;
    }

    addc::cper::ParseContext strict{
        .mode = addc::ParseMode::Strict,
    };
    return parse(body, descriptor, strict).is_null();
}

template <typename Parse, typename Select>
bool nativeBinaryIsInternalOnly(const Parse& parse,
                                const std::vector<uint8_t>& body, Select select)
{
    addc::cper::SectionDescriptor descriptor;
    addc::cper::ParseContext native{
        .binary_representation = addc::cper::BinaryRepresentation::Native,
    };
    const auto native_ir = parse(body, descriptor, native);
    const auto public_ir = parse(body, descriptor);
    return !native_ir.is_null() && !public_ir.is_null() &&
           select(native_ir).is_binary() && select(public_ir).is_string();
}

} // namespace

int main()
{
    const auto bytes = overlappingRecord();
    [[maybe_unused]] const auto epyc = [](auto body, const auto& descriptor,
                                          auto&... context) {
        return addc::cper::sections::parse_amd_epyc_crashdump(
            body, descriptor, context...);
    };
    [[maybe_unused]] const auto mi300 = [](auto body, const auto& descriptor,
                                           auto&... context) {
        return addc::cper::sections::parse_amd_gpu_crashdump(
            body, descriptor, addc::cper::sections::AmdGpuLayout::Mi300,
            context...);
    };
    [[maybe_unused]] const auto mi400 = [](auto body, const auto& descriptor,
                                           auto&... context) {
        return addc::cper::sections::parse_amd_gpu_crashdump(
            body, descriptor, addc::cper::sections::AmdGpuLayout::Mi400,
            context...);
    };

    // Poisoned fixed-width firmware text must never make the public JSON
    // result invalid UTF-8. Preserve the printable prefix and truncate at the
    // first non-ASCII byte.
    std::vector<uint8_t> runtime_body(64U, 0U);
    runtime_body[16U] = 'z';
    runtime_body[17U] = 0xdaU;
    runtime_body[18U] = 0xadU;
    runtime_body[19U] = 0xbaU;
    addc::cper::SectionDescriptor runtime_descriptor;
#if defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                  \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350) || defined(ADDC_HAS_MI400)
    const auto runtime =
        addc::cper::sections::parse_amd_gpu_runtime(
            runtime_body, runtime_descriptor,
            addc::cper::sections::AmdGpuLayout::Mi300);
    if (runtime["fwId"] != "z")
    {
        return 15;
    }
    try
    {
        static_cast<void>(runtime.dump());
    }
    catch (const nlohmann::json::exception&)
    {
        return 16;
    }
#endif

    [[maybe_unused]] const auto cdd = [](auto body, const auto& descriptor,
                                         auto&... context) {
        return addc::cper::sections::parse_amd_epyc_cdd(body, descriptor,
                                                        context...);
    };
    [[maybe_unused]] const auto mtl = [](auto body, const auto& descriptor,
                                         auto&... context) {
        return addc::cper::sections::parse_amd_epyc_mtl(body, descriptor,
                                                        context...);
    };
    [[maybe_unused]] const auto pcie = [](auto body, const auto& descriptor,
                                          auto&... context) {
        return addc::cper::sections::parse_pcie(body, descriptor, context...);
    };

    const auto tolerant = addc::cper::cper_to_ir(bytes);
    if (tolerant.is_null())
    {
        return 1;
    }
    if (!tolerant.contains("recovery") ||
        tolerant["recovery"]["recovered"] != true ||
        tolerant["recovery"]["repairs"].size() != 1U)
    {
        return 2;
    }
    const auto& repair = tolerant["recovery"]["repairs"][0];
    if (repair["field"] != "section_offset" ||
        repair["original_value"] != 272U || repair["substituted_value"] != 276U)
    {
        return 3;
    }
    if (tolerant["sections"][0]["Unknown"]["data"] != "AQIDBA==" ||
        tolerant["sections"][1]["Unknown"]["data"] != "BQYHCA==")
    {
        return 4;
    }

    const addc::DecodeOptions strict{
        .parse_mode = addc::ParseMode::Strict,
    };
    if (!addc::cper::cper_to_ir(bytes, strict).is_null())
    {
        return 5;
    }

    // EPYC historically emitted an empty context object for a truncated
    // context header. Tolerant mode keeps that output and reports the repair;
    // strict mode rejects it.
    [[maybe_unused]] std::vector<uint8_t> epyc_body(132U, 0U);
    writeU64(epyc_body, 0U, 1ULL << 8U);
#if defined(ADDC_HAS_EMBEDDED_2005) || defined(ADDC_HAS_GENOA) ||                 \
    defined(ADDC_HAS_TURIN) || defined(ADDC_HAS_VENICE) ||                  \
    defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                 \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350)
    if (!parserRepairsTolerantAndRejectsStrict(
            epyc, epyc_body))
    {
        return 6;
    }
#endif

    // Both GPU crashdump layouts clamp an over-declared register array to the
    // bytes that remain inside the section.
    [[maybe_unused]] std::vector<uint8_t> gpu_body(148U, 0U);
    writeU64(gpu_body, 0U, 1ULL << 8U);
    writeU16(gpu_body, 128U + 2U, 32U);
#if defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                  \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350)
    if (!parserRepairsTolerantAndRejectsStrict(
            mi300, gpu_body))
    {
        return 7;
    }
#endif
#ifdef ADDC_HAS_MI400
    if (!parserRepairsTolerantAndRejectsStrict(
            mi400, gpu_body))
    {
        return 8;
    }
#endif

    // Analysis keeps large binary fields native. The standalone parser remains
    // a compatibility/debug adapter and therefore retains Base64 JSON.
    [[maybe_unused]] std::vector<uint8_t> complete_epyc_body(144U, 0U);
    writeU64(complete_epyc_body, 0U, 1ULL << 8U);
#if defined(ADDC_HAS_EMBEDDED_2005) || defined(ADDC_HAS_GENOA) ||                 \
    defined(ADDC_HAS_TURIN) || defined(ADDC_HAS_VENICE) ||                  \
    defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                 \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350)
    if (!nativeBinaryIsInternalOnly(
            epyc, complete_epyc_body,
            [](const nlohmann::json& value) -> const nlohmann::json& {
                return value["processorContextInfo"][0]["data"];
            }))
    {
        return 9;
    }
#endif

    [[maybe_unused]] std::vector<uint8_t> cdd_body(65U, 0U);
#if defined(ADDC_HAS_EMBEDDED_2005) || defined(ADDC_HAS_GENOA) ||                 \
    defined(ADDC_HAS_TURIN) || defined(ADDC_HAS_VENICE)
    if (!nativeBinaryIsInternalOnly(
            cdd, cdd_body,
            [](const nlohmann::json& value) -> const nlohmann::json& {
                return value["payload"];
            }))
    {
        return 10;
    }
#endif

    [[maybe_unused]] std::vector<uint8_t> mtl_body(16U, 0U);
#if defined(ADDC_HAS_EMBEDDED_2005) || defined(ADDC_HAS_GENOA) ||                 \
    defined(ADDC_HAS_TURIN) || defined(ADDC_HAS_VENICE) ||                  \
    defined(ADDC_HAS_MI400)
    if (!nativeBinaryIsInternalOnly(
            mtl, mtl_body,
            [](const nlohmann::json& value) -> const nlohmann::json& {
                return value["mtlRawData"];
            }))
    {
        return 11;
    }
#endif

    std::vector<uint8_t> pcie_body(302U, 0U);
    pcie_body[0] = 0x40U; // Capability Structure valid
    if (!nativeBinaryIsInternalOnly(
            pcie, pcie_body,
            [](const nlohmann::json& value) -> const nlohmann::json& {
                return value["capabilityStructure"]["data"];
            }))
    {
        return 12;
    }

#if defined(ADDC_HAS_MI300A) || defined(ADDC_HAS_MI300C) ||                  \
    defined(ADDC_HAS_MI300X) || defined(ADDC_HAS_MI325X) ||                 \
    defined(ADDC_HAS_MI350)
    if (!nativeBinaryIsInternalOnly(
            mi300, gpu_body,
            [](const nlohmann::json& value) -> const nlohmann::json& {
                return value["gpuContextStructures"][0]["registerArray"];
            }))
    {
        return 13;
    }
#endif

#ifdef ADDC_HAS_MI400
    if (!nativeBinaryIsInternalOnly(
            mi400, gpu_body,
            [](const nlohmann::json& value) -> const nlohmann::json& {
                return value["gpuContextStructures"][0]["data"];
            }))
    {
        return 14;
    }
#endif

    return 0;
}
