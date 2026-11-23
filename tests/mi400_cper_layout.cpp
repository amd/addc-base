// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
//
// MI400 Series CPER layouts: record header, AMD Crashdump section (PLDM_BNDL,
// GPU Error Info, Boot Status and Break Event contexts), AMD GPU Non-Standard
// section, and AMD Scale Up section.
#include "../src/addc-cper-parser/sections/cper-section-amd-gpu-common.hpp"
#include "../src/addc-cper-parser/sections/cper-section-amd-gpu-runtime.hpp"
#include "addc/common.hpp"
#include "addc/detail/format.hpp"
#include "addc/cper/parser.hpp"
#include "addc/cper/section_dispatch.hpp"
#include "addc/cper/structural.hpp"
#include "addc/pipeline/section_dispatch.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition)
    {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

using Bytes = std::vector<uint8_t>;

void putU16(Bytes& b, std::size_t off, uint16_t v)
{
    b[off] = static_cast<uint8_t>(v);
    b[off + 1U] = static_cast<uint8_t>(v >> 8U);
}

void putU32(Bytes& b, std::size_t off, uint32_t v)
{
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        b[off + i] = static_cast<uint8_t>(v >> (8U * i));
    }
}

void putU64(Bytes& b, std::size_t off, uint64_t v)
{
    for (std::size_t i = 0U; i < 8U; ++i)
    {
        b[off + i] = static_cast<uint8_t>(v >> (8U * i));
    }
}

void putStr(Bytes& b, std::size_t off, std::string_view s)
{
    std::memcpy(b.data() + off, s.data(), s.size());
}

struct GuidBytes
{
    uint32_t d1;
    uint16_t d2;
    uint16_t d3;
    uint8_t d4[8];
};

void putGuid(Bytes& b, std::size_t off, const GuidBytes& g)
{
    putU32(b, off, g.d1);
    putU16(b, off + 4U, g.d2);
    putU16(b, off + 6U, g.d3);
    std::memcpy(b.data() + off + 8U, g.d4, 8U);
}

constexpr GuidBytes kCrashdumpGuid{
    0x32AC0C78U, 0x2623U, 0x48F6U,
    {0xB0, 0xD0, 0x73, 0x65, 0x72, 0x5F, 0xD6, 0xAE}};
constexpr GuidBytes kNonStandardGuid{
    0x32AC0C78U, 0x2623U, 0x48F6U,
    {0x81, 0xA2, 0xAC, 0x69, 0x17, 0x80, 0x55, 0x1D}};
constexpr GuidBytes kScaleUpGuid{
    0x5EEE25C9U, 0x83A0U, 0x234DU,
    {0x82, 0xBB, 0x7B, 0x7F, 0xC1, 0x40, 0xF2, 0x1F}};
constexpr GuidBytes kPlatformEventGuid{
    0x3F7C2A1EU, 0x8D5BU, 0x4C91U,
    {0x9A, 0x2F, 0x6E, 0x7B, 0x4D, 0x1C, 0x0A, 0x8F}};
constexpr GuidBytes kMpxTraceLogGuid{
    0x2327BCA9U, 0x9490U, 0x4696U,
    {0xAD, 0xDE, 0x04, 0xC9, 0x94, 0x5B, 0x80, 0x30}};

// PLDM_BNDL 0x0f1a0c04: patch 4, minor 12, major/year 26,
// byte 3 = 0x0f (buildType 7, productId 1).
constexpr uint32_t kPldm = 0x0f1a0c04U;

// MS Check: errorTypeValid | pccValid, Error Type 3 (External Error), PCC 1,
// Uncorrected 1 (not valid).
constexpr uint64_t kMsCheck = 0x3U | (3U << 16U) | (1U << 19U) | (1U << 20U);

// GPU Error Info structure.
void putErrorInfo(Bytes& b, std::size_t off, uint64_t valid,
                  std::string_view redfish)
{
    putGuid(b, off, kNonStandardGuid);
    putU64(b, off + 16U, valid);
    putU64(b, off + 24U, kMsCheck);
    putStr(b, off + 32U, "102-G43201-0C");
    putStr(b, off + 48U, redfish);
}

