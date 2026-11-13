/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "model/Model.hpp"
#include "core/Process.hpp"
#include "managed/NativeProject.hpp"
#include "managed/Project.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace axlum
{
    void BuildModel::mergeIDECustomization(IDECustomization& to, const IDECustomization& from)
    {
        for (const auto& [k, v] : from.m_properties)
            to.m_properties[k] = v;
        for (const auto& [k, v] : from.m_debugger)
            to.m_debugger[k] = v;
        for (auto member :
             {&IDECustomization::m_projectBegin,
              &IDECustomization::m_projectEnd,
              &IDECustomization::m_filtersBegin,
              &IDECustomization::m_filtersEnd,
              &IDECustomization::m_userBegin,
              &IDECustomization::m_userEnd})
            if (!(from.*member).empty())
                to.*member += (to.*member).empty() ? from.*member : "\n" + from.*member;
        if (!from.m_jsonPatch.empty())
        {
            auto patch = to.m_jsonPatch.empty() ? nlohmann::json::object() : nlohmann::json::parse(to.m_jsonPatch);
            patch.merge_patch(nlohmann::json::parse(from.m_jsonPatch));
            to.m_jsonPatch = patch.dump();
        }
    }

    std::string BuildModel::toString(TargetKind kind)
    {
        switch (kind)
        {
        case TargetKind::Executable:
            return "executable";
        case TargetKind::StaticLibrary:
            return "static_library";
        case TargetKind::SharedLibrary:
            return "shared_library";
        case TargetKind::HeaderOnly:
            return "header_only";
        case TargetKind::DotNetProject:
            return "dotnet_project";
        case TargetKind::Codegen:
            return "codegen";
        }
        throw std::runtime_error("unknown TargetKind");
    }

    TargetKind BuildModel::parseTargetKind(std::string_view kind)
    {
        if (kind == "executable")
            return TargetKind::Executable;
        if (kind == "static_library")
            return TargetKind::StaticLibrary;
        if (kind == "shared_library")
            return TargetKind::SharedLibrary;
        if (kind == "header_only")
            return TargetKind::HeaderOnly;
        if (kind == "dotnet_project")
            return TargetKind::DotNetProject;
        if (kind == "codegen")
            return TargetKind::Codegen;
        throw std::runtime_error(
            "invalid target.kind '" + std::string(kind) + "'; expected executable, static_library, shared_library, header_only, dotnet_project or codegen"
        );
    }

    Configuration BuildModel::parseConfiguration(std::string_view value)
    {
        std::string name(value), lower(value);
        std::transform(
            lower.begin(),
            lower.end(),
            lower.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            }
        );
        if (lower == "debug")
            return Configuration::s_debug;
        if (lower == "release")
            return Configuration::s_release;
        if (lower == "development")
            return {"Development"};
        if (name.empty() || !std::isalpha(static_cast<unsigned char>(name.front())) ||
            name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
            throw std::runtime_error("invalid configuration name: " + name);
        return {name};
    }

    const Configuration Configuration::s_debug{"Debug"};
    const Configuration Configuration::s_release{"Release"};

    std::string BuildModel::toString(Configuration config)
    {
        return config.m_name;
    }

    bool BuildModel::isDebug(Configuration config)
    {
        return config == Configuration::s_debug;
    }

    std::string BuildModel::normalizeArchitecture(std::string_view value)
    {
        if (value == "x64" || value == "x86_64")
            return "x64";
        if (value == "arm64" || value == "aarch64" || value == "arm64-v8a")
            return "arm64";
        throw std::runtime_error("unsupported architecture; expected x64 or arm64: " + std::string(value));
    }

    namespace
    {
        void checkName(const std::string& name, const std::string& owner)
        {
            if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
                throw std::runtime_error(owner + " must start with ASCII letter or '_'");
            for (unsigned char c : name)
            {
                if (!(std::isalnum(c) || c == '_' || c == '-'))
                    throw std::runtime_error(owner + " '" + name + "' has invalid character; use [A-Za-z0-9_-]");
            }
        }

        std::string key(const fs::path& path)
        {
            std::string s = fs::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
            std::transform(
                s.begin(),
                s.end(),
                s.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
#endif
            return s;
        }

        fs::path sourcePath(const Workspace& w, const std::string& value)
        {
            if (value.starts_with("@build/"))
                return fs::path("@build") / value.substr(7);
            return fs::absolute(w.m_root / value).lexically_normal();
        }
    } // namespace

    std::vector<std::size_t> BuildModel::topologicalTargets(const Workspace& ws)
    {
        std::unordered_map<std::string, std::size_t> nameToIndex;
        for (std::size_t i = 0; i < ws.m_targets.size(); ++i)
        {
            const auto& t = ws.m_targets[i];
            checkName(t.m_name, "target.m_name");
            if (!nameToIndex.emplace(t.m_name, i).second)
                throw std::runtime_error("duplicate target.m_name '" + t.m_name + "' (line " + std::to_string(t.m_declarationLine) + ")");
        }
        std::vector<std::size_t> order;
        std::vector<int> state(ws.m_targets.size(), 0);
        std::vector<std::string> stack;
        std::function<void(std::size_t)> visit = [&](std::size_t i)
        {
            if (state[i] == 2)
                return;
            if (state[i] == 1)
            {
                std::string chain;
                for (const auto& s : stack)
                    chain += s + " -> ";
                throw std::runtime_error(
                    "target dependency cycle (line " + std::to_string(ws.m_targets[i].m_declarationLine) + "): " + chain + ws.m_targets[i].m_name
                );
            }
            state[i] = 1;
            const auto& t = ws.m_targets[i];
            stack.push_back(t.m_name);
            for (const auto* deps : {&t.m_publicDependencies, &t.m_privateDependencies, &t.m_interfaceDependencies})
            {
                for (const auto& dep : *deps)
                {
                    auto it = nameToIndex.find(dep);
                    if (it == nameToIndex.end())
                        throw std::runtime_error(
                            "target '" + t.m_name + "' (line " + std::to_string(t.m_declarationLine) + ") references unknown dependency '" + dep + "'"
                        );
                    visit(it->second);
                }
            }
            stack.pop_back();
            state[i] = 2;
            order.push_back(i);
        };
        for (std::size_t i = 0; i < ws.m_targets.size(); ++i)
            visit(i);
        return order;
    }

    bool BuildModel::isNativeProject(const Target& target)
    {
        return !target.m_project.empty() &&
               (target.m_kind == TargetKind::Executable || target.m_kind == TargetKind::StaticLibrary || target.m_kind == TargetKind::SharedLibrary);
    }

    std::string BuildModel::targetUnsupportedReason(const Target& t, Configuration config, const std::string& architecture, const std::string& platform)
    {
        if (config.m_name == "__inspect")
            return {};
        auto permitted = [](const auto& values, const auto& value)
        {
            return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
        };
        auto reason = [&](const char* axis, const std::vector<std::string>& allowed)
        {
            std::string list;
            for (const auto& item : allowed)
                list += (list.empty() ? "" : ",") + item;
            return "target '" + t.m_name + "' is unsupported for " + platform + "/" + architecture + "/" + BuildModel::toString(config) + "; " + axis + "=[" +
                   list + "]";
        };
        if (!permitted(t.m_platforms, platform))
            return reason("platforms", t.m_platforms);
        if (!permitted(t.m_architectures, architecture))
            return reason("architectures", t.m_architectures);
        if (!permitted(t.m_configurations, BuildModel::toString(config)))
            return reason("configurations", t.m_configurations);
        if (BuildModel::isNativeProject(t) && platform != "windows")
            return reason("external native project platforms", {"windows"});
        return {};
    }

    void BuildModel::selectTargets(Workspace& ws, Configuration config, const std::string& architecture, const std::string& platform)
    {
        std::set<std::string> names;
        for (const auto& t : ws.m_targets)
        {
            checkName(t.m_name, "target.m_name");
            if (!names.insert(t.m_name).second)
                throw std::runtime_error("duplicate target.m_name: " + t.m_name);
            for (const auto& p : t.m_platforms)
                if (p != "windows" && p != "linux" && p != "macos" && p != "android" && p != "ios" && p != "ios-simulator")
                    throw std::runtime_error("unsupported target platform: " + p);
            for (const auto& a : t.m_architectures)
                (void)BuildModel::normalizeArchitecture(a);
            for (const auto& c : t.m_configurations)
                (void)BuildModel::parseConfiguration(c);
            const auto reason = BuildModel::targetUnsupportedReason(t, config, architecture, platform);
            if (!reason.empty())
                ws.m_disabledTargets[t.m_name] = reason;
        }
        if (config.m_name == "__inspect")
            return;
        for (const auto& m : ws.m_modules)
            if (ws.m_disabledTargets.contains(m.m_targetName))
                ws.m_disabledTargets[m.m_targetName + "__" + (m.m_binaryModuleName.empty() ? m.m_name : m.m_binaryModuleName)] =
                    ws.m_disabledTargets.at(m.m_targetName);
        for (const auto& [alias, name] : ws.m_targetAliases)
            if (ws.m_disabledTargets.contains(name))
                ws.m_disabledTargets[alias] = ws.m_disabledTargets.at(name);
        std::erase_if(
            ws.m_targets,
            [&](const auto& t)
            {
                return ws.m_disabledTargets.contains(t.m_name);
            }
        );
        std::erase_if(
            ws.m_modules,
            [&](const auto& m)
            {
                return ws.m_disabledTargets.contains(m.m_targetName);
            }
        );
        auto require = [&](const std::string& owner, std::string name)
        {
            if (name.starts_with("@target/"))
                name = name.substr(8);
            if (const auto slash = name.find('/'); slash != std::string::npos)
                name.erase(slash);
            if (ws.m_disabledTargets.contains(name))
                throw std::runtime_error("active target '" + owner + "' depends on unavailable " + ws.m_disabledTargets.at(name));
        };
        for (const auto& t : ws.m_targets)
        {
            for (const auto* scope :
                 {&t.m_publicDependencies,
                  &t.m_privateDependencies,
                  &t.m_interfaceDependencies,
                  &t.m_publicModules,
                  &t.m_privateModules,
                  &t.m_references,
                  &t.m_managed.m_analyzerProjects})
                for (const auto& name : *scope)
                    require(t.m_name, name);
            if (t.m_tool.starts_with("@target/"))
                require(t.m_name, t.m_tool);
            for (const auto* hooks : {&t.m_preBuild, &t.m_postBuild})
                for (const auto& hook : *hooks)
                    if (hook.m_tool.starts_with("@target/"))
                        require(t.m_name, hook.m_tool);
        }
        for (const auto& m : ws.m_modules)
            for (const auto* scope : {&m.m_publicDependencies, &m.m_privateDependencies})
                for (const auto& name : *scope)
                    if (name.find('/') != std::string::npos)
                        require(m.m_targetName + "/" + m.m_name, name);
    }

    void BuildModel::validateWorkspace(const Workspace& ws)
    {
        if (ws.m_name.empty())
            throw std::runtime_error("[workspace].name must not be empty");
        if (ws.m_targets.empty() && ws.m_disabledTargets.empty())
            throw std::runtime_error("manifest requires at least one [[target]]");
        const auto order = BuildModel::topologicalTargets(ws);
        (void)order;
        std::unordered_map<std::string, const Target*> targets;
        for (const auto& target : ws.m_targets)
            targets.emplace(target.m_name, &target);
        std::unordered_map<std::string, std::string> sourceOwners;
        std::unordered_set<std::string> moduleNames, sharedCreators;
        auto sourceKey = [&](const std::string& source)
        {
            const auto path = sourcePath(ws, source);
            return path.string().starts_with("@build") ? path.lexically_normal().generic_string() : key(path);
        };
        auto containsSource = [&](const std::vector<std::string>& sources, const std::string& wanted)
        {
            return std::any_of(
                sources.begin(),
                sources.end(),
                [&](const auto& source)
                {
                    return sourceKey(source) == sourceKey(wanted);
                }
            );
        };
        auto checkSources = [&](const std::string& owner, const std::vector<std::string>& sources)
        {
            for (const auto& source : sources)
            {
                if (source.empty())
                    throw std::runtime_error("empty source path for '" + owner + "'");
                fs::path p = sourcePath(ws, source);
                std::string key = sourceKey(source);
                auto [it, inserted] = sourceOwners.emplace(key, owner);
                if (!inserted)
                    throw std::runtime_error("source file '" + source + "' belongs to both '" + it->second + "' and '" + owner + "'");
                if (!source.starts_with("@build/") && !fs::is_regular_file(p))
                    throw std::runtime_error("source file '" + p.string() + "' does not exist (" + owner + ")");
            }
        };
        for (const auto& target : ws.m_targets)
        {
            if (!BuildModel::isNativeProject(target) && (!target.m_nativeProject.m_configurations.empty() || !target.m_nativeProject.m_architectures.empty() ||
                                                         !target.m_nativeProject.m_properties.empty()))
                throw std::runtime_error("external native mappings/properties require a native project: " + target.m_name);
            NativeProjectBuilder::validateNativeProject(ws, target);
            if (!target.m_android.m_applicationId.empty())
            {
                if (target.m_kind != TargetKind::SharedLibrary ||
                    !std::regex_match(target.m_android.m_applicationId, std::regex("[a-zA-Z][a-zA-Z0-9_]*(\\.[a-zA-Z][a-zA-Z0-9_]*)+")))
                    throw std::runtime_error("android_applicationid requires a SharedLibrary and a dotted application ID: " + target.m_name);
                if (target.m_android.m_minSdk > target.m_android.m_targetSdk)
                    throw std::runtime_error("Android min SDK exceeds target SDK");
            }
            std::unordered_set<std::string> deps;
            for (const auto* group : {&target.m_publicDependencies, &target.m_privateDependencies, &target.m_interfaceDependencies})
            {
                for (const auto& dep : *group)
                {
                    if (!deps.insert(dep).second)
                        throw std::runtime_error("target '" + target.m_name + "' lists dependency '" + dep + "' more than once/across scopes");
                }
            }
            if (target.m_kind == TargetKind::DotNetProject)
            {
                ManagedProjectGenerator::validateManagedOptions(target);
                if (target.m_project.empty() && !target.m_framework.empty())
                {
                    checkSources("target " + target.m_name, target.m_sources);
                    continue;
                }
                if (target.m_project.empty())
                    throw std::runtime_error("dotnet_project '" + target.m_name + "' needs project = \"...csproj\"");
                if (!fs::is_regular_file(ws.m_root / target.m_project))
                    throw std::runtime_error("missing .NET project '" + (ws.m_root / target.m_project).string() + "'");
                if (fs::path(target.m_project).extension() != ".csproj")
                    throw std::runtime_error("dotnet_project '" + target.m_name + "' must reference a .csproj file");
            }
            else if (target.m_kind == TargetKind::Codegen)
            {
                if (target.m_tool.empty() || target.m_outputs.empty())
                    throw std::runtime_error("codegen '" + target.m_name + "' needs tool and outputs");
                for (const auto& output : target.m_outputs)
                {
                    if (output.empty() || output == "." || fs::path(output).is_absolute() ||
                        !FileSystem::isWithin(fs::path("build-root") / output, "build-root") ||
                        (fs::path("build-root") / output).lexically_normal() == fs::path("build-root"))
                        throw std::runtime_error("codegen '" + target.m_name + "' output must stay under build directory: " + output);
                }
            }
            else if (target.m_kind == TargetKind::HeaderOnly)
            {
                if (!target.m_sources.empty())
                    throw std::runtime_error("header_only target '" + target.m_name + "' cannot compile sources");
            }
            if (target.m_kind != TargetKind::HeaderOnly && target.m_kind != TargetKind::Codegen && target.m_kind != TargetKind::DotNetProject)
                checkSources("target " + target.m_name, target.m_sources);
            if (!target.m_compile.m_pchSource.empty())
            {
                const auto key = sourceKey(target.m_compile.m_pchSource);
                if (!containsSource(target.m_sources, target.m_compile.m_pchSource) &&
                    !(target.m_compile.m_sharedPch.value_or(false) && sharedCreators.contains(key)))
                    checkSources("target " + target.m_name, {target.m_compile.m_pchSource});
                if (target.m_compile.m_sharedPch.value_or(false))
                    sharedCreators.insert(key);
            }
            if (target.m_kind != TargetKind::Codegen && target.m_kind != TargetKind::DotNetProject && !target.m_tool.empty())
                throw std::runtime_error("tool field only valid for codegen target");
        }
        for (const auto& m : ws.m_modules)
        {
            checkName(m.m_name, "module.name");
            if (!targets.contains(m.m_targetName))
                throw std::runtime_error("module '" + m.m_name + "' references unknown target '" + m.m_targetName + "'");
            const auto& t = *targets.at(m.m_targetName);
            if (BuildModel::isNativeProject(t))
                throw std::runtime_error("external native project owns its compilation graph; DSL modules are not allowed: " + t.m_name);
            if (m.m_name == "root" && (!t.m_sources.empty() || !t.m_compile.m_pchSource.empty()))
                throw std::runtime_error("module name root is reserved for the target's primary sources: " + m.m_targetName);
            if (t.m_kind != TargetKind::StaticLibrary && t.m_kind != TargetKind::SharedLibrary && t.m_kind != TargetKind::Executable)
                throw std::runtime_error("module '" + m.m_name + "' must belong to C/C++ target");
            if (!moduleNames.emplace(m.m_targetName + '/' + m.m_name).second)
                throw std::runtime_error("duplicate module '" + m.m_name + "' in target '" + m.m_targetName + "'");
            checkSources("module " + m.m_targetName + '/' + m.m_name, m.m_sources);
            if (!m.m_compile.m_pchSource.empty())
            {
                const auto key = sourceKey(m.m_compile.m_pchSource);
                const bool shared = m.m_compile.m_sharedPch.value_or(t.m_compile.m_sharedPch.value_or(false));
                if (!containsSource(m.m_sources, m.m_compile.m_pchSource) && !(shared && sharedCreators.contains(key)))
                    checkSources("module " + m.m_targetName + '/' + m.m_name, {m.m_compile.m_pchSource});
                if (shared)
                    sharedCreators.insert(key);
            }
        }
        for (const auto& t : ws.m_targets)
        {
            if (t.m_kind == TargetKind::StaticLibrary || t.m_kind == TargetKind::SharedLibrary || t.m_kind == TargetKind::Executable)
            {
                bool hasSource = BuildModel::isNativeProject(t) || !t.m_sources.empty() || !t.m_compile.m_pchSource.empty();
                for (const auto& module : ws.m_modules)
                    if (module.m_targetName == t.m_name)
                        hasSource |= !module.m_sources.empty() || !module.m_compile.m_pchSource.empty();
                if (!hasSource)
                    throw std::runtime_error("native target '" + t.m_name + "' has no source files");
            }
        }
    }
} // namespace axlum
