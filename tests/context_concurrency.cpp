// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include <addc/lib_c.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
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

std::string decode(addc_context_t* context, const std::vector<uint8_t>& cper)
{
    size_t required = 0U;
    if (addc_cper_decode_json(context, cper.data(), cper.size(), nullptr,
                              &required) != ADDC_STATUS_SUCCESS ||
        required < 2U)
    {
        return {};
    }

    std::vector<char> json(required);
    size_t capacity = json.size();
    if (addc_cper_decode_json(context, cper.data(), cper.size(), json.data(),
                              &capacity) != ADDC_STATUS_SUCCESS ||
        capacity != required)
    {
        return {};
    }
    return std::string{json.data()};
}

} // namespace

int main()
{
    const auto cper = minimalCper();
    addc_context_t* context = addc_context_create();
    if (context == nullptr)
    {
        return 1;
    }

    const std::string expected = decode(context, cper);
    if (expected.empty())
    {
        addc_context_destroy(context);
        return 2;
    }

    std::atomic_bool failed = false;
    std::vector<std::thread> workers;
    constexpr unsigned worker_count = 8U;
    constexpr unsigned iterations = 32U;
    workers.reserve(worker_count);
    for (unsigned worker = 0U; worker < worker_count; ++worker)
    {
        workers.emplace_back([&] {
            for (unsigned iteration = 0U; iteration < iterations; ++iteration)
            {
                if (decode(context, cper) != expected)
                {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    addc_context_destroy(context);
    return failed.load(std::memory_order_relaxed) ? 3 : 0;
}
