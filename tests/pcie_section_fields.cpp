// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "../src/addc-cper-parser/sections/cper-section-pcie.hpp"
#include "addc/cper/record.hpp"
#include "addc/decoder/pcie/decoder.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{

constexpr std::size_t kDeviceId = 24U;
constexpr std::size_t kCapStruct = 52U;
constexpr std::size_t kAer = 112U;

void putU16(std::vector<uint8_t>& b, std::size_t off, uint16_t v)
{
    b[off] = static_cast<uint8_t>(v);
    b[off + 1U] = static_cast<uint8_t>(v >> 8U);
}

void putU32(std::vector<uint8_t>& b, std::size_t off, uint32_t v)
{
    putU16(b, off, static_cast<uint16_t>(v));
    putU16(b, off + 2U, static_cast<uint16_t>(v >> 16U));
}

nlohmann::json parse(const std::vector<uint8_t>& body)
{
    return addc::cper::sections::parse_pcie(body,
                                            addc::cper::SectionDescriptor{});
}

} // namespace

int main()
{
    // Every validation bit clear: only the validation bits are reported.
    {
        const std::vector<uint8_t> body(362U, 0xFFU);
        std::vector<uint8_t> zero_valid = body;
        for (std::size_t i = 0U; i < 8U; ++i)
        {
            zero_valid[i] = 0U;
        }
        const auto j = parse(zero_valid);
        if (j.size() != 1U || !j.contains("sectionValidBits"))
        {
            return 1;
        }
    }

    std::vector<uint8_t> body(362U, 0U);
    // Port type, version, command status, device ID, capability structure,
    // AER info.
    putU32(body, 0U, 0xCFU);

    // Device ID bytes 13-14: bits[2:0] reserved, bits[15:3] slot number.
    putU16(body, kDeviceId + 13U, static_cast<uint16_t>((0x5U << 3U) | 0x7U));
    body[kDeviceId + 7U] = 0x3U;  // function
    body[kDeviceId + 8U] = 0x1FU; // device
    putU16(body, kDeviceId + 9U, 0x0001U);

    // Device Capabilities bits[4:3] = 2.
    putU32(body, kCapStruct + 4U, 0x2U << 3U);
    // Device Status bit 6 set, bit 7 (RsvdZ) set too: only bit 6 counts.
    putU16(body, kCapStruct + 10U, 0xC0U);
    // Device Capabilities 2: TPH bits[13:12] = 2, OBFF bits[19:18] = 1,
    // EPR bits[25:24] = 2.
    putU32(body, kCapStruct + 36U, (0x2U << 12U) | (0x1U << 18U) | (0x2U << 24U));
    // Root Control bit 3, Slot Capabilities 2 OOB PD bits[2:1] = 3.
    putU16(body, kCapStruct + 28U, 0x8U);
    putU32(body, kCapStruct + 52U, 0x6U);

    // AER Root Error Status: ERR_COR received, subclass 2, SFW handling
    // ERR_COR received (bit 9), interrupt message number 4, plus bits that
    // used to be misread as PME fields.
    putU32(body, kAer + 48U,
           0x1U | (0x2U << 7U) | (0x1U << 9U) | (0x4U << 27U) | (0x3U << 16U));
    // Uncorrectable severity bit 31.
    putU32(body, kAer + 12U, 0x80000000U);
    // Root Error Command: SFW handling source ID 0x1234.
    putU32(body, kAer + 44U, 0x1234U << 16U);

    const auto j = parse(body);

    const auto& did = j["deviceID"];
    if (did["slotNumber"] != 0x5U || did["slotNumber_Hex"] != "0x0005" ||
        did["segmentNumber_Hex"] != "0x0001" || did.contains("rcrbLowAddress"))
    {
        return 2;
    }

    const auto& cap = j["capabilityStructure"];
    const auto& dev_sts = cap["device_status"];
    if (dev_sts.contains("emergency_power_reduction") ||
        dev_sts["emergency_power_reduction_detected"] != true)
    {
        return 3;
    }
    if (cap["device_capabilities"]["phantom_functions_supported"]["value"] !=
            "TwoMostSignificantBits" ||
        cap["device_capabilities2"]["tph_completer_supported"]["value"] !=
            "Reserved" ||
        cap["device_capabilities2"]["obff_supported"]["value"] !=
            "MessageSignalingOnly" ||
        cap["device_capabilities2"]["emergency_power_reduction_supported"]
           ["value"] != "FormFactorOrDeviceSpecificMechanism")
    {
        return 4;
    }
    if (cap["root_control"]["pme_interrupt_enable"] != true ||
        cap["slot_capabilities2"]["oob_pd_supported"] != 3U)
    {
        return 5;
    }

    const auto& root = j["aerInfo"]["root_error_status"];
    if (root.contains("pme_requester_id") || root.contains("pme_status") ||
        root.contains("pme_pending") || root["err_cor_subclass"] != 2U ||
        root["sfw_handling_err_cor_received"] != true ||
        root["advanced_error_interrupt_message_number"] != 4U)
    {
        return 6;
    }
    if (j["aerInfo"]["uncorrectable_error_severity"]
           ["tlp_translation_egress_blocked_severity"] != "Fatal" ||
        j["aerInfo"]["root_error_command"]["sfw_handling_source_id"] !=
            0x1234U)
    {
        return 7;
    }

    // Bits 4, 5, 9, 10 and 11 are clear: those fields are omitted.
    if (j.contains("deviceSerialNumber") || j.contains("bridgeControlStatus") ||
        j.contains("rcrbHighAddress") || j.contains("dpcInfo") ||
        j.contains("flitLoggingDepth"))
    {
        return 8;
    }

    const auto r = addc::decoder::decode_pcie(j, 0U);
    // Raw bytes 13-14 rebuilt from the slot number (reserved bits dropped).
    if (r.data_array["device_id"] != "0x00000000000000031f000100000028")
    {
        return 9;
    }
    const auto& root_list = r.event_report["aer_info"]["root_error_status"];
    if (std::find(root_list.begin(), root_list.end(),
                  "SFW Handling ERR_COR Received") == root_list.end())
    {
        return 10;
    }

    // Device ID via RCRB (bit 8 only): bytes 7-10 are the RCRB low DWord.
    {
        std::vector<uint8_t> rcrb(362U, 0U);
        putU32(rcrb, 0U, 0x100U);
        putU32(rcrb, kDeviceId + 7U, 0xFED00000U);
        const auto rj = parse(rcrb);
        const auto& rdid = rj["deviceID"];
        if (rdid["rcrbLowAddress"] != 0xFED00000U ||
            rdid.contains("functionNumber") || rdid.contains("segmentNumber"))
        {
            return 11;
        }
        const auto rr = addc::decoder::decode_pcie(rj, 0U);
        if (rr.event_report["device_id"]["rcrb_low_address"] != "0xfed00000" ||
            rr.event_report["device_id"].contains("seg_bus_device_function") ||
            rr.event_report.contains("aer_info") ||
            rr.data_array.contains("aer_info"))
        {
            return 12;
        }
    }

    return 0;
}
