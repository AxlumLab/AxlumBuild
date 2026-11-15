/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"
#include <nlohmann/json.hpp>

namespace axlum
{
    class DslFeatures
    {
      public:
        DslFeatures() = delete;

      public:
        static std::string luaPrelude();
        static nlohmann::json readOptions(const fs::path& build);
        static nlohmann::json option(Workspace& ws, const nlohmann::json& declaration, const nlohmann::json& values);
        static void finalize(Workspace& ws, const fs::path& build, Configuration config);
        static bool systemPackage(const Workspace& ws, const std::string& name);
        static void resolveToolPackages(Workspace& ws, const fs::path& build, Configuration config, bool locked);
        static void prepare(Workspace& ws, const fs::path& build, Configuration config, const fs::path& self);
        static void plan(BuildGraph& graph, const Workspace& ws, const Toolchain& tc, const fs::path& build, Configuration config, const fs::path& self);
        static int execute(const fs::path& description);
        static int discoverTests(const fs::path& description);
        static int runTests(const fs::path& build, const std::string& pattern, const std::string& label);
        static void selectClosure(Workspace& ws, const std::vector<std::string>& roots);
    };
} // namespace axlum