// Crashdump: 1 error info, 2 contexts (Boot Status, Break Event).
Bytes crashdumpBody(uint64_t break_event = UINT64_C(0x00000800200021bf))
{
    Bytes b(64U + 64U + 2U * 24U, 0U);
    putU64(b, 0U, (1U << 2U) | (2U << 8U) | (1U << 14U) | (1U << 15U));
    putStr(b, 8U, "75C1");
    putU32(b, 12U, kPldm);
    putErrorInfo(b, 64U, 0xA1U, "1730000000_1");
    std::size_t ctx = 128U;
    putU16(b, ctx, 9U);
    putU16(b, ctx + 2U, 8U);
    putU64(b, ctx + 16U, UINT64_C(0x00038120077036a1));
    ctx += 24U;
    putU16(b, ctx, 3U);
    putU16(b, ctx + 2U, 8U);
    putU64(b, ctx + 16U, break_event);
    return b;
}

// Scale Up: 1 error info, a telemetry buffer, a 16-byte event, an 8-byte
// event and a context of an unknown type.
Bytes scaleUpBody()
{
    Bytes b(72U + 64U + (16U + 48U) + (16U + 16U) + (16U + 16U) + 16U, 0U);
    putU64(b, 0U, (1U << 15U) | (1U << 16U) | (1U << 17U));
    putU32(b, 12U, kPldm);
    putU32(b, 64U, 1U);
    putU32(b, 68U, 4U);
    // Offset 48 is reserved and must be ignored.
    putGuid(b, 72U, kScaleUpGuid);
    putU64(b, 88U, 0x160U);
    putU32(b, 96U, 0x00000102U);
    putStr(b, 104U, "102-G43201-0C");
    putStr(b, 120U, "55");
    putStr(b, 128U, "17300000");
    std::size_t ctx = 136U;
    putU16(b, ctx, 2U);
    putU16(b, ctx + 2U, 40U);
    putU32(b, ctx + 4U, static_cast<uint32_t>(-1));
    putU64(b, ctx + 8U, UINT64_C(0x1122334455667788));
    // Category 4, 1 element, 2 instances, element and tag length 8.
    const uint8_t telemetry[40] = {
        0x04, 0x00, 0x01, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x04,
        0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(b.data() + ctx + 16U, telemetry, sizeof(telemetry));
    ctx += 16U + 48U;
    putU16(b, ctx, 1U);
    putU16(b, ctx + 2U, 16U);
    putU64(b, ctx + 8U, UINT64_C(0x1122334455667790));
    const uint8_t event[16] = {0x01, 0x1A, 0xC6, 0x15, 0xF0};
    std::memcpy(b.data() + ctx + 16U, event, sizeof(event));
    ctx += 16U + 16U;
    // NETPORT_SINGLE, netport 1, UP to DOWN.
    putU16(b, ctx, 1U);
    putU16(b, ctx + 2U, 8U);
    const uint8_t netport[8] = {0x01, 0x1A, 0xC6, 0x15, 0x01, 0x04};
    std::memcpy(b.data() + ctx + 16U, netport, sizeof(netport));
    ctx += 16U + 16U;
    putU16(b, ctx, 3U);
    return b;
}

using FruId = std::array<uint8_t, 16>;

Bytes record(const GuidBytes& notification, uint32_t flags,
             const std::vector<std::pair<GuidBytes, Bytes>>& sections,
             const FruId* fru_id = nullptr)
{
    constexpr std::size_t kHeader = 128U;
    constexpr std::size_t kDescriptor = 72U;
    std::size_t total = kHeader + sections.size() * kDescriptor;
    for (const auto& [guid, body] : sections)
    {
        total += body.size();
    }
    Bytes b(total, 0U);
    putStr(b, 0U, "CPER");
    putU16(b, 4U, 0x0100U);
    putU32(b, 6U, 0xFFFFFFFFU);
    putU16(b, 10U, static_cast<uint16_t>(sections.size()));
    putU32(b, 12U, 1U);
    putU32(b, 20U, static_cast<uint32_t>(total));
    putGuid(b, 80U, notification);
    putU32(b, 104U, flags);

    std::size_t body_off = kHeader + sections.size() * kDescriptor;
    for (std::size_t i = 0U; i < sections.size(); ++i)
    {
        const auto& [guid, body] = sections[i];
        const std::size_t d = kHeader + i * kDescriptor;
        putU32(b, d, static_cast<uint32_t>(body_off));
        putU32(b, d + 4U, static_cast<uint32_t>(body.size()));
        putU16(b, d + 8U, 0x3201U); // RAS gen 3, Instinct
        b[d + 10U] = 0x02U;         // FRU string valid
        putGuid(b, d + 16U, guid);
        if (fru_id != nullptr)
        {
            b[d + 10U] |= 0x01U; // FRU ID valid
            std::memcpy(b.data() + d + 32U, fru_id->data(), fru_id->size());
        }
        putU32(b, d + 48U, 1U);
        putStr(b, d + 52U, "EAM0");
        std::memcpy(b.data() + body_off, body.data(), body.size());
        body_off += body.size();
    }
    return b;
}

std::vector<addc::pipeline::PipelineEvent> decodeAll(const Bytes& bytes)
{
    std::vector<addc::pipeline::PipelineEvent> events;
    const auto ir = addc::cper::cper_to_ir(bytes);
    const auto typed = addc::cper::to_typed_record(ir, bytes);
    if (!typed)
    {
        return events;
    }
    for (std::size_t i = 0U; i < typed->sections.size(); ++i)
    {
        const auto& section = typed->sections[i];
        addc::pipeline::DecodeContext context;
        context.section_index = i;
        auto result = addc::pipeline::decode_base_section(
            addc::pipeline::parse_descriptor(section.canonical_descriptor),
            section.payload, context);
        for (auto& event : result.events)
        {
            events.push_back(std::move(event));
        }
    }
    return events;
}

const addc::pipeline::PipelineEvent* findEvent(
    const std::vector<addc::pipeline::PipelineEvent>& events,
    std::string_view event_class)
{
    for (const auto& event : events)
    {
        if (event.metadata.value("event_class", "") == event_class)
        {
            return &event;
        }
    }
    return nullptr;
}

void checkParserLayouts()
{
    using namespace addc::cper::sections;

    // PLDM_BNDL.
    const auto pldm = parse_amd_gpu_pldm_bundle(kPldm, AmdGpuLayout::Mi400);
    check(pldm.value("patch", 0U) == 4U && pldm.value("minor", 0U) == 12U &&
              pldm.value("majorYear", 0U) == 26U &&
              pldm.value("buildType", 0U) == 7U &&
              pldm.value("productId", 0U) == 1U,
          "PLDM_BNDL byte layout");

    // Bit 5 is MPN valid, bit 6 is reserved, bit 7 is Redfish.
    Bytes info(64U, 0U);
    putErrorInfo(info, 0U, 0xC0U, "14609");
    const auto ei = parse_amd_gpu_error_info(info, 0U, AmdGpuLayout::Mi400);
    check(!ei["validBits"].value("fruManufacturerPartNumberValid", true),
          "bit 6 must not set MPN valid");
    check(ei["validBits"].value("redfishEventLogIdValid", false),
          "bit 7 is Redfish valid");
    check(ei["validBits"].value("reservedBits", "") == "0x0000000000000040",
          "reserved bits are reported");
    check(ei["msCheckStructure"].value("errorType", 0U) == 3U,
          "MS Check error type");

    // The MI400 Series Non-Standard section uses the same Error Info
    // layout (Redfish ID at offset 48, 16 bytes). MI300 keeps its layout.
    Bytes runtime(64U + 64U, 0U);
    putU64(runtime, 0U, (1U << 2U) | (1U << 15U));
    putU32(runtime, 12U, kPldm);
    putErrorInfo(runtime, 64U, 0xA1U, "1730000000_1234");
    const addc::cper::SectionDescriptor desc{};
    const auto mi400 =
        parse_amd_gpu_runtime(runtime, desc, AmdGpuLayout::Mi400);
    const auto& mi400_ei = mi400["gpuErrorInfoStructures"][0];
    check(mi400_ei.value("redfishEventLogId", "") == "1730000000_1234",
          "Redfish ID at offset 48");
    check(mi400_ei["validBits"].value("redfishEventLogIdValid", false),
          "Redfish valid is bit 7");
    check(mi400["pldmBundle"].value("majorYear", 0U) == 26U,
          "PLDM_BNDL byte layout");
    const auto mi300 =
        parse_amd_gpu_runtime(runtime, desc, AmdGpuLayout::Mi300);
    check(mi300["pldmBundle"].value("year", 0U) == 26U,
          "MI300 runtime PLDM layout is unchanged");

    // The dispatcher selects the layout from the descriptor revision.
    addc::cper::SectionDescriptor runtime_desc{};
    runtime_desc.section_type = addc::cper::Guid{
        kNonStandardGuid.d1,
        kNonStandardGuid.d2,
        kNonStandardGuid.d3,
        {kNonStandardGuid.d4[0], kNonStandardGuid.d4[1],
         kNonStandardGuid.d4[2], kNonStandardGuid.d4[3],
         kNonStandardGuid.d4[4], kNonStandardGuid.d4[5],
         kNonStandardGuid.d4[6], kNonStandardGuid.d4[7]}};
    for (const auto& [revision, key] :
         {std::pair<uint8_t, const char*>{0x32U, "majorYear"},
          std::pair<uint8_t, const char*>{0x22U, "year"}})
    {
        runtime_desc.revision_major = revision;
        const auto kind =
            addc::cper::classify_section(addc::cper::RecordHeader{},
                                         runtime_desc)
                .kind;
        addc::cper::ParseContext context;
        const auto parsed = addc::cper::parse_section_json(
            kind, runtime, runtime_desc, context);
        check(parsed["pldmBundle"].contains(key),
              "dispatcher selects the GPU runtime layout from the revision");
    }
}

void checkRecordHeader()
{
    // Platform Event and MPx Trace Log notification types and the
    // INJECTION_CONTEXT flag (bit 3).
    const auto platform_event = addc::cper::cper_to_ir(
        record(kPlatformEventGuid, 0x9U, {{kCrashdumpGuid, crashdumpBody()}}));
    check(platform_event["header"]["notificationType"].value("type", "") ==
              "Platform Event",
          "Platform Event notification type");
    check(platform_event["header"]["flags"].value("name", "") ==
              "HW_ERROR_FLAGS_RECOVERED | HW_ERROR_FLAGS_INJECTION_CONTEXT",
          "flags are decoded bit by bit");

    const auto mtl = addc::cper::cper_to_ir(
        record(kMpxTraceLogGuid, 0x8U, {{kCrashdumpGuid, crashdumpBody()}}));
    check(mtl["header"]["notificationType"].value("type", "") ==
              "MPx Trace Log",
          "MPx Trace Log notification type");
    check(mtl["header"]["flags"].value("name", "") ==
              "HW_ERROR_FLAGS_INJECTION_CONTEXT",
          "INJECTION_CONTEXT flag");
}

void checkCrashdumpPipeline()
{
    const auto events = decodeAll(
        record(kPlatformEventGuid, 0U, {{kCrashdumpGuid, crashdumpBody()}}));

    const auto* boot = findEvent(events, "boot_status_eam");
    check(boot != nullptr, "Boot Status event");
    const auto* brk = findEvent(events, "break_event_eam");
    check(brk != nullptr, "Break Event context is not dropped");
    if (boot == nullptr || brk == nullptr)
    {
        return;
    }
    check(brk->metadata.value("context_type", "") == "MI400 Series Break Event",
          "context type name");
    check(brk->data_array.value("break_event_msg", "") ==
              "0x00000800200021bf",
          "BREAKEVENT[7:0]");
    check(brk->event_report.is_null() &&
              brk->analysis["afid"][0].get<int>() == addc::kAfidSentinel,
          "Base does not decode a Break Event");

    for (const uint8_t node : {0x00U, 0x04U, 0x07U, 0x08U, 0xFFU})
    {
        const uint64_t raw = UINT64_C(0x00000000010021bf) |
                             (static_cast<uint64_t>(node) << 40U);
        const auto located = decodeAll(record(
            kPlatformEventGuid, 0U, {{kCrashdumpGuid, crashdumpBody(raw)}}));
        const auto* event = findEvent(located, "break_event_eam");
        check(event != nullptr, "Break Event with NODEID is preserved");
        if (event != nullptr)
        {
            check(event->data_array.value("break_event_msg", "") ==
                      addc::format("0x{:016x}", raw),
                  "complete outbound payload including NODEID is preserved");
            check(event->event_report.is_null(),
                  "Base keeps the Break Event report undecoded");
        }
    }

    const auto& gpu = boot->platform["gpu"];
    check(gpu.value("fru_manufacturer_part_number", "") == "102-G43201-0C",
          "MPN with bit 5 set");
    check(gpu.value("redfish_event_log_id", "") == "1730000000_1",
          "raw Redfish ID is kept");
    check(gpu.value("redfish_event_log_timestamp", "") ==
              "2024-10-27T03:33:20Z",
          "Redfish ID timestamp");
    const auto& check_info = gpu["check_info"];
    check(check_info.value("error_type", "") == "External Error" &&
              check_info.value("pcc", false) &&
              check_info["uncorrected"].is_null(),
          "MS Check structure with valid bits");

    const auto& pldm = boot->firmware["pldm_bundle"];
    check(pldm.value("patch", 0U) == 4U && pldm.value("minor", 0U) == 12U &&
              pldm.value("major_year", 0U) == 26U &&
              pldm.value("build_type", 0U) == 7U &&
              pldm.value("product_id", 0U) == 1U,
          "PLDM_BNDL output");
}

void checkScaleUpPipeline()
{
    const auto bytes =
        record(kPlatformEventGuid, 0U, {{kScaleUpGuid, scaleUpBody()}});
    const auto ir = addc::cper::cper_to_ir(bytes);
    check(ir["sectionDescriptors"][0]["sectionType"].value("type", "") ==
              "AMD MI400 Series Scale Up Section",
          "Scale Up section is classified");
    const auto& contexts =
        ir["sections"][0]["AmdMi450ScaleUp"]["scaleUpContextStructures"];
    const auto& body = ir["sections"][0]["AmdMi450ScaleUp"];
    check(body.value("scaleUpErrorInfoCount", 0U) == 1U &&
              body.value("scaleUpContextInfoCount", 0U) == 4U &&
              body["validBits"].value("contextCountValid", false),
          "Scale Up INFO_COUNT and CTX_COUNT are parsed");
    const auto& info = body["scaleUpErrorInfoStructures"][0];
    check(info["validBits"].value("fruManufacturerPartNumberValid", false) &&
              info["validBits"].value("redfishEventLogIdValid", false) &&
              info["validBits"].value("eventDataFormatVersionValid", false) &&
              info["validBits"].value("reservedBits", "") ==
                  "0x0000000000000000" &&
              info.value("eventDataFormatVersion", "") == "0x00000102" &&
              info.value("redfishEventLogId", "") == "17300000",
          "Scale Up Error Info follows SCALEUP.2");
    check(contexts.size() == 4U &&
              contexts[0].value("relativeToErr", 0) == -1 &&
              contexts[0].value("timestamp", "") == "0x1122334455667788" &&
              contexts[0].value("registerArraySize", 0U) == 40U &&
              contexts[1].value("registerContextType", 0U) == 1U &&
              contexts[1].value("registerArraySize", 0U) == 16U,
          "Scale Up context headers are parsed; padding is skipped");
    const auto& header = contexts[0]["ualoeTelemetry"]["telemetryHeader"];
    check(header.value("categoryId", 0U) == 4U &&
              header.value("elementCount", 0U) == 1U &&
              header.value("instanceCount", 0U) == 2U &&
              header.value("elementLength", 0U) == 8U &&
              header.value("categoryVersion", 1U) == 0U &&
              header.value("tagLength", 0U) == 8U &&
              contexts[0]["ualoeTelemetry"].value("categoryTagData", "") ==
                  "AQAABAAAAAA=" &&
              contexts[0]["ualoeTelemetry"].value("categoryData", "") ==
                  "EQAAAAAAAAAiAAAAAAAAAA==",
          "UALoE telemetry buffer follows Table 7.22");
    check(!contexts[1].contains("ualoeEvent") && contexts[1].contains("data"),
          "UALoE event that is not 8 bytes is not decoded");
    const nlohmann::json netport = {
        {"eventId", 1},      {"eventSeq", 0x1A}, {"telemetrySeq", 0xC6},
        {"stationId", 0x15}, {"netportId", 1},   {"state", 0},
        {"prevState", 1},
    };
    check(contexts[2]["ualoeEvent"] == netport && contexts[2].contains("data"),
          "UALoE NETPORT_SINGLE event follows Table 7.19");

    const auto events = decodeAll(bytes);
    const auto* event = findEvent(events, "scaleup_event");
    const auto* telemetry = findEvent(events, "scaleup_telemetry");
    check(event != nullptr && telemetry != nullptr,
          "Scale Up contexts produce events");
    if (event == nullptr || telemetry == nullptr)
    {
        return;
    }
    // Base reports the structure with null values and the raw buffer.
    for (const auto* e : {event, telemetry})
    {
        const auto& er = e->event_report;
        check(er.contains("relative_to_err") &&
                  er["relative_to_err"].is_null() &&
                  er.contains("timestamp") && er["timestamp"].is_null() &&
                  er.contains("details") && er["details"].is_null(),
              "Base Scale Up report has null values");
        check(e->analysis["afid"][0].get<int>() == addc::kAfidSentinel,
              "Base Scale Up AFID is the sentinel");
    }
    check(event->data_array.value("raw_data", "") ==
              "ARrGFfAAAAAAAAAAAAAAAA==",
          "UALoE event raw buffer");
    check(telemetry->data_array.value("raw_data", "") ==
              "BAABAggAAAAAAAgAAAAAAAEAAAQAAAAAEQAAAAAAAAAiAAAAAAAAAA==",
          "UALoE telemetry raw buffer");
    check(event->platform["gpu"].value("redfish_event_log_id", "") ==
              "17300000",
          "Scale Up Error Info Redfish ID");
    check(!event->platform["gpu"].contains("redfish_event_log_timestamp"),
          "Scale Up Redfish ID is not decoded as a timestamp");
    std::size_t scaleup_events = 0U;
    for (const auto& e : events)
    {
        const auto cls = e.metadata.value("event_class", std::string{});
        scaleup_events += cls.rfind("scaleup_", 0U) == 0U ? 1U : 0U;
    }
    check(scaleup_events == 3U, "Unknown Scale Up context types are skipped");
    check(event->firmware.value("event_data_format_version", "") ==
              "0x00000102",
          "Scale Up Event Data Format Version");
    check(event->firmware["pldm_bundle"].value("build_type", 0U) == 7U,
          "PLDM_BNDL output");
}

FruId fruIdFromHex(std::string_view hex)
{
    FruId bytes{};
    for (std::size_t i = 0U; i < bytes.size(); ++i)
    {
        bytes[i] = static_cast<uint8_t>(
            std::stoul(std::string{hex.substr(2U * i, 2U)}, nullptr, 16));
    }
    return bytes;
}

struct CuidCase
{
    const char* name;
    FruId bytes;
    const char* fru_id;
    const char* encoding;
    const char* kind;
    const char* serial; // nullptr when the CUID has no fields
    unsigned unit_id;
    const char* device_id;
    const char* component_type_name;
};

void checkFruIdCuid()
{
    // A constructed CUID in the v84 integer form, as APML prints it:
    // 2004009d-7040-0002-1238-456789abcdef, with the serial
    // 0x0123456789abcdef.
    const FruId apml = fruIdFromHex("2004009d704000021238456789abcdef");
    FruId apml_lsb_first{};
    std::reverse_copy(apml.begin(), apml.end(), apml_lsb_first.begin());

    const CuidCase cases[] = {
        // Vectors from the current CUID specification.
        {"spec P-1", fruIdFromHex("d4abaad39b348c509800028dcc084200"),
         "d4abaad3-9b34-8c50-9800-028dcc084200", "spec", "primary",
         "0x06c5349bd3aaabd4", 0U, "0x73a3", "GPU"},
        {"spec P-QPX2", fruIdFromHex("d4abaad39b348c509824028dcc084200"),
         "d4abaad3-9b34-8c50-9824-028dcc084200", "spec", "primary",
         "0x06c5349bd3aaabd4", 9U, "0x73a3", "GPU"},
        {"spec D-2", fruIdFromHex("73488f9eea5286ce84012627fa41b068"),
         "73488f9e-ea52-86ce-8401-2627fa41b068", "spec", "derived", nullptr, 0U,
         nullptr, nullptr},
        {"spec TV-GPU", fruIdFromHex("df21fafe884b89e0bc2bf5ee8a5f7dde"),
         "df21fafe-884b-89e0-bc2b-f5ee8a5f7dde", "spec", "temporary", nullptr,
         0U, nullptr, nullptr},
        // A constructed CUID: amdgpu writes the reserved type 0xC.
        {"amdgpu", fruIdFromHex("efcdab896745823084000305d4084003"),
         "efcdab89-6745-8230-8400-0305d4084003", "spec", "primary",
         "0x0123456789abcdef", 0U, "0x75c1", "Reserved"},
        {"v84 little-endian", apml_lsb_first,
         "2004009d-7040-0002-1238-456789abcdef", "v84_compat", "primary",
         "0x0123456789abcdef", 0U, "0x75c1", "GPU"},
        {"v84 big-endian", apml, "2004009d-7040-0002-1238-456789abcdef",
         "v84_compat", "primary", "0x0123456789abcdef", 0U, "0x75c1", "GPU"},
    };

    for (const auto& c : cases)
    {
        for (const auto& [guid, body] :
             {std::pair{kCrashdumpGuid, crashdumpBody()},
              std::pair{kScaleUpGuid, scaleUpBody()}})
        {
            const auto events = decodeAll(
                record(kPlatformEventGuid, 0U, {{guid, body}}, &c.bytes));
            check(!events.empty(), c.name);
            for (const auto& event : events)
            {
                const auto& p = event.platform;
                const bool has_cuid = p.contains("cuid") &&
                                      p["cuid"].is_object();
                check(p.value("fru_id", "") == c.fru_id && has_cuid, c.name);
                if (!has_cuid)
                    continue;
                const auto& cuid = p["cuid"];
                check(cuid.value("encoding", "") == c.encoding &&
                          cuid.value("kind", "") == c.kind,
                      c.name);
                if (c.serial == nullptr)
                {
                    check(!cuid.contains("serial"), c.name);
                    continue;
                }
                check(cuid.value("serial", "") == c.serial &&
                          cuid.value("unit_id", 99U) == c.unit_id &&
                          cuid.value("vendor_id", "") == "0x1002" &&
                          cuid.value("device_id", "") == c.device_id &&
                          cuid.value("component_type_name", "") ==
                              c.component_type_name,
                      c.name);
                check(p["gpu"]["serial_number"].is_null(),
                      "a CUID is not an ASCII serial number");
            }
        }
    }

    // The FRU ID valid bit is set, but the bytes are "0".
    const FruId ascii_zero{0x30};
    for (const auto& event :
         decodeAll(record(kPlatformEventGuid, 0U,
                          {{kScaleUpGuid, scaleUpBody()}}, &ascii_zero)))
    {
        check(event.platform.value("fru_id", "") == "0" &&
                  event.platform.contains("cuid") &&
                  event.platform["cuid"].is_null(),
              "an ASCII FRU ID shows as text, not as a CUID");
    }

    for (const auto& event : decodeAll(record(
             kPlatformEventGuid, 0U, {{kCrashdumpGuid, crashdumpBody()}})))
    {
        check(event.platform.contains("fru_id") &&
                  event.platform["fru_id"].is_null() &&
                  event.platform.contains("cuid") &&
                  event.platform["cuid"].is_null(),
              "no valid FRU ID gives a null FRU ID and CUID");
    }
}

} // namespace

int main()
{
    checkParserLayouts();
    checkRecordHeader();
    checkCrashdumpPipeline();
    checkScaleUpPipeline();
    checkFruIdCuid();
    return failures == 0 ? 0 : 1;
}
