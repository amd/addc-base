// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include <addc/lib_c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(addc_cper_summary_entry_t) == 424U,
               "summary entry ABI size changed");
_Static_assert(offsetof(addc_cper_summary_entry_t, reserved) == 392U,
               "summary entry ABI layout changed");
_Static_assert(sizeof(addc_error_info_t) == 344U,
               "error detail ABI size changed");
_Static_assert(offsetof(addc_error_info_t, message) == 56U,
               "error detail ABI layout changed");

static void putU16(uint8_t* bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static void putU32(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint8_t* minimalCper(size_t* size_out)
{
    const size_t header_size = 128U;
    const size_t descriptor_size = 72U;
    const size_t body_offset = header_size + descriptor_size;
    const size_t size = body_offset + 1U;
    uint8_t* bytes = (uint8_t*)calloc(size, 1U);
    if (!bytes)
    {
        return NULL;
    }

    putU32(bytes, 0U, 0x52455043U);
    putU32(bytes, 6U, 0xFFFFFFFFU);
    putU16(bytes, 10U, 1U);
    putU32(bytes, 20U, (uint32_t)size);
    putU32(bytes, header_size, (uint32_t)body_offset);
    putU32(bytes, header_size + 4U, 1U);
    *size_out = size;
    return bytes;
}

#ifdef ADDC_HAS_VENICE
static uint8_t* sentinelAfidCper(size_t* size_out)
{
    const size_t header_size = 128U;
    const size_t descriptor_size = 72U;
    const size_t body_offset = header_size + descriptor_size;
    const size_t body_size = 16U;
    const size_t size = body_offset + body_size;
    uint8_t* bytes = (uint8_t*)calloc(size, 1U);
    if (!bytes)
    {
        return NULL;
    }

    putU32(bytes, 0U, 0x52455043U);
    putU32(bytes, 6U, 0xFFFFFFFFU);
    putU16(bytes, 10U, 1U);
    putU32(bytes, 20U, (uint32_t)size);

    putU32(bytes, header_size, (uint32_t)body_offset);
    putU32(bytes, header_size + 4U, (uint32_t)body_size);
    putU16(bytes, header_size + 8U, 0x3100U);
    bytes[header_size + 10U] = 0x2U;
    putU16(bytes, header_size + 20U, 0x1022U);
    static const char sentinel_fru[] = "SENTINEL_FRU";
    for (size_t i = 0; i < sizeof(sentinel_fru) - 1U; ++i)
    {
        bytes[header_size + 52U + i] = (unsigned char)sentinel_fru[i];
    }

    *size_out = size;
    return bytes;
}
#endif

static uint8_t* readFile(const char* path, size_t* size_out)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }

    const long file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }

    uint8_t* bytes = (uint8_t*)malloc((size_t)file_size);
    if (!bytes)
    {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1U, (size_t)file_size, file) != (size_t)file_size)
    {
        free(bytes);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *size_out = (size_t)file_size;
    return bytes;
}

static int checkJson(addc_context_t* ctx, const uint8_t* cper, size_t cper_size)
{
    size_t required = 0U;
    addc_error_info_t error_info = {0};
    error_info.struct_size = sizeof(error_info);
    addc_status_t status = addc_cper_decode_json_ex(
        ctx, cper, cper_size, "source.cper", NULL, &required, &error_info);
    if (status != ADDC_STATUS_SUCCESS || required < 2U)
    {
        return 1;
    }

    char tiny[1] = {0};
    size_t tiny_size = sizeof(tiny);
    error_info.struct_size = sizeof(error_info);
    status = addc_cper_decode_json_ex(ctx, cper, cper_size, "source.cper", tiny,
                                      &tiny_size, &error_info);
    if (status != ADDC_STATUS_INSUFFICIENT_SIZE || tiny_size != required ||
        tiny[0] != '\0' || error_info.status != ADDC_STATUS_INSUFFICIENT_SIZE)
    {
        return 2;
    }

    char* json = (char*)malloc(required);
    if (!json)
    {
        return 3;
    }
    size_t capacity = required;
    error_info.struct_size = sizeof(error_info);
    status = addc_cper_decode_json_ex(ctx, cper, cper_size, "source.cper", json,
                                      &capacity, &error_info);
    const int failed =
        status != ADDC_STATUS_SUCCESS || capacity != required ||
        json[required - 1U] != '\0' || strstr(json, "\"events\"") == NULL ||
        strstr(json, "\"filename\": \"source.cper\"") == NULL ||
        error_info.status != ADDC_STATUS_SUCCESS ||
        error_info.message[0] != '\0';
    free(json);
    return failed ? 4 : 0;
}

