/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "core/Diagnostics.hpp"
#include "managed/NativeProject.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace axlum
{
    namespace
    {
        using Json = nlohmann::json;

        double ms(std::chrono::steady_clock::duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
        }

        std::string contents(const fs::path& file)
        {
            return fs::is_regular_file(file) ? FileSystem::readAll(file) : "";
        }
    } // namespace

    CommandSession::CommandSession(std::string command, fs::path directory, ExecutionOptions options, std::vector<std::string> targets)
        : m_command(std::move(command)), m_directory(std::move(directory)), m_options(std::move(options)), m_targets(std::move(targets)),
          m_start(std::chrono::steady_clock::now())
    {
        if (m_options.m_report.empty())
            m_options.m_report = m_directory / (m_command == "publish" ? "axlum.last-publish.json" : "axlum.last-build.json");
        if (!m_options.m_logfile.empty())
        {
            const auto parent = fs::absolute(m_options.m_logfile).parent_path();
            fs::create_directories(parent);
            m_log.open(m_options.m_logfile, std::ios::binary | std::ios::trunc);
            if (!m_log)
                throw std::runtime_error("cannot open logfile: " + m_options.m_logfile.string());
        }
    }

    void CommandSession::emit(std::string_view text)
    {
#ifdef _WIN32
        std::size_t offset = 0;
        while (offset < text.size())
        {
            DWORD written = 0;
            if (!WriteFile(
                    GetStdHandle(STD_OUTPUT_HANDLE),
                    text.data() + offset,
                    static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, 65536)),
                    &written,
                    nullptr
                ) ||
                !written)
                break;
            offset += written;
        }
#else
        std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
        std::cout.flush();
