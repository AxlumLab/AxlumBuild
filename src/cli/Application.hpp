/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "backend/Ide.hpp"
#include "core/BuildOperations.hpp"
#include <map>
#include <string>
#include <vector>

namespace CLI
{
    class App;
}

namespace axlum
{
    class Application
    {
      public:
        int run(int argc, char** argv);

      private:
        struct Arguments
        {
            std::string m_command;
            std::string m_file = "axlum.build.lua";
            std::string m_buildDir = ".axlum/build";
            std::string m_config = "debug";
            std::string m_compiler = "auto";
            std::string m_target;
            std::vector<std::string> m_targets;
            ExecutionOptions m_execution;
            PublishOptions m_publication;
            bool m_locked = false;
            std::string m_toolchain;
            std::string m_architecture = "x64", m_platform, m_preset, m_format = "all", m_output;
            std::vector<std::string> m_presets;
            bool m_configureOnly = false;
            InstallRequest m_installation;
            CleanRequest m_cleaning;
            bool m_noBuild = false, m_json = false, m_strict = false, m_cleanFirst = false, m_cleanAll = false, m_buildFirst = false;
            std::string m_sourceDir, m_generator = "Ninja", m_requestedConfig, m_workingDirectory;
            std::vector<std::string> m_runArguments, m_environment;
        };

        Arguments m_options;

      private:
        std::string needArg(const std::vector<std::string>& args, std::size_t& i);
        void configureCli(CLI::App& app, Arguments& options);
        std::map<std::string, std::string> readState(const fs::path& dir);
        void printGraph(const BuildGraph& graph);
        fs::path currentExecutable(const std::string& arg0);
        IDEContext configure(Arguments opt, const fs::path& buildDir, const fs::path& self, bool automatic = false, bool skipEmpty = false);
        IDEContext refresh(const fs::path& buildDir, const fs::path& self);
        int configureEntries(const Arguments& options, const fs::path& self);
        int doctor(const Arguments& options);
        int runWrapped(const std::vector<std::string>& args);
    };
} // namespace axlum
