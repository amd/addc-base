// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/cper/parser.hpp"
#include "addc/cper/structural.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <variant>
#include <vector>

namespace
{

void putU16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
}

void putU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
{
    for (std::size_t index = 0; index < 4U; ++index)
    {
        bytes[offset + index] =
            static_cast<uint8_t>(value >> (8U * index));
    }
}

std::vector<uint8_t> opaqueRecord()
{
    constexpr std::size_t header_size = 128U;
    constexpr std::size_t descriptor_size = 72U;
    constexpr std::size_t body_offset = header_size + descriptor_size;
    std::vector<uint8_t> bytes(body_offset + 4U, 0U);
    putU32(bytes, 0U, 0x52455043U);
    putU32(bytes, 6U, 0xffffffffU);
    putU16(bytes, 10U, 1U);
    putU32(bytes, 20U, static_cast<uint32_t>(bytes.size()));
    putU32(bytes, header_size, static_cast<uint32_t>(body_offset));
    putU32(bytes, header_size + 4U, 4U);
    bytes[body_offset] = 0xdeU;
    bytes[body_offset + 1U] = 0xadU;
    bytes[body_offset + 2U] = 0xbeU;
    bytes[body_offset + 3U] = 0xefU;
    return bytes;
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

bool validatesTypedEvidence(std::span<const uint8_t> bytes)
{
    const auto canonical = addc::cper::normalize_parser_json(
        addc::cper::parse_native_json(bytes));
    if (canonical.is_null())
    {
        return true;
    }
    const auto typed = addc::cper::to_typed_record(canonical, bytes);
    if (!typed)
    {
        return false;
    }
    for (const auto& section : typed->sections)
    {
        const auto& opaque =
            std::get<addc::cper::OpaqueSection>(section.payload);
        const auto range = section.source_range;
        if (opaque.source_range != range || range.offset > bytes.size() ||
            range.length > bytes.size() - range.offset ||
            opaque.raw_data != std::vector<uint8_t>{
                                   bytes.begin() + static_cast<std::ptrdiff_t>(
                                                       range.offset),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(
                                                       range.offset +
                                                       range.length)})
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const auto bytes = opaqueRecord();
    const auto producer = addc::cper::parse_native_json(bytes);
    const auto canonical = addc::cper::normalize_parser_json(producer);
    if (canonical.is_null() || canonical != producer)
    {
        return 1;
    }

    auto with_unknowns = canonical;
    with_unknowns["futureRoot"] = {{"kept", true}};
    with_unknowns["sectionDescriptors"][0]["futureDescriptor"] = 17U;
    with_unknowns["sections"][0]["futureSection"] = "evidence";
    with_unknowns["recovery"] = {
        {"recovered", true},
        {"repairs",
         {{{"code", "test_repair"},
           {"field", "section_length"},
           {"section_index", 0U},
           {"original_value", 9U},
           {"substituted_value", 4U},
           {"reason", "fixture"}}}},
    };

    addc::Diagnostics diagnostics;
    diagnostics.warn("boundary_fixture", "diagnostic retained");
    addc::cper::TypedRecordFailure failure;
    const auto typed = addc::cper::to_typed_record(
        with_unknowns, bytes, diagnostics, &failure);
    if (!typed || typed->sections.size() != 1U ||
        typed->repairs.size() != 1U ||
        typed->repairs[0].code != "test_repair" ||
        typed->boundary_diagnostics.items().size() != 1U)
    {
        return 2;
    }

    const auto& section = typed->sections[0];
    const auto& opaque = std::get<addc::cper::OpaqueSection>(section.payload);
    if (section.kind != addc::cper::SectionKind::Opaque ||
        section.source_range != addc::cper::SourceRange{200U, 4U} ||
        opaque.raw_data != std::vector<uint8_t>({0xdeU, 0xadU, 0xbeU, 0xefU}) ||
        opaque.canonical_descriptor["futureDescriptor"] != 17U ||
        opaque.canonical_section["futureSection"] != "evidence")
    {
        return 3;
    }

    auto truncated = canonical;
    truncated["sectionDescriptors"][0]["sectionLength"] = 5U;
    if (addc::cper::to_typed_record(truncated, bytes, {}, &failure) ||
        failure.code != addc::cper::TypedRecordError::InvalidSourceRange ||
        failure.section_index != 0U)
    {
        return 4;
    }

    auto mismatched = canonical;
    mismatched["sections"].push_back({{"Unknown", {{"data", ""}}}});
    if (addc::cper::to_typed_record(mismatched, bytes, {}, &failure) ||
        failure.code != addc::cper::TypedRecordError::SectionCountMismatch)
    {
        return 5;
    }

    auto oversized = canonical;
    oversized["sectionDescriptors"][0]["sectionOffset"] =
        std::numeric_limits<uint64_t>::max();
    if (addc::cper::to_typed_record(oversized, bytes, {}, &failure) ||
        failure.code != addc::cper::TypedRecordError::InvalidSourceRange)
    {
        return 6;
    }

    std::size_t checked = 0U;
    for (int index = 1; index < argc; ++index)
    {
        // Meson supplies each corpus fixture path to the test process.
        // codeql[cpp/path-injection]
        const auto input = readFile(argv[index]);
        if (input.empty() || !validatesTypedEvidence(input))
        {
            return 7;
        }
        ++checked;
    }
    if (checked != 0U)
    {
        std::cout << "typed_evidence_checked=" << checked << '\n';
    }
    return 0;
}