static int checkSummary(addc_context_t* ctx, const uint8_t* cper,
                        size_t cper_size, int require_entries)
{
    size_t required = 0U;
    addc_status_t status =
        addc_cper_get_summary(ctx, cper, cper_size, NULL, &required);
    if (status != ADDC_STATUS_SUCCESS || (require_entries && required == 0U) ||
        (!require_entries && required != 0U))
    {
        return 1;
    }

    if (required == 0U)
    {
        addc_cper_summary_entry_t untouched = {0};
        size_t capacity = 1U;
        status =
            addc_cper_get_summary(ctx, cper, cper_size, &untouched, &capacity);
        return status == ADDC_STATUS_SUCCESS && capacity == 0U ? 0 : 2;
    }

    addc_cper_summary_entry_t* entries =
        (addc_cper_summary_entry_t*)calloc(required, sizeof(*entries));
    if (!entries)
    {
        return 3;
    }

    size_t too_small = required - 1U;
    status = addc_cper_get_summary(ctx, cper, cper_size, entries, &too_small);
    if (status != ADDC_STATUS_INSUFFICIENT_SIZE || too_small != required)
    {
        free(entries);
        return 4;
    }

    size_t capacity = required;
    status = addc_cper_get_summary(ctx, cper, cper_size, entries, &capacity);
    if (status != ADDC_STATUS_SUCCESS || capacity != required)
    {
        free(entries);
        return 5;
    }

    for (size_t i = 0U; i < required; ++i)
    {
        if (entries[i].fru_id[ADDC_CPER_FRU_ID_MAX_LENGTH - 1U] != '\0' ||
            entries[i].fru_text[ADDC_CPER_FRU_TEXT_MAX_LENGTH - 1U] != '\0' ||
            entries[i].additional_context[
                ADDC_CPER_ADDITIONAL_CONTEXT_MAX_LENGTH - 1U] != '\0')
        {
            free(entries);
            return 6;
        }
        for (size_t j = 0U; j < 4U; ++j)
        {
            if (entries[i].reserved[j] != 0U)
            {
                free(entries);
                return 8;
            }
        }
    }

    free(entries);
    return 0;
}

static int checkSentinelAfid(addc_context_t* ctx)
{
#ifndef ADDC_HAS_VENICE
    (void)ctx;
    return 0;
#else
    size_t cper_size = 0U;
    uint8_t* cper = sentinelAfidCper(&cper_size);
    if (!cper)
    {
        return 1;
    }

    size_t count = 0U;
    addc_status_t status =
        addc_cper_get_summary(ctx, cper, cper_size, NULL, &count);
    if (status != ADDC_STATUS_SUCCESS || count != 1U)
    {
        free(cper);
        return 2;
    }

    addc_cper_summary_entry_t entry = {0};
    status = addc_cper_get_summary(ctx, cper, cper_size, &entry, &count);
    if (status != ADDC_STATUS_SUCCESS || count != 1U ||
        entry.afid != ADDC_UNCLASSIFIED_AFID ||
        strcmp(entry.fru_text, "SENTINEL_FRU") != 0 ||
        entry.additional_context[0] != '\0')
    {
        free(cper);
        return 3;
    }

    char* json = NULL;
    char* error = NULL;
    const int json_status = addc_get_error_summary_ctx(
        ctx, cper, cper_size, "sentinel.cper", &json, &error);
#define ADDC_STRINGIFY_VALUE(value) #value
#define ADDC_STRINGIFY(value) ADDC_STRINGIFY_VALUE(value)
    static const char expected_afid[] =
        "\"afid\": " ADDC_STRINGIFY(ADDC_UNCLASSIFIED_AFID);
    const int failed = json_status != 0 || !json || error ||
                       strstr(json ? json : "", expected_afid) == NULL;

    addc_free_string(json);
    addc_free_string(error);
    free(cper);
    return failed ? 4 : 0;
#endif
}

static int checkSafeParseAndMca(addc_context_t* ctx, const uint8_t* cper,
                                size_t cper_size)
{
    addc_error_info_t error = {0};
    error.struct_size = sizeof(error);
    size_t required = 0U;
    addc_status_t status =
        addc_cper_parse_json(ctx, cper, cper_size, NULL, &required, &error);
    if (status != ADDC_STATUS_SUCCESS || required < 2U)
    {
        return 1;
    }

    char* json = (char*)malloc(required);
    if (!json)
    {
        return 2;
    }
    size_t capacity = required;
    error.struct_size = sizeof(error);
    status =
        addc_cper_parse_json(ctx, cper, cper_size, json, &capacity, &error);
    if (status != ADDC_STATUS_SUCCESS || capacity != required ||
        json[required - 1U] != '\0')
    {
        free(json);
        return 3;
    }
    free(json);

    const uint8_t invalid_cper[128] = {0};
    required = 0U;
    error.struct_size = sizeof(error);
    status = addc_cper_parse_json(ctx, invalid_cper, sizeof(invalid_cper), NULL,
                                  &required, &error);
    if (status != ADDC_STATUS_PARSE_ERROR ||
        strcmp(error.source, "cper") != 0 ||
        (error.flags & ADDC_ERROR_INFO_HAS_OFFSET) == 0U)
    {
        return 4;
    }

    const addc_mca_regs_t regs = {0};
    required = 0U;
    error.struct_size = sizeof(error);
    status = addc_mca_decode_json(ctx, ADDC_TEST_PROJECT, &regs, NULL,
                                  &required, &error);
    if (status != ADDC_STATUS_SUCCESS || required < 2U)
    {
        return 5;
    }

    error.struct_size = sizeof(error);
    status = addc_mca_decode_json(ctx, "not-a-project", &regs, NULL, &required,
                                  &error);
    if (status != ADDC_STATUS_UNSUPPORTED ||
        strcmp(error.source, "mca") != 0 ||
        strstr(error.message, "unsupported_by_build") == NULL)
    {
        return 6;
    }

    return 0;
}

