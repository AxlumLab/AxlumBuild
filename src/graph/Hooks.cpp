/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/Hooks.hpp"
#include "core/Process.hpp"
#include "graph/NativePaths.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace axlum
{
    void HookPlanner::planHooks(BuildGraph& graph, const Workspace& ws, const Toolchain& tc, const fs::path& build, Configuration config)
    {
        std::map<std::string, std::vector<fs::path>> completion;
        std::map<std::string, const Target*> targets;
        for (const auto& t : ws.m_targets)
            targets[t.m_name] = &t;
        const auto original = graph.m_actions.size();
        std::vector<std::pair<std::size_t, std::string>> hostTools;
        for (const auto& t : ws.m_targets)
        {
            std::set<std::string> names;
            std::vector<fs::path> pre, post;
            auto output = [&](const std::string& value)
            {
                auto path = value.starts_with("@build/") ? build / value.substr(7) : build / value;
                path = fs::absolute(path).lexically_normal();
                if (path == build || !FileSystem::isWithin(fs::weakly_canonical(path), fs::weakly_canonical(build)))
                    throw std::runtime_error("build action output escapes build directory: " + value);
                return path;
            };
            for (const auto* hooks : {&t.m_preBuild, &t.m_postBuild})
                for (const auto& h : *hooks)
                {
                    if (!names.insert(h.m_name).second || h.m_tool.empty() || h.m_outputs.empty())
                        throw std::runtime_error("invalid or duplicate build action: " + t.m_name + "/" + h.m_name);
                    Action a;
                    a.m_name = (hooks == &t.m_preBuild ? "Pre-build " : "Post-build ") + t.m_name + ":" + h.m_name;
                    a.m_owner = t.m_name;
                    a.m_workingDirectory = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
                    a.m_restat = true;
                    auto expand = [&](const std::string& value)
                    {
                        auto expanded = value;
                        const auto token = expanded.find("${targetfile}");
                        if (token != std::string::npos)
                        {
                            if (graph.m_targetOutputs.at(t.m_name).empty())
                                throw std::runtime_error("targetfile requires a produced artifact");
                            expanded.replace(token, 13, graph.m_targetOutputs.at(t.m_name).front().string());
                        }
                        return NativePaths::expandBuildValue(expanded, ws, build, config, t.m_name);
                    };
                    std::string tool = expand(h.m_tool);
                    if (tool.starts_with("@target/"))
                    {
                        const auto name = tool.substr(8);
                        if (tc.m_host != tc.m_target || !targets.contains(name) || targets.at(name)->m_kind != TargetKind::Executable)
                            throw std::runtime_error("build action requires a host executable target: " + tool);
                        tool = graph.m_targetOutputs.at(name).front().string();
                        if (name != t.m_name)
                            hostTools.emplace_back(graph.m_actions.size(), name);
                    }
                    else if (tool.find_first_of("/\\") != std::string::npos)
                        tool = NativePaths::resolveBuildInput(ws, build, tool).string();
                    else
                        tool = Process::findExecutable(tool);
                    if (tool.empty())
                        throw std::runtime_error("build action tool not found: " + h.m_tool);
                    a.m_argv = {tool};
                    a.m_inputs.emplace_back(tool);
                    for (const auto& arg : h.m_args)
                        a.m_argv.push_back(expand(arg));
                    for (const auto& input : h.m_inputs)
                        a.m_inputs.push_back(NativePaths::resolveBuildInput(ws, build, expand(input)));
                    for (const auto& file : h.m_outputs)
                        a.m_outputs.push_back(output(expand(file)));
                    if (!h.m_depfile.empty())
                    {
                        a.m_depfile = output(expand(h.m_depfile));
                        a.m_depsStyle = "gcc";
                    }
                    auto& previous = hooks == &t.m_preBuild ? pre : post;
                    a.m_inputs.insert(a.m_inputs.end(), previous.begin(), previous.end());
                    if (hooks == &t.m_postBuild)
                    {
                        const auto& artifacts = graph.m_targetOutputs.at(t.m_name);
                        a.m_inputs.insert(a.m_inputs.end(), artifacts.begin(), artifacts.end());
                        a.m_orderOnly.insert(a.m_orderOnly.end(), pre.begin(), pre.end());
                    }
                    previous.insert(previous.end(), a.m_outputs.begin(), a.m_outputs.end());
                    graph.m_actions.push_back(std::move(a));
                }
            for (std::size_t i = 0; i < original; ++i)
                if (graph.m_actions[i].m_owner == t.m_name || (t.m_linkType == "modular" && graph.m_actions[i].m_owner.starts_with(t.m_name + "__")))
                    graph.m_actions[i].m_inputs.insert(graph.m_actions[i].m_inputs.end(), pre.begin(), pre.end());
            auto& outputs = graph.m_targetOutputs.at(t.m_name);
            outputs.insert(outputs.end(), pre.begin(), pre.end());
            outputs.insert(outputs.end(), post.begin(), post.end());
            completion[t.m_name] = post.empty() ? pre : post;
        }
        // Header-only/interface targets still carry completion files through the graph.
        // Consumers may include generated headers from any transitive dependency.
        for (const auto index : BuildModel::topologicalTargets(ws))
        {
            const auto& t = ws.m_targets[index];
            std::vector<fs::path> inherited;
            for (const auto* deps : {&t.m_publicDependencies, &t.m_privateDependencies, &t.m_interfaceDependencies})
                for (const auto& d : *deps)
                {
                    const auto& done = completion.at(d);
                    inherited.insert(inherited.end(), done.begin(), done.end());
                }
            std::sort(inherited.begin(), inherited.end());
            inherited.erase(std::unique(inherited.begin(), inherited.end()), inherited.end());
            for (std::size_t i = original; i < graph.m_actions.size(); ++i)
                if (graph.m_actions[i].m_owner == t.m_name)
                    graph.m_actions[i].m_orderOnly.insert(graph.m_actions[i].m_orderOnly.end(), inherited.begin(), inherited.end());
            auto& done = completion.at(t.m_name);
            done.insert(done.end(), inherited.begin(), inherited.end());
            std::sort(done.begin(), done.end());
            done.erase(std::unique(done.begin(), done.end()), done.end());
        }
        for (const auto& [index, name] : hostTools)
        {
            const auto& outputs = graph.m_targetOutputs.at(name);
            auto& action = graph.m_actions[index];
            action.m_inputs.insert(action.m_inputs.end(), outputs.begin(), outputs.end());
        }
        for (std::size_t i = 0; i < original; ++i)
        {
            auto& a = graph.m_actions[i];
            const auto& t = *targets.at(a.m_owner);
            for (const auto* deps : {&t.m_publicDependencies, &t.m_privateDependencies, &t.m_interfaceDependencies})
                for (const auto& d : *deps)
                {
                    const auto& done = completion.at(d);
                    a.m_inputs.insert(a.m_inputs.end(), done.begin(), done.end());
                }
        }
    }
} // namespace axlum
