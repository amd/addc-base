// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include <addc/lib_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    addc_error_info_t detail = {0};
    detail.struct_size = sizeof(detail);
    size_t required = 0U;
    if (addc_mca_supported_projects_json(NULL, &required, &detail) !=
            ADDC_STATUS_SUCCESS ||
        required < 2U)
    {
        return 2;
    }

    char* safe_json = (char*)malloc(required);
    if (!safe_json)
    {
        return 3;
    }
    size_t capacity = required;
    detail.struct_size = sizeof(detail);
    if (addc_mca_supported_projects_json(safe_json, &capacity, &detail) !=
            ADDC_STATUS_SUCCESS ||
        capacity != required || safe_json[required - 1U] != '\0' ||
        strstr(safe_json, "\"feature\":\"mca\"") == NULL)
    {
        free(safe_json);
        return 4;
    }

    char* json = NULL;
    char* error = NULL;
    if (addc_mca_supported_projects(&json, &error) != 0)
    {
        fputs("MCA project list failed: ", stderr);
        fputs(error ? error : "unknown error", stderr);
        fputc('\n', stderr);
        addc_free_string(error);
        free(safe_json);
        return 1;
    }

    const int failed =
        strstr(json, "\"feature\":\"mca\"") == NULL ||
        strstr(json, "\"available\":true") == NULL ||
        strstr(json, "\"name\":\"" ADDC_TEST_PROJECT "\"") == NULL ||
        strstr(json, "\"decode_levels\"") != NULL ||
        strstr(json, "\"name\":\"mi450x\"") != NULL ||
        strstr(json, "\"name\":\"mi410\"") != NULL ||
        strstr(json, "\"name\":\"mi430\"") != NULL ||
        strstr(json, "\"name\":\"mi440\"") != NULL;

    if (failed)
    {
        fputs("MCA project-list contract mismatch: ", stderr);
        fputs(json, stderr);
        fputc('\n', stderr);
    }
    if (!failed && strcmp(safe_json, json) != 0)
    {
        fputs("safe and compatibility project lists differ\n", stderr);
        free(safe_json);
        addc_free_string(json);
        return 5;
    }
    free(safe_json);
    addc_free_string(json);
    return failed ? 1 : 0;
}
