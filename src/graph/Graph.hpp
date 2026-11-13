/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "model/Model.hpp"
#include "toolchain/Toolchain.hpp"
#include <unordered_map>

namespace axlum
{
    struct Action
    {
        std::string m_name;
        std::string m_owner;
        std::vector<std::string> m_argv; // executable + arguments, never an unparsed shell string
        fs::path m_workingDirectory;
        std::vector<fs::path> m_inputs;
        std::vector<fs::path> m_outputs;
        std::vector<fs::path> m_orderOnly;
        fs::path m_depfile;
        std::string m_depsStyle; // gcc | msvc-json | msvc | empty
        bool m_always = false;
        bool m_restat = false;
        bool m_rsp = false; // argv[0] executes with response file containing argv[1:]
        bool m_gnuRsp = false;
        bool m_compile = false;
        fs::path m_source; // compile_commands.json
    };

    struct BuildGraph
    {
        std::map<std::string, std::string> m_environment, m_disabledTargets;
        std::vector<Action> m_actions;
        std::unordered_map<std::string, std::vector<fs::path>> m_targetOutputs;
        std::unordered_map<std::string, TargetKind> m_targetKinds;
        std::map<std::string, std::string> m_targetAliases, m_nativeProjectPlans;
        std::map<std::string, std::vector<std::string>> m_targetDependencies;
        void validate() const;
    };

    class BuildPlanner
    {
      public:
        BuildPlanner() = delete;

      public:
        static BuildGraph planBuild(const Workspace& ws, const Toolchain& tc, const fs::path& buildDir, Configuration cfg, const fs::path& self);
    };
} // namespace axlum