int main(int argc, char** argv)
{
    size_t cper_size = 0U;
    // Meson supplies this optional corpus fixture path to the test process.
    // codeql[cpp/path-injection]
    uint8_t* cper = argc > 1 ? readFile(argv[1], &cper_size)
                             : minimalCper(&cper_size);
    if (!cper)
    {
        return 10;
    }

    addc_context_t* ctx = addc_context_create();
    if (!ctx)
    {
        free(cper);
        return 11;
    }

    addc_context_options_t strict_options = {0};
    strict_options.struct_size = sizeof(strict_options);
    strict_options.parse_mode = ADDC_PARSE_MODE_STRICT;
    addc_context_t* strict_ctx =
        addc_context_create_with_options(&strict_options);
    if (!strict_ctx)
    {
        addc_context_destroy(ctx);
        free(cper);
        return 15;
    }
    addc_context_destroy(strict_ctx);

    // Exercise rejection of an intentionally invalid enum value.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    strict_options.parse_mode = (addc_parse_mode_t)99;
    if (addc_context_create_with_options(&strict_options) != NULL)
    {
        addc_context_destroy(ctx);
        free(cper);
        return 16;
    }

    if (strcmp(addc_status_string(ADDC_STATUS_INSUFFICIENT_SIZE),
               "insufficient size") != 0 ||
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        strcmp(addc_status_string((addc_status_t)999), "unknown status") != 0)
    {
        addc_context_destroy(ctx);
        free(cper);
        return 12;
    }

    size_t size = 0U;
    if (addc_cper_decode_json(NULL, cper, cper_size, NULL, &size) !=
            ADDC_STATUS_INVALID_ARGUMENT ||
        addc_cper_decode_json(ctx, NULL, 0U, NULL, &size) !=
            ADDC_STATUS_INVALID_ARGUMENT ||
        addc_cper_get_summary(ctx, cper, cper_size, NULL, NULL) !=
            ADDC_STATUS_INVALID_ARGUMENT)
    {
        addc_context_destroy(ctx);
        free(cper);
        return 13;
    }

    const uint8_t invalid_cper[128] = {0};
    addc_error_info_t error_info = {0};
    error_info.struct_size = sizeof(error_info);
    if (addc_cper_decode_json_ex(ctx, invalid_cper, sizeof(invalid_cper),
                                 "bad.cper", NULL, &size, &error_info) !=
            ADDC_STATUS_PARSE_ERROR ||
        error_info.status != ADDC_STATUS_PARSE_ERROR ||
        strcmp(error_info.source, "cper") != 0 ||
        strstr(error_info.message, "signature") == NULL ||
        (error_info.flags & ADDC_ERROR_INFO_HAS_OFFSET) == 0U ||
        error_info.byte_offset != 0U)
    {
        addc_context_destroy(ctx);
        free(cper);
        return 14;
    }

    error_info.struct_size = sizeof(uint32_t);
    if (addc_cper_decode_json_ex(ctx, cper, cper_size, NULL, NULL, &size,
                                 &error_info) != ADDC_STATUS_INVALID_ARGUMENT)
    {
        addc_context_destroy(ctx);
        free(cper);
        return 17;
    }

    const int json_result = checkJson(ctx, cper, cper_size);
    const int summary_result = checkSummary(ctx, cper, cper_size, argc > 1);
    const int sentinel_result = checkSentinelAfid(ctx);
    const int safe_operation_result =
        checkSafeParseAndMca(ctx, cper, cper_size);

    addc_context_destroy(ctx);
    free(cper);

    if (json_result != 0)
    {
        return 20 + json_result;
    }
    if (summary_result != 0)
    {
        return 30 + summary_result;
    }
    if (sentinel_result != 0)
    {
        return 40 + sentinel_result;
    }
    if (safe_operation_result != 0)
    {
        return 50 + safe_operation_result;
    }
    return 0;
}
