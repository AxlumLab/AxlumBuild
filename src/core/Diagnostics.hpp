/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "core/Process.hpp"
#include <chrono>
#include <fstream>

namespace axlum
{
    struct ExecutionOptions
    {
        int m_jobs = 0;
        bool m_verbose = false, m_explain = false, m_timing = false, m_dryRun = false;
        fs::path m_logfile, m_report, m_trace;
    };

    class CommandSession
    {
      public:
        CommandSession(std::string command, fs::path directory, ExecutionOptions options, std::vector<std::string> targets = {});
        int
        run(const std::string& phase,
            const std::vector<std::string>& args,
            const fs::path& cwd = {},
            const std::map<std::string, std::string>& environment = {});
        void finish(int exitCode);
        void recordFailure(const std::string& error);
        ~CommandSession();

      private:
        std::string m_command;
        fs::path m_directory;
        ExecutionOptions m_options;
        std::vector<std::string> m_targets;
        std::chrono::steady_clock::time_point m_start;
        std::ofstream m_log;
        std::string m_phases = "[]", m_actions = "[]", m_events = "[]";
        bool m_finished = false;
        std::string m_error;
        void emit(std::string_view text);
    };

    class BuildExecutor
    {
      public:
        BuildExecutor() = delete;

      public:
        static int executeNinja(
            const fs::path& directory,
            const std::string& ninja,
            const std::string& command,
            const std::vector<std::string>& targets,
            const ExecutionOptions& options
        );
    };
} // namespace axlum
