/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "core/Process.hpp"
#include "graph/Graph.hpp"
#include "model/Model.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ab = axlum;
namespace fs = std::filesystem;

namespace
{
    int s_failures = 0, s_checks = 0;

    void check(bool condition, const std::string& description)
    {
        ++s_checks;
        if (!condition)
        {
            ++s_failures;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    void throws(const std::function<void()>& run, const std::string& contains)
    {
        ++s_checks;
        try
        {
            run();
            ++s_failures;
            std::cerr << "FAIL: expected error containing '" << contains << "'\n";
        }
        catch (const std::exception& e)
        {
            if (std::string(e.what()).find(contains) == std::string::npos)
            {
                ++s_failures;
                std::cerr << "FAIL: expected '" << contains << "', got: " << e.what() << '\n';
            }
        }
    }
} // namespace

int main()
{
    check(ab::BuildModel::parseConfiguration("release") == ab::Configuration::s_release, "parse configuration");
    check(ab::BuildModel::parseTargetKind("static_library") == ab::TargetKind::StaticLibrary, "parse target kind");
    throws(
        []
        {
            (void)ab::BuildModel::parseTargetKind("??");
        },
        "invalid target.kind"
    );
    ab::Workspace ws;
    ws.m_name = "unit";
    ws.m_root = fs::current_path();
    ab::Target a;
    a.m_name = "Alpha";
    ab::Target b;
    b.m_name = "Beta";
    a.m_publicDependencies = {"Beta"};
    b.m_publicDependencies = {"Alpha"};
    ws.m_targets = {a, b};
    throws(
        [&]
        {
            (void)ab::BuildModel::topologicalTargets(ws);
        },
        "cycle"
    );
    ws.m_targets[1].m_publicDependencies.clear();
    check(ab::BuildModel::topologicalTargets(ws).front() == 1, "topological order dependencies first");
    ws.m_targets[0].m_publicDependencies = {"DoesNotExist"};
    throws(
        [&]
        {
            (void)ab::BuildModel::topologicalTargets(ws);
        },
        "unknown dependency"
    );
    ws.m_targets[0].m_publicDependencies = {"Beta", "Beta"};
    throws(
        [&]
        {
            ab::BuildModel::validateWorkspace(ws);
        },
        "more than once"
    );
    ab::BuildGraph graph;
    ab::Action first, second;
    first.m_name = "first";
    first.m_argv = {"false"};
    first.m_outputs = {fs::absolute("same.out")};
    second.m_name = "second";
    second.m_argv = {"true"};
    second.m_outputs = first.m_outputs;
    graph.m_actions = {first, second};
    throws(
        [&]
        {
            graph.validate();
        },
        "duplicate output"
    );
    graph.m_actions[1].m_outputs = {fs::absolute("other.out")};
    graph.m_actions[0].m_inputs = graph.m_actions[1].m_outputs;
    graph.m_actions[1].m_inputs = graph.m_actions[0].m_outputs;
    throws(
        [&]
        {
            graph.validate();
        },
        "cycle"
    );
    check(ab::NinjaSyntax::escapeNinjaPath("x y:a$") == "x$ y$:a$$", "ninja path escaping");
    check(ab::FileSystem::isWithin("build/a/../b", "build"), "path containment positive");
    check(!ab::FileSystem::isWithin("build/../outside", "build"), "path containment negative");
    std::cout << (s_failures ? "FAILED: " : "PASSED: ") << s_checks << " checks\n";
    return s_failures ? 1 : 0;
}
