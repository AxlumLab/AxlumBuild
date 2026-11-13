/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace axlum
{
    namespace fs = std::filesystem;

    class Process
    {
      public:
        Process() = delete;

      public:
        static int
        run(const std::vector<std::string>& args,
            const fs::path& workingDir = {},
            const std::map<std::string, std::string>& environment = {},
            const std::function<void(std::string_view)>& output = {});
        static std::string findExecutable(const std::string& executable);

        static std::string quoteShellArg(const std::string& arg);
        static std::string quoteRspArg(const std::string& arg, bool gnu = false);
    };

    class NinjaSyntax
    {
      public:
        NinjaSyntax() = delete;

      public:
        static std::string escapeNinjaPath(const std::string& path);
        static std::string escapeNinjaValue(const std::string& value);
    };

    class FileSystem
    {
      public:
        FileSystem() = delete;

      public:
        static void writeIfChanged(const fs::path& path, const std::string& contents);
        static std::string readAll(const fs::path& path);
        static std::string shortHash(std::string_view input);
        static bool isWithin(const fs::path& child, const fs::path& parent);
        static void writeMsvcDepfile(const fs::path& jsonFile, const fs::path& depfile, const fs::path& object);
        static void writeEnvironment(const fs::path& file, const std::map<std::string, std::string>& environment);
        static std::map<std::string, std::string> readEnvironment(const fs::path& file);
    };
} // namespace axlum
