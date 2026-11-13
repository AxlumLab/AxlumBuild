/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/Runtime.hpp"
#include "graph/NativePaths.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <stdexcept>

namespace axlum
{
    void RuntimePlanner::planRuntimeCopies(BuildGraph& graph, const Workspace& ws, const fs::path& build, Configuration config, const fs::path& self)
    {
        std::unordered_map<std::string, const Target*> targets;
        for (const auto& t : ws.m_targets)
            targets.emplace(t.m_name, &t);
        auto key = [](const fs::path& path)
        {
            auto text = fs::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
#endif
            return text;
        };
        std::map<std::string, fs::path> produced, copies;
        for (const auto& action : graph.m_actions)
            for (const auto& file : action.m_outputs)
                produced.emplace(key(file), file);
        for (const auto& consumer : ws.m_targets)
        {
            const bool managed = consumer.m_kind == TargetKind::DotNetProject && consumer.m_generatedProject;
            if (consumer.m_kind != TargetKind::Executable && consumer.m_kind != TargetKind::SharedLibrary && !managed)
                continue;
            auto configuration = BuildModel::toString(config);
            std::transform(
                configuration.begin(),
                configuration.end(),
                configuration.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
            const auto directory = managed ? build / "dotnet" / consumer.m_name / "artifacts/bin" / fs::path(consumer.m_project).stem() / configuration
                                           : graph.m_targetOutputs.at(consumer.m_name).front().parent_path();
            std::set<std::string> visited;
            std::vector<fs::path> sources;
            std::function<void(const Target&)> collect = [&](const Target& t)
            {
                if (!visited.insert(t.m_name).second)
                    return;
                if (t.m_kind == TargetKind::SharedLibrary && t.m_name != consumer.m_name)
                    sources.push_back(graph.m_targetOutputs.at(t.m_name).front());
                for (const auto* files : {&t.m_runtimeFiles, &t.m_optionalRuntimeFiles})
                {
                    for (const auto& file : *files)
                    {
                        if (managed && (file.find("${rid}") != std::string::npos || file.find("${mode}") != std::string::npos))
                            continue;
                        const auto path = NativePaths::resolveBuildInput(ws, build, NativePaths::expandBuildValue(file, ws, build, config, t.m_name));
                        if (files == &t.m_optionalRuntimeFiles && !fs::is_regular_file(path) && !produced.contains(key(path)))
                            continue;
                        sources.push_back(path);
                    }
                }
                for (const auto* deps : {&t.m_publicDependencies, &t.m_privateDependencies, &t.m_interfaceDependencies})
                    for (const auto& name : *deps)
                        collect(*targets.at(name));
            };
            collect(consumer);
            for (const auto& source : sources)
            {
                const auto destination = directory / source.filename();
                if (key(source) == key(destination))
                {
                    // An explicitly attached runtime producer may already write beside the app.
                    // It still belongs to the selected target's build closure (e.g. a plugin DLL).
                    const auto existing = copies.find(key(destination));
                    if (existing != copies.end() && key(existing->second) != key(source))
                        throw std::runtime_error("runtime file collision at " + destination.string());
                    copies.emplace(key(destination), source);
                    auto& outputs = graph.m_targetOutputs.at(consumer.m_name);
                    if (std::find(outputs.begin(), outputs.end(), source) == outputs.end())
                        outputs.push_back(source);
                    continue;
                }
                const auto existing = copies.find(key(destination));
                if (existing != copies.end())
                {
                    if (key(existing->second) != key(source))
                        throw std::runtime_error("runtime file collision at " + destination.string());
                }
                else
                {
                    if (produced.contains(key(destination)))
                        throw std::runtime_error("runtime copy overlaps a target output: " + destination.string());
                    copies.emplace(key(destination), source);
                    Action copy;
                    copy.m_name = "Copy runtime " + source.filename().string();
                    copy.m_owner = consumer.m_name;
                    copy.m_workingDirectory = ws.m_root;
                    copy.m_inputs = {source};
                    copy.m_outputs = {destination};
                    copy.m_restat = true;
                    if (managed)
                        copy.m_orderOnly.push_back(graph.m_targetOutputs.at(consumer.m_name).front());
                    copy.m_argv = {self.string(), "__copy", source.string(), destination.string()};
                    graph.m_actions.push_back(std::move(copy));
                }
                auto& outputs = graph.m_targetOutputs.at(consumer.m_name);
                if (std::find(outputs.begin(), outputs.end(), destination) == outputs.end())
                    outputs.push_back(destination);
            }
        }
        // Host tools must have their copied DLLs available before a code generation action starts.
        for (const auto& t : ws.m_targets)
        {
            if (t.m_kind != TargetKind::Codegen || !t.m_tool.starts_with("@target/"))
                continue;
            const auto& outputs = graph.m_targetOutputs.at(t.m_tool.substr(8));
            for (auto& action : graph.m_actions)
                if (action.m_owner == t.m_name)
                    for (const auto& output : outputs)
                        if (std::find(action.m_orderOnly.begin(), action.m_orderOnly.end(), output) == action.m_orderOnly.end())
                            action.m_orderOnly.push_back(output);
        }
    }
} // namespace axlum
