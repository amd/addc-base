// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/cper/parser.hpp"
#include "addc/lib.hpp"
#include "addc/pipeline/pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
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
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<uint8_t>(value >> 24U);
}

std::vector<uint8_t> minimalCper()
{
    constexpr std::size_t header_size = 128U;
    constexpr std::size_t descriptor_size = 72U;
    constexpr std::size_t body_offset = header_size + descriptor_size;

    std::vector<uint8_t> bytes(body_offset + 1U);
    putU32(bytes, 0U, 0x52455043U); // "CPER"
    putU32(bytes, 6U, 0xFFFFFFFFU);
    putU16(bytes, 10U, 1U);
    putU32(bytes, 20U, static_cast<uint32_t>(bytes.size()));

    putU32(bytes, header_size, static_cast<uint32_t>(body_offset));
    putU32(bytes, header_size + 4U, 1U);
    return bytes;
}

[[maybe_unused]] std::vector<uint8_t> sentinelAfidCper()
{
    constexpr std::size_t header_size = 128U;
    constexpr std::size_t descriptor_size = 72U;
    constexpr std::size_t body_offset = header_size + descriptor_size;
    constexpr std::size_t body_size = 16U;

    std::vector<uint8_t> bytes(body_offset + body_size);
    putU32(bytes, 0U, 0x52455043U); // "CPER"
    putU32(bytes, 6U, 0xFFFFFFFFU);
    putU16(bytes, 10U, 1U);
    putU32(bytes, 20U, static_cast<uint32_t>(bytes.size()));

    putU32(bytes, header_size, static_cast<uint32_t>(body_offset));
    putU32(bytes, header_size + 4U, static_cast<uint32_t>(body_size));
    putU16(bytes, header_size + 8U, 0x3100U);  // Venice MTL revision
    bytes[header_size + 10U] = 0x2U;           // FRU text is valid
    putU16(bytes, header_size + 20U, 0x1022U); // MTL GUID discriminator

    constexpr char fru[] = "SENTINEL_FRU";
    std::copy_n(&fru[0], sizeof(fru) - 1U, bytes.begin() + header_size + 52U);
    return bytes;
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

std::optional<nlohmann::ordered_json> legacyIrAnalysis(
    std::span<const uint8_t> bytes, std::string_view filename,
    std::string_view tool_version)
{
    const auto ir = addc::cper::cper_to_ir(bytes);
    if (ir.is_null())
    {
        return std::nullopt;
    }

    const auto output = addc::pipeline::run_pipeline(
        ir, filename, tool_version);
    return output
               ? std::optional<nlohmann::ordered_json>{addc::pipeline::to_json(
                     *output)}
               : std::nullopt;
}

[[maybe_unused]] int checkSentinelAfid()
{
    const auto bytes = sentinelAfidCper();
    std::string error;

    const auto analysis =
        addc::analyze_cper_json(bytes, "sentinel.cper", &error);
    if (!analysis)
    {
        return 1;
    }
    const auto legacy = legacyIrAnalysis(
        bytes, "sentinel.cper", (*analysis)["tool_version"].get<std::string>());
    if (!legacy || *legacy != *analysis)
    {
        return 2;
    }

    const auto entries =
        addc::get_error_summary_entries(bytes, "sentinel.cper", &error);
    if (!entries || entries->size() != 1U ||
        (*entries)[0].afid != addc::kUnclassifiedAfid ||
        (*entries)[0].fru_text != "SENTINEL_FRU")
    {
        return 3;
    }

    const auto summary_json =
        addc::get_error_summary_json(bytes, "sentinel.cper", &error);
    if (!summary_json || summary_json->size() != 1U ||
        (*summary_json)[0]["afid"] != addc::kUnclassifiedAfid ||
        (*summary_json)[0]["fru_text"] != "SENTINEL_FRU")
    {
        return 4;
    }

    const auto summary_string =
        addc::get_error_summary(bytes, "sentinel.cper", &error);
    if (!summary_string ||
        nlohmann::ordered_json::parse(*summary_string) != *summary_json)
    {
        return 5;
    }

    return 0;
}

// Insert a CR before every LF, as a text-mode file transfer does.
std::vector<uint8_t> asTextTransfer(const std::vector<uint8_t>& bytes)
{
    std::vector<uint8_t> out;
    out.reserve(bytes.size() * 2U);
    for (const uint8_t byte : bytes)
    {
        if (byte == 0x0AU)
        {
            out.push_back(0x0DU);
        }
        out.push_back(byte);
    }
    return out;
}

bool hasMismatchRepair(const nlohmann::json& document)
{
    if (!document.is_object() || !document.contains("recovery") ||
        !document["recovery"].contains("repairs"))
    {
        return false;
    }
    for (const auto& repair : document["recovery"]["repairs"])
    {
        if (repair["code"] == "record_length_mismatch")
        {
            return true;
        }
    }
    return false;
}

// A buffer larger than its declared record length must be flagged unless the
// extra bytes are zero padding.
int checkRecordLengthMismatch()
{
    const addc::DecodeOptions strict{.parse_mode = addc::ParseMode::Strict};

    const auto exact = minimalCper();
    const auto exact_ir = addc::cper::cper_to_ir(exact);
    if (exact_ir.is_null() || hasMismatchRepair(exact_ir))
    {
        return 1;
    }

    auto padded = exact;
    padded.insert(padded.end(), 3U, 0x00U);
    const auto padded_ir = addc::cper::cper_to_ir(padded);
    if (padded_ir.is_null() || hasMismatchRepair(padded_ir) ||
        addc::cper::cper_to_ir(padded, strict).is_null())
    {
        return 2;
    }

    auto trailing = exact;
    trailing.insert(trailing.end(), {0x11U, 0x22U});
    const auto trailing_ir = addc::cper::cper_to_ir(trailing);
    if (trailing_ir.is_null() || !hasMismatchRepair(trailing_ir) ||
        trailing_ir["recovery"]["repairs"][0]["original_value"] !=
            exact.size() ||
        !addc::cper::cper_to_ir(trailing, strict).is_null())
    {
        return 3;
    }

    // A text-mode transfer: the last body byte is an LF that gains a CR.
    auto lf = exact;
    lf.back() = 0x0AU;
    const auto crlf = asTextTransfer(lf);
    if (crlf.size() != lf.size() + 1U)
    {
        return 4;
    }
    const auto crlf_ir = addc::cper::cper_to_ir(crlf);
    if (crlf_ir.is_null() || !hasMismatchRepair(crlf_ir) ||
        crlf_ir["recovery"]["repairs"][0]["reason"].get<std::string>().find(
            "CRLF") == std::string::npos ||
        !addc::cper::cper_to_ir(crlf, strict).is_null())
    {
        return 5;
    }
    return 0;
}

#ifdef ADDC_HAS_VENICE
// The decoded events of a mismatched record must not look trustworthy.
int checkMismatchMarksEvents()
{
    std::string error;
    const auto clean_bytes = sentinelAfidCper();
    const auto clean =
        addc::analyze_cper_json(clean_bytes, "clean.cper", &error);
    if (!clean || (*clean)["events"].empty())
    {
        return 1;
    }
    const auto& clean_validation = (*clean)["events"][0]["validation"];
    if (clean_validation.contains("corrupted") &&
        clean_validation["corrupted"] == true)
    {
        return 1;
    }

    auto damaged_bytes = clean_bytes;
    damaged_bytes.insert(damaged_bytes.end(), {0x11U, 0x22U});
    const auto damaged =
        addc::analyze_cper_json(damaged_bytes, "damaged.cper", &error);
    if (!damaged || (*damaged)["events"].empty() ||
        (*damaged)["events"][0]["validation"]["corrupted"] != true ||
        !hasMismatchRepair(*damaged))
    {
        return 2;
    }

    bool warned = false;
    for (const auto& diagnostic : (*damaged)["diagnostics"])
    {
        warned = warned || (diagnostic["level"] == "warning" &&
                            diagnostic["code"] == "record_length_mismatch");
    }
    if (!warned)
    {
        return 3;
    }

    const addc::DecodeOptions strict{.parse_mode = addc::ParseMode::Strict};
    if (addc::analyze_cper_json(damaged_bytes, "damaged.cper", strict, &error))
    {
        return 4;
    }
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    const int mismatch_result = checkRecordLengthMismatch();
    if (mismatch_result != 0)
    {
        return 40 + mismatch_result;
    }
#ifdef ADDC_HAS_VENICE
    const int marked_result = checkMismatchMarksEvents();
    if (marked_result != 0)
    {
        return 50 + marked_result;
    }
#endif

#ifdef ADDC_HAS_VENICE
    const int sentinel_result = checkSentinelAfid();
    if (sentinel_result != 0)
    {
        return 20 + sentinel_result;
    }
#endif

    // Meson supplies this optional corpus fixture path to the test process.
    // codeql[cpp/path-injection]
    const auto bytes = argc > 1 ? readFile(argv[1]) : minimalCper();
    if (bytes.empty())
    {
        return 10;
    }
    std::string error;

    const auto full_json =
        addc::analyze_cper_json(bytes, "minimal.cper", &error);
    if (!full_json || !full_json->contains("events"))
    {
        return 1;
    }

    // The supported facade now keeps CperRecord typed through analysis. Keep
    // the legacy JSON-IR adapter behavior-identical while consumers migrate.
    const auto legacy = legacyIrAnalysis(
        bytes, "minimal.cper", (*full_json)["tool_version"].get<std::string>());
    if (!legacy || *legacy != *full_json)
    {
        return 7;
    }

    const auto full_string = addc::analyze_cper(bytes, "minimal.cper", &error);
    if (!full_string ||
        nlohmann::ordered_json::parse(*full_string) != *full_json)
    {
        return 2;
    }

    const auto entries =
        addc::get_error_summary_entries(bytes, "minimal.cper", &error);
    if (!entries || (argc == 1 && !entries->empty()) ||
        (argc > 1 && entries->empty()))
    {
        return 3;
    }

    const auto summary_json =
        addc::get_error_summary_json(bytes, "minimal.cper", &error);
    if (!summary_json || !summary_json->is_array() ||
        summary_json->size() != entries->size())
    {
        return 4;
    }

    for (std::size_t i = 0; i < entries->size(); ++i)
    {
        if ((*summary_json)[i]["fru_id"] != (*entries)[i].fru_id ||
            (*summary_json)[i]["fru_text"] != (*entries)[i].fru_text ||
            (*summary_json)[i]["additional_context"] !=
                (*entries)[i].additional_context ||
            (*summary_json)[i]["afid"] != (*entries)[i].afid)
        {
            return 6;
        }
    }

    const auto summary_string =
        addc::get_error_summary(bytes, "minimal.cper", &error);
    if (!summary_string ||
        nlohmann::ordered_json::parse(*summary_string) != *summary_json)
    {
        return 5;
    }

    return 0;
}
