/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/Pch.hpp"
#include "core/Process.hpp"
#include <stdexcept>

namespace axlum
{
    PchPlan PchPlanner::planPch(
        BuildGraph& graph,
        const Toolchain& tc,
        const std::vector<std::string>& commonArguments,
        const fs::path& header,
        const fs::path& source,
        const fs::path& directory,
        const fs::path& workspace,
        const std::vector<fs::path>& orderOnly,
        const std::string& target,
        const std::string& group,
        bool documentation
    )
    {
        PchPlan result;
        Action action;
        action.m_name = "PCH " + target + ":" + group;
        action.m_owner = target;
        action.m_workingDirectory = workspace;
        action.m_orderOnly = orderOnly;
        action.m_rsp = true;
        action.m_gnuRsp = tc.m_kind != CompilerKind::MSVC;
        action.m_inputs = {header, tc.m_cppCompiler};
        action.m_argv = commonArguments;
        if (tc.m_kind == CompilerKind::MSVC)
        {
            const auto pch = directory / "precompiled.pch";
            const auto object = directory / "precompiled.obj";
            auto creator = source;
            if (creator.empty())
            {
                creator = directory / "precompiled.cpp";
                FileSystem::writeIfChanged(creator, "// Generated PCH translation unit.\n#include \"" + header.generic_string() + "\"\n");
            }
            action.m_inputs.push_back(creator);
            action.m_outputs = {pch, object};
            if (documentation)
            {
                const auto doc = directory / "precompiled.xdc";
                action.m_argv.push_back("/doc" + doc.string());
                action.m_outputs.push_back(doc);
                result.m_documents.push_back(doc);
            }
            action.m_argv.insert(
                action.m_argv.end(),
                {"/c",
                 "/Yc" + header.string(),
                 "/FI" + header.string(),
                 "/Fp" + pch.string(),
                 "/Fo" + object.string(),
                 "/sourceDependencies",
                 pch.string() + ".json",
                 creator.string()}
            );
            action.m_depfile = pch.string() + ".d";
            action.m_depsStyle = "msvc-json";
            result.m_flags = {"/Yu" + header.string(), "/FI" + header.string(), "/Fp" + pch.string()};
            result.m_inputs = {pch};
            result.m_objects = {object};
            result.m_compiledSource = creator;
        }
        else
        {
            const auto shim = directory / "precompiled.hpp";
            FileSystem::writeIfChanged(shim, "// Generated PCH include.\n#include \"" + header.generic_string() + "\"\n");
            const auto pch = fs::path(shim.string() + (tc.m_kind == CompilerKind::Clang ? ".pch" : ".gch"));
            action.m_inputs.push_back(shim);
            action.m_outputs = {pch};
            action.m_depfile = pch.string() + ".d";
            action.m_depsStyle = "gcc";
            action.m_argv.insert(
                action.m_argv.end(), {"-x", "c++-header", "-MMD", "-MF", action.m_depfile.string(), "-MT", pch.string(), "-o", pch.string(), shim.string()}
            );
            result.m_flags = tc.m_kind == CompilerKind::Clang ? std::vector<std::string>{"-include-pch", pch.string()}
                                                              : std::vector<std::string>{"-include", shim.string(), "-Werror=invalid-pch"};
            result.m_inputs = {pch};
        }
        graph.m_actions.push_back(std::move(action));
        return result;
    }

    PchPlan PchPlanner::useExternalPch(const Toolchain& tc, const fs::path& header, const fs::path& binary, const fs::path& object)
    {
        PchPlan result;
        result.m_inputs = {header, binary};
        if (tc.m_kind == CompilerKind::MSVC)
        {
            result.m_flags = {"/Yu" + header.string(), "/FI" + header.string(), "/Fp" + binary.string()};
            if (!object.empty())
            {
                result.m_objects.push_back(object);
                result.m_inputs.push_back(object);
            }
        }
        else if (tc.m_kind == CompilerKind::Clang)
            result.m_flags = {"-include-pch", binary.string()};
        else
        {
            if (binary.lexically_normal() != fs::path(header.string() + ".gch").lexically_normal())
                throw std::runtime_error("GCC external pchfile must be adjacent to pchheader as <header>.gch");
            result.m_flags = {"-include", header.string(), "-Werror=invalid-pch"};
        }
        return result;
    }

} // namespace axlum