#endif
        if (m_log.is_open())
        {
            m_log.write(text.data(), static_cast<std::streamsize>(text.size()));
            m_log.flush();
        }
    }

    void CommandSession::recordFailure(const std::string& error)
    {
        m_error = error;
        emit("[axlum] " + error + "\n");
    }

    int CommandSession::run(
        const std::string& phase,
        const std::vector<std::string>& args,
        const fs::path& cwd,
        const std::map<std::string, std::string>& environment
    )
    {
        const auto before = contents(m_directory / ".ninja_log");
        const auto begin = std::chrono::steady_clock::now();
        auto output = [&](std::string_view text)
        {
            this->emit(text);
        };
        if (m_options.m_verbose)
        {
            std::string text = "[axlum " + phase + "]";
            for (const auto& arg : args)
                text += " " + Process::quoteShellArg(arg);
            emit(text + "\n");
        }
        int rc = 2;
        std::string error;
        try
        {
            rc = Process::run(args, cwd, environment, output);
        }
        catch (const std::exception& e)
        {
            error = e.what();
            emit(error + "\n");
        }
        if (rc)
            m_error = error.empty() ? phase + " failed (exit=" + std::to_string(rc) + ")" : error;
        const auto end = std::chrono::steady_clock::now();
        auto phases = Json::parse(m_phases);
        phases.push_back({{"name", phase}, {"arguments", args}, {"durationMs", ms(end - begin)}, {"exitCode", rc}, {"error", error}});
        m_phases = phases.dump();
        auto events = Json::parse(m_events);
        events.push_back(
            {{"name", phase},
             {"cat", "axlum.phase"},
             {"ph", "X"},
             {"pid", 1},
             {"tid", 1},
             {"ts", ms(begin - m_start) * 1000.0},
             {"dur", ms(end - begin) * 1000.0},
             {"args", {{"exitCode", rc}}}}
        );
        const auto after = contents(m_directory / ".ninja_log");
        std::string appended;
        if (after.starts_with(before))
            appended = after.substr(before.size());
        else
        {
            // Ninja can compact the log; compare complete records rather than stale byte offsets.
            std::multiset<std::string> previous;
            std::istringstream old(before);
            std::string line;
            while (std::getline(old, line))
                previous.insert(line);
            std::istringstream current(after);
            while (std::getline(current, line))
            {
                const auto found = previous.find(line);
                if (found == previous.end())
                    appended += line + "\n";
                else
                    previous.erase(found);
            }
        }
        auto actions = Json::parse(m_actions);
        std::map<std::string, Json> grouped;
        std::istringstream lines(appended);
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::vector<std::string> fields;
            std::istringstream columns(line);
            std::string field;
            while (std::getline(columns, field, '\t'))
                fields.push_back(field);
            if (fields.size() != 5)
                continue;
            try
            {
                const auto first = std::stoll(fields[0]), last = std::stoll(fields[1]);
                const auto key = fields[0] + ":" + fields[1] + ":" + fields[4];
                if (!grouped.contains(key))
                    grouped[key] = {{"startMs", first}, {"durationMs", last - first}, {"commandHash", fields[4]}, {"outputs", Json::array()}};
                grouped[key]["outputs"].push_back(fields[3]);
            }
            catch (const std::exception&)
            { /* Ignore malformed/older Ninja log records. */
            }
        }
        for (const auto& [key, action] : grouped)
        {
            (void)key;
            actions.push_back(action);
            events.push_back(
                {{"name", action.at("outputs").at(0)},
                 {"cat", "ninja.action"},
                 {"ph", "X"},
                 {"pid", 1},
                 {"tid", 2},
                 {"ts", ms(begin - m_start) * 1000.0 + action.at("startMs").get<double>() * 1000.0},
                 {"dur", action.at("durationMs").get<double>() * 1000.0},
                 {"args", action}}
            );
        }
        m_actions = actions.dump();
        m_events = events.dump();
        return rc;
    }

    void CommandSession::finish(int rc)
    {
        if (m_finished)
            return;
        const auto duration = ms(std::chrono::steady_clock::now() - m_start);
        Json report = {
            {"schemaVersion", 1},
            {"command", m_command},
            {"buildDirectory", m_directory.generic_string()},
            {"targets", m_targets},
            {"jobs", m_options.m_jobs},
            {"exitCode", rc},
            {"durationMs", duration},
            {"phases", Json::parse(m_phases)},
            {"actions", Json::parse(m_actions)},
            {"error", m_error},
            {"logfile", m_options.m_logfile.generic_string()},
            {"trace", m_options.m_trace.generic_string()}
        };
        FileSystem::writeIfChanged(m_options.m_report, report.dump(2) + "\n");
        if (!m_options.m_trace.empty())
            FileSystem::writeIfChanged(m_options.m_trace, Json{{"traceEvents", Json::parse(m_events)}, {"displayTimeUnit", "ms"}}.dump(2) + "\n");
        if (m_options.m_timing)
            std::cout << "[timing] " << m_command << ": " << duration << " ms; " << report["actions"].size() << " completed Ninja actions; exit=" << rc << '\n';
        m_finished = true;
    }

    CommandSession::~CommandSession()
    {
        if (!m_finished)
            try
            {
                finish(2);
            }
            catch (...)
            {
            }
    }

    int BuildExecutor::executeNinja(
        const fs::path& directory,
        const std::string& ninja,
        const std::string& command,
        const std::vector<std::string>& targets,
        const ExecutionOptions& options
    )
    {
        CommandSession session(command, directory, options, targets);
        const auto clean = [&]
        {
            if (fs::is_regular_file(directory / "axlum.artifacts.json"))
            {
                const auto artifacts = Json::parse(FileSystem::readAll(directory / "axlum.artifacts.json"));
                const auto selection = fs::is_regular_file(directory / "axlum.target-selection.json")
                                           ? Json::parse(FileSystem::readAll(directory / "axlum.target-selection.json"))
                                           : Json::object();
                std::set<std::string> visited, plans;
                std::function<void(std::string)> visit = [&](std::string name)
                {
                    if (selection.contains("aliases") && selection["aliases"].contains(name))
                        name = selection["aliases"][name].get<std::string>();
                    if (!visited.insert(name).second || !artifacts.contains(name))
                        return;
                    const auto& target = artifacts.at(name);
                    if (target.contains("nativeProjectPlan"))
                        plans.insert(target.at("nativeProjectPlan").get<std::string>());
                    if (target.contains("dependencies"))
                        for (const auto& dep : target.at("dependencies"))
                            visit(dep.get<std::string>());
                };
                if (targets.empty() || std::find(targets.begin(), targets.end(), "all") != targets.end())
                    for (const auto& [name, value] : artifacts.items())
                    {
                        (void)value;
                        visit(name);
                    }
                else
                    for (const auto& name : targets)
                        visit(name);
                const auto environment = FileSystem::readEnvironment(directory / "axlum.environment.json");
                for (const auto& file : plans)
                {
                    const auto commandLine = NativeProjectBuilder::nativeProjectCommand(file, true);
                    const int result = session.run("MSBuild clean", commandLine, fs::path(commandLine.at(1)).parent_path(), environment);
                    if (result)
                        return result;
                }
            }
            std::vector<std::string> args = {ninja, "-C", directory.string(), "-t", "clean"};
            args.insert(args.end(), targets.begin(), targets.end());
            return session.run("clean", args);
        };
        int rc = 0;
        if (command == "clean" || command == "rebuild")
            rc = clean();
        if (rc == 0 && command != "clean")
        {
            std::vector<std::string> args = {ninja, "-C", directory.string()};
            if (options.m_jobs)
                args.insert(args.end(), {"-j", std::to_string(options.m_jobs)});
            if (options.m_verbose)
                args.push_back("-v");
            if (options.m_explain)
                args.insert(args.end(), {"-d", "explain"});
            if (options.m_dryRun)
                args.push_back("-n");
            args.insert(args.end(), targets.begin(), targets.end());
            std::map<std::string, std::string> environment;
            if (command == "rebuild")
            {
                environment["AXLUM_DOTNET_REBUILD"] = "1";
                environment["AXLUM_REBUILD"] = "1";
            }
            if (options.m_jobs)
                environment["AXLUM_BUILD_JOBS"] = std::to_string(options.m_jobs);
            rc = session.run("build", args, {}, environment);
        }
        session.finish(rc);
        return rc;
    }
} // namespace axlum
