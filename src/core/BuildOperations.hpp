/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "core/Diagnostics.hpp"
#include "graph/Graph.hpp"

namespace axlum
{
    struct CommandContext
    {
        const Workspace& m_workspace;
        const Toolchain& m_toolchain;
        const BuildGraph& m_graph;
        fs::path m_buildDirectory, m_executable;
        Configuration m_configuration;
    };

    struct InstallRequest
    {
        fs::path m_prefix;
        std::string m_component = "all";
        bool m_noBuild = false, m_dryRun = false, m_install = false;
    };

    struct CleanRequest
    {
        std::string m_mode = "build";
        bool m_dependencies = true, m_dryRun = false;
    };

    class BuildOperations
    {
      public:
        BuildOperations() = delete;

      public:
        static int installTargets(
            const CommandContext& context,
            const std::vector<std::string>& targets,
            const InstallRequest& request,
            const PublishOptions& publication,
            const ExecutionOptions& execution
        );
        static int uninstallTargets(const fs::path& prefix, const std::vector<std::string>& targets, const std::string& component, bool dryRun);
        static int runTarget(
            const CommandContext& context,
            std::string target,
            const std::vector<std::string>& arguments,
            const fs::path& workingDirectory,
            const std::vector<std::string>& environment,
            bool noBuild,
            const ExecutionOptions& execution
        );
        static int cleanBuild(
            const fs::path& directory,
            const std::string& ninja,
            const std::vector<std::string>& targets,
            const CleanRequest& request,
            const ExecutionOptions& execution
        );
    };
} // namespace axlum
