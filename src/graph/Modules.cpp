/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/Modules.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        void append(std::vector<std::string>& to, const std::vector<std::string>& from)
        {
            for (const auto& s : from)
                if (std::find(to.begin(), to.end(), s) == to.end())
                    to.push_back(s);
        }

        using ModuleEdges = std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>>;

        Workspace resolveModuleReferences(const Workspace& source, const Toolchain& tc, ModuleEdges& edges)
        {
            Workspace input = source;
            std::map<std::string, const Target*> owners;
            for (const auto& target : source.m_targets)
                owners[target.m_name] = &target;
            std::map<std::string, const Module*> modules;
            for (const auto& module : source.m_modules)
                modules[module.m_targetName + "/" + module.m_name] = &module;
            auto keyOf = [&](const Module& m, const std::string& dep)
            {
                return dep.find('/') == std::string::npos ? m.m_targetName + "/" + dep : dep;
            };
            std::map<std::string, int> state;
            std::map<std::string, CompileOptions> interfaces;
            std::vector<std::string> stack;
            std::function<void(const std::string&)> visit = [&](const std::string& key)
            {
                if (!modules.contains(key))
                    throw std::runtime_error("unknown module dependency: " + key);
                if (state[key] == 2)
                    return;
                if (state[key] == 1)
                {
                    std::string chain;
                    for (const auto& node : stack)
                        chain += node + " -> ";
                    throw std::runtime_error("module dependency cycle: " + chain + key);
                }
                state[key] = 1;
                stack.push_back(key);
                const auto& m = *modules.at(key);
                const auto& owner = *owners.at(m.m_targetName);
                auto iface = m.m_compile;
                append(iface.m_publicIncludes, owner.m_compile.m_publicIncludes);
                append(iface.m_publicDefines, owner.m_compile.m_publicDefines);
                for (const auto* scope : {&m.m_publicDependencies, &m.m_privateDependencies})
                    for (const auto& dep : *scope)
                    {
                        const auto other = keyOf(m, dep);
                        visit(other);
                        if (scope == &m.m_publicDependencies)
                        {
                            append(iface.m_publicIncludes, interfaces.at(other).m_publicIncludes);
                            append(iface.m_publicDefines, interfaces.at(other).m_publicDefines);
                        }
                    }
                if (!m.m_apiDefine.empty())
                    iface.m_publicDefines.push_back(
                        m.m_apiDefine + "=" +
                        ((owner.m_linkType == "modular" || owner.m_kind == TargetKind::SharedLibrary) && tc.m_kind == CompilerKind::MSVC
                             ? "__declspec(dllimport)"
                             : "")
                    );
                interfaces[key] = std::move(iface);
                stack.pop_back();
                state[key] = 2;
            };
            for (const auto& [key, m] : modules)
            {
                (void)m;
                visit(key);
            }
            auto binary = [&](const std::string& key)
            {
                const auto& m = *modules.at(key);
                const auto& owner = *owners.at(m.m_targetName);
                if (owner.m_kind != TargetKind::StaticLibrary && owner.m_kind != TargetKind::SharedLibrary && owner.m_linkType != "modular")
                    throw std::runtime_error("referenced module must belong to a library or Modular target: " + key);
                return owner.m_linkType == "modular" ? owner.m_name + "__" + (m.m_binaryModuleName.empty() ? m.m_name : m.m_binaryModuleName) : owner.m_name;
            };
            auto use = [&](CompileOptions& compile, const std::string& from, const std::string& key, bool publicly)
            {
                visit(key);
                const auto& iface = interfaces.at(key);
                append(compile.m_privateIncludes, iface.m_publicIncludes);
                append(compile.m_privateDefines, iface.m_publicDefines);
                if (publicly)
                {
                    append(compile.m_publicIncludes, iface.m_publicIncludes);
                    append(compile.m_publicDefines, iface.m_publicDefines);
                }
                const auto dep = binary(key);
                if (from == dep)
                    throw std::runtime_error("module reference resolves to its own binary: " + key);
                append(publicly ? edges[from].first : edges[from].second, {dep});
            };
            for (auto& m : input.m_modules)
            {
                const auto& owner = *owners.at(m.m_targetName);
                const auto from =
                    owner.m_linkType == "modular" ? owner.m_name + "__" + (m.m_binaryModuleName.empty() ? m.m_name : m.m_binaryModuleName) : owner.m_name;
                for (auto* scope : {&m.m_publicDependencies, &m.m_privateDependencies})
                {
                    for (const auto& dep : *scope)
                        if (dep.find('/') != std::string::npos)
                            use(m.m_compile, from, dep, scope == &m.m_publicDependencies);
                    std::erase_if(
                        *scope,
                        [](const auto& d)
                        {
                            return d.find('/') != std::string::npos;
                        }
                    );
                }
            }
            for (auto& target : input.m_targets)
                for (const auto* scope : {&target.m_publicModules, &target.m_privateModules})
                    for (const auto& key : *scope)
                        use(target.m_compile, target.m_name, key, scope == &target.m_publicModules);
            return input;
        }
    } // namespace

    Workspace ModulePlanner::planModules(const Workspace& source, const Toolchain& tc)
    {
        BuildModel::validateWorkspace(source);
        ModuleEdges moduleEdges;
        const auto input = resolveModuleReferences(source, tc, moduleEdges);
        BuildModel::validateWorkspace(input);
        Workspace ws = input;
        ws.m_modules.clear();
        for (std::size_t ti = 0; ti < input.m_targets.size(); ++ti)
        {
            const auto& owner = input.m_targets[ti];
            const bool modular = owner.m_linkType == "modular";
            const bool shared = modular || owner.m_kind == TargetKind::SharedLibrary;
            if (owner.m_linkType != "monolithic" && !modular)
                throw std::runtime_error("invalid linktype: " + owner.m_linkType);
            if (modular && owner.m_kind != TargetKind::Executable && owner.m_kind != TargetKind::SharedLibrary)
                throw std::runtime_error("Modular requires an executable or shared-library target: " + owner.m_name);
            std::map<std::string, Module> modules;
            for (const auto& m : input.m_modules)
                if (m.m_targetName == owner.m_name)
                    modules.emplace(m.m_name, m);
            for (const auto& [name, m] : modules)
            {
                std::set<std::string> dependencies;
                for (const auto* scope : {&m.m_publicDependencies, &m.m_privateDependencies})
                    for (const auto& d : *scope)
                        if (!dependencies.insert(d).second)
                            throw std::runtime_error("duplicate module dependency across scopes: " + owner.m_name + "/" + name + " -> " + d);
            }
            std::map<std::string, int> state;
            std::map<std::string, CompileOptions> interfaces;
            std::function<void(const std::string&)> visit = [&](const std::string& name)
            {
                if (!modules.contains(name))
                    throw std::runtime_error("unknown module dependency: " + owner.m_name + "/" + name);
                if (state[name] == 2)
                    return;
                if (state[name] == 1)
                    throw std::runtime_error("module dependency cycle: " + owner.m_name + "/" + name);
                state[name] = 1;
                auto& m = modules.at(name);
                auto iface = m.m_compile;
                for (const auto* deps : {&m.m_publicDependencies, &m.m_privateDependencies})
                    for (const auto& d : *deps)
                    {
                        visit(d);
                        append(m.m_compile.m_privateIncludes, interfaces.at(d).m_publicIncludes);
                        append(m.m_compile.m_privateDefines, interfaces.at(d).m_publicDefines);
                        if (deps == &m.m_publicDependencies)
                        {
                            append(iface.m_publicIncludes, interfaces.at(d).m_publicIncludes);
                            append(iface.m_publicDefines, interfaces.at(d).m_publicDefines);
                        }
                    }
                if (!m.m_apiDefine.empty())
                {
                    const auto exportValue =
                        shared ? (tc.m_kind == CompilerKind::MSVC ? "__declspec(dllexport)" : "__attribute__((visibility(\"default\")))") : "";
                    const auto importValue = shared && tc.m_kind == CompilerKind::MSVC ? "__declspec(dllimport)" : "";
                    m.m_compile.m_privateDefines.push_back(m.m_apiDefine + "=" + exportValue);
                    iface.m_publicDefines.push_back(m.m_apiDefine + "=" + importValue);
                    if (!modular)
                        ws.m_targets[ti].m_compile.m_privateDefines.push_back(m.m_apiDefine + "=" + exportValue);
                }
                interfaces[name] = std::move(iface);
                state[name] = 2;
            };
            for (const auto& [name, m] : modules)
                visit(name);
            if (!modular)
                for (const auto& [name, iface] : interfaces)
                {
                    append(ws.m_targets[ti].m_compile.m_publicIncludes, iface.m_publicIncludes);
                    append(ws.m_targets[ti].m_compile.m_publicDefines, iface.m_publicDefines);
                }
            std::map<std::string, std::string> groups;
            for (const auto& [name, m] : modules)
                groups[name] = modular ? (m.m_binaryModuleName.empty() ? name : m.m_binaryModuleName) : owner.m_name;
            std::map<std::string, std::string> apiGroups;
            for (const auto& [name, m] : modules)
                if (!m.m_apiDefine.empty())
                {
                    const auto [it, inserted] = apiGroups.emplace(m.m_apiDefine, groups.at(name));
                    if (!inserted && it->second != groups.at(name))
                        throw std::runtime_error("API macro is shared by different binaries: " + m.m_apiDefine);
                }
            std::map<std::string, std::size_t> binaries;
            if (modular)
                for (const auto& [name, m] : modules)
                {
                    const auto& group = groups.at(name);
                    if (!binaries.contains(group))
                    {
                        Target binary;
                        binary.m_name = owner.m_name + "__" + group;
                        binary.m_outputName = group;
                        binary.m_kind = TargetKind::SharedLibrary;
                        binary.m_sourceRoot = owner.m_sourceRoot;
                        binary.m_compile = owner.m_compile;
                        binary.m_compile.m_pchSource.clear();
                        binary.m_outputDirectory = owner.m_outputDirectory;
                        binary.m_documentationDirectory = owner.m_documentationDirectory;
                        binary.m_ide = owner.m_ide;
                        binary.m_importLibraryDirectory = owner.m_importLibraryDirectory;
                        binary.m_publicDependencies = owner.m_publicDependencies;
                        binary.m_privateDependencies = owner.m_privateDependencies;
                        binary.m_interfaceDependencies = owner.m_interfaceDependencies;
                        binary.m_link = owner.m_link;
                        binary.m_link.m_moduleDefinition.clear();
                        binary.m_link.m_manifest.clear();
                        binary.m_link.m_subsystem.clear();
                        binary.m_link.m_entryPoint.clear();
                        binary.m_link.m_exports.clear();
                        binary.m_link.m_importLibrary = true;
                        binaries[group] = ws.m_targets.size();
                        ws.m_targets.push_back(std::move(binary));
                        ws.m_targets[ti].m_publicDependencies.push_back(ws.m_targets.back().m_name);
                    }
                }
            for (auto& [name, m] : modules)
            {
                if (!modular && shared)
                    for (const auto& [peerName, peer] : modules)
                        if (!peer.m_apiDefine.empty())
                        {
                            const auto prefix = peer.m_apiDefine + "=";
                            std::erase_if(
                                m.m_compile.m_privateDefines,
                                [&](const auto& d)
                                {
                                    return d.starts_with(prefix);
                                }
                            );
                            m.m_compile.m_privateDefines.push_back(
                                prefix + (tc.m_kind == CompilerKind::MSVC ? "__declspec(dllexport)" : "__attribute__((visibility(\"default\")))")
                            );
                        }
                if (modular)
                {
                    auto& binary = ws.m_targets[binaries.at(groups.at(name))];
                    m.m_targetName = binary.m_name;
                    append(binary.m_compile.m_publicIncludes, interfaces.at(name).m_publicIncludes);
                    // APIs of all modules in one DLL must be exports inside that DLL.
                    for (const auto& [peerName, peer] : modules)
                        if (groups.at(peerName) == groups.at(name) && !peer.m_apiDefine.empty())
                        {
                            const auto prefix = peer.m_apiDefine + "=";
                            std::erase_if(
                                m.m_compile.m_privateDefines,
                                [&](const auto& d)
                                {
                                    return d.starts_with(prefix);
                                }
                            );
                            m.m_compile.m_privateDefines.push_back(
                                prefix + (tc.m_kind == CompilerKind::MSVC ? "__declspec(dllexport)" : "__attribute__((visibility(\"default\")))")
                            );
                        }
                    append(binary.m_compile.m_publicDefines, interfaces.at(name).m_publicDefines);
                    for (const auto* deps : {&m.m_publicDependencies, &m.m_privateDependencies})
                        for (const auto& d : *deps)
                        {
                            if (groups.at(d) == groups.at(name))
                                continue;
                            const auto dependency = ws.m_targets[binaries.at(groups.at(d))].m_name;
                            auto& scope = deps == &m.m_publicDependencies ? binary.m_publicDependencies : binary.m_privateDependencies;
                            append(scope, {dependency});
                        }
                }
                m.m_publicDependencies.clear();
                m.m_privateDependencies.clear();
                ws.m_modules.push_back(m);
            }
            for (const auto& [group, index] : binaries)
            {
                auto& binary = ws.m_targets[index];
                std::erase_if(
                    binary.m_privateDependencies,
                    [&](const auto& d)
                    {
                        return std::find(binary.m_publicDependencies.begin(), binary.m_publicDependencies.end(), d) != binary.m_publicDependencies.end();
                    }
                );
            }
        }
        for (auto& target : ws.m_targets)
            if (moduleEdges.contains(target.m_name))
            {
                append(target.m_publicDependencies, moduleEdges.at(target.m_name).first);
                append(target.m_privateDependencies, moduleEdges.at(target.m_name).second);
                std::erase_if(
                    target.m_privateDependencies,
                    [&](const auto& dep)
                    {
                        return std::find(target.m_publicDependencies.begin(), target.m_publicDependencies.end(), dep) != target.m_publicDependencies.end();
                    }
                );
            }
        // A Modular target made entirely of modules is an aggregate, not an empty DLL.
        for (auto& target : ws.m_targets)
            if (target.m_linkType == "modular" && target.m_sources.empty() && target.m_compile.m_pchSource.empty())
            {
                target.m_kind = TargetKind::HeaderOnly;
                target.m_linkType = "monolithic";
            }
        // Also detects cycles introduced by merging otherwise acyclic modules into DLLs.
        BuildModel::validateWorkspace(ws);
        return ws;
    }
} // namespace axlum
