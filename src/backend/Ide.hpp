/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"
#include <functional>

namespace axlum
{
    struct IDEContext
    {
        Workspace m_workspace;
        Toolchain m_toolchain;
        BuildGraph m_graph;
        fs::path m_buildDirectory;
        Configuration m_configuration;
        std::string m_label;
    };

    using IDEProjectCustomizer = std::function<void(const IDEContext&, const std::string& target, const std::string& format, IDECustomization&)>;

    class IDEGenerator
    {
      public:
        IDEGenerator() = delete;

      public:
        static void generateIDE(
            const std::vector<IDEContext>& configurations,
            const fs::path& output,
            const fs::path& executable,
            const std::string& format,
            const std::vector<IDEProjectCustomizer>& customizers = {}
        );
        static void writeArtifactIndex(const BuildGraph& graph, const fs::path& directory);
    };

} // namespace axlum
