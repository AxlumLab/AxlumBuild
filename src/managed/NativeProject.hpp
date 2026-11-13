/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"

namespace axlum
{

    class NativeProjectBuilder
    {
      public:
        NativeProjectBuilder() = delete;

      public:
        static std::string nativeProjectConfiguration(const Target&, Configuration);
        static std::string nativeProjectArchitecture(const Target&, const std::string&);
        static std::string nativeProjectGuid(const Target&);
        static void validateNativeProject(const Workspace&, const Target&);
        static void planNativeProject(
            BuildGraph&,
            const Workspace&,
            const Toolchain&,
            const fs::path&,
            Configuration,
            const fs::path&,
            const Target&,
            const std::vector<std::string>&,
            const std::vector<std::string>&
        );
        static std::vector<std::string> nativeProjectCommand(const fs::path& plan, bool clean = false);
        static int buildNativeProject(const std::vector<std::string>&);
    };
} // namespace axlum
