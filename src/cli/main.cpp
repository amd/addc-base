// Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
// SPDX-License-Identifier: MIT
#include "addc/detail/format.hpp"
#include "addc/lib.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

void usage(std::string_view argv0)
{
    std::cerr << "Usage:\n"
              << "  " << argv0 << " mca --list-projects\n"
              << "  " << argv0
              << " analyze <file.cper> [--strict] [--output <file.json>]\n"
              << "  " << argv0
              << " parse   <file.cper> [--strict] [--output <file.json>]\n";
}

void writeOutput(const nlohmann::ordered_json& j, const std::string& out_path)
{
    if (out_path.empty())
    {
        std::cout << j.dump(2) << '\n';
    }
    else
    {
        std::ofstream f(out_path);
        if (!f)
        {
            throw std::runtime_error(
                addc::format("cannot write '{}'", out_path));
        }
        f << j.dump(2) << '\n';
        std::cerr << "Written to " << out_path << '\n';
    }
}

int cmdParse(const std::filesystem::path& cper_path,
             const std::string& out_path, const addc::DecodeOptions& options)
{
    std::string error;
    const auto ir = addc::parse_cper_file(cper_path.string(), options, &error);
    if (!ir)
    {
        std::cerr << "Error: " << error << '\n';
        return 1;
    }
    writeOutput(*ir, out_path);
    return 0;
}

int cmdAnalyze(const std::filesystem::path& cper_path,
               const std::string& out_path, const addc::DecodeOptions& options)
{
    std::string err;
    auto result =
        addc::analyze_cper_file_json(cper_path.string(), options, &err);

    if (!result)
    {
        std::cerr << "Error: " << err << '\n';
        return 1;
    }

    writeOutput(*result, out_path);
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc == 3 && std::string_view{argv[1]} == "mca" &&
        std::string_view{argv[2]} == "--list-projects")
    {
        std::cout << addc::mca_supported_projects_json() << '\n';
        return 0;
    }
    if (argc < 3)
    {
        usage(argv[0]);
        return 1;
    }

    const std::string_view cmd = argv[1];
    const std::filesystem::path path = argv[2];
    std::string out_path;
    addc::DecodeOptions options;

    for (int i = 3; i < argc; ++i)
    {
        if (std::string_view{argv[i]} == "--output" && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else if (std::string_view{argv[i]} == "--strict")
        {
            options.parse_mode = addc::ParseMode::Strict;
        }
    }

    try
    {
        if (cmd == "analyze")
        {
            // CLI file paths are explicitly selected by the invoking user.
            // codeql[cpp/path-injection]
            return cmdAnalyze(path, out_path, options);
        }
        if (cmd == "parse")
        {
            // CLI file paths are explicitly selected by the invoking user.
            // codeql[cpp/path-injection]
            return cmdParse(path, out_path, options);
        }
        std::cerr << "Unknown command: " << cmd << '\n';
        usage(argv[0]);
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
