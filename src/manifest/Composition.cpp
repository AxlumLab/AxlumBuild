/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "core/Process.hpp"
#include "manifest/Manifest.hpp"
#include "packages/PackageManifest.hpp"
#include "toolchain/Toolchain.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace axlum
{
    Workspace ManifestLoader::loadManifest(
        const fs::path& filename,
        Configuration config,
        const fs::path& build,
        const std::string& architecture,
        const std::string& platform
    )
    {
        Workspace result;
        std::map<std::string, std::string> loaded, aliases;
        std::set<std::string> active;
        auto replace = [](std::string text, const std::string& from, const std::string& to)
        {
            for (std::size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
                text.replace(at, from.size(), to);
            return text;
        };
        std::function<std::string(fs::path, std::string)> load = [&](fs::path manifest, std::string prefix)
        {
            auto key = fs::weakly_canonical(manifest).generic_string();
#ifdef _WIN32
            std::transform(
                key.begin(),
                key.end(),
                key.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
#endif
            if (active.contains(key))
                throw std::runtime_error("workspace reference cycle: " + key);
            if (loaded.contains(key))
                return loaded.at(key);
            active.insert(key);
            loaded[key] = prefix;
            auto ws = ManifestLoader::loadSingleManifest(manifest, config, build, architecture, platform);
            if (prefix.empty())
            {
                result.m_ide = ws.m_ide;
                result.m_name = ws.m_name;
                result.m_manifest = ws.m_manifest;
                result.m_root = ws.m_root;
                result.m_configurations = ws.m_configurations;
                result.m_architectures = ws.m_architectures;
                result.m_presets = ws.m_presets;
                result.m_packageManifest = std::make_shared<PackageManifest>();
                if (ws.m_packageManifest)
                    result.m_packageManifest->m_model = ws.m_packageManifest->m_model;
                result.m_packageManifest->m_model.rootPackage.dependencies.clear();
                result.m_packageManifest->m_model.rootPackage.dependencyOverrides.clear();
            }
            std::set<std::string> localAliases;
            for (const auto& ref : ws.m_references)
            {
                if (!localAliases.insert(ref.m_alias).second)
                    throw std::runtime_error("duplicate workspace alias: " + ref.m_alias);
                const auto requested = prefix + ref.m_alias + "__";
                aliases[requested] = load(ref.m_manifest, requested);
            }
            const auto folder = prefix.empty() ? std::string{} : "projects/" + prefix.substr(0, prefix.size() - 2) + "/";
            auto text = [&](std::string value)
            {
                value = replace(value, "${workspace}", ws.m_root.generic_string());
                if (!folder.empty())
                    value = replace(value, "${build}", "${build}/" + folder.substr(0, folder.size() - 1));
                return value;
            };
            auto input = [&](const std::string& value)
            {
                if (value.starts_with("@build/"))
                    return "@build/" + folder + value.substr(7);
                return fs::absolute(ws.m_root / value).lexically_normal().generic_string();
            };
            auto output = [&](std::string value)
            {
                if (value.starts_with("${build}"))
                    return text(value);
                if (value.starts_with("@build/"))
                    return "@build/" + folder + value.substr(7);
                return folder + value;
            };
            auto paths = [&](auto& values)
            {
                for (auto& value : values)
                    value = input(value);
            };
            auto deps = [&](auto& values)
            {
                for (auto& value : values)
                    value = prefix + replace(value, "::", "__");
            };
            auto compile = [&](CompileOptions& c)
            {
                paths(c.m_publicIncludes);
                paths(c.m_privateIncludes);
                if (!c.m_pchHeader.empty())
                    c.m_pchHeader = input(c.m_pchHeader);
                if (!c.m_pchSource.empty())
                    c.m_pchSource = input(c.m_pchSource);
                if (!c.m_pchFile.empty())
                    c.m_pchFile = input(c.m_pchFile);
                if (!c.m_pchObject.empty())
                    c.m_pchObject = input(c.m_pchObject);
            };
            std::set<std::string> locals;
            for (const auto& t : ws.m_targets)
                locals.insert(t.m_name);
            for (auto& t : ws.m_targets)
            {
                const auto original = t.m_name;
                t.m_name = prefix + t.m_name;
                t.m_sourceRoot = ws.m_root;
                if (!prefix.empty())
                {
                    auto inherited = ws.m_ide;
                    for (const auto& [format, custom] : t.m_ide)
                        BuildModel::mergeIDECustomization(inherited[format], custom);
                    t.m_ide = std::move(inherited);
                }
                paths(t.m_sources);
                paths(t.m_ideFiles);
                compile(t.m_compile);
                deps(t.m_publicDependencies);
                deps(t.m_privateDependencies);
                deps(t.m_interfaceDependencies);
                deps(t.m_packages);
                deps(t.m_publicPackages);
                paths(t.m_link.m_libraryDirectories);
                paths(t.m_link.m_frameworkDirectories);
                for (auto& f : t.m_link.m_frameworks)
                    if (f.find_first_of("/\\") != std::string::npos)
                        f = input(f);
                deps(t.m_publicModules);
                deps(t.m_privateModules);
                for (auto& lib : t.m_link.m_libraries)
                    if (lib.find_first_of("/\\") != std::string::npos)
                        lib = input(lib);
                for (auto* file : {&t.m_link.m_moduleDefinition, &t.m_link.m_manifest, &t.m_icon, &t.m_project, &t.m_android.m_manifest})
                    if (!file->empty())
                        *file = input(*file);
                paths(t.m_managed.m_analyzers);
                paths(t.m_managed.m_additionalFiles);
                paths(t.m_managed.m_editorConfigs);
                paths(t.m_managed.m_nativeLibraries);
                for (auto* group : {&t.m_references, &t.m_managed.m_analyzerProjects})
                    for (auto& ref : *group)
                    {
                        if (locals.contains(ref) || ref.find("::") != std::string::npos)
                            ref = prefix + replace(ref, "::", "__");
                        else if (fs::path(ref).has_extension())
                            ref = input(ref);
                    }
                if (!prefix.empty() && !t.m_publish.m_directory.empty())
                    t.m_publish.m_directory = output(t.m_publish.m_directory);
                paths(t.m_runtimeFiles);
                paths(t.m_optionalRuntimeFiles);
                paths(t.m_resourceInputs);
                paths(t.m_inputs);
                paths(t.m_install.m_headers);
                paths(t.m_install.m_files);
                t.m_install.m_headerBase = input(t.m_install.m_headerBase);
                if (t.m_tool.starts_with("@target/"))
                    t.m_tool = "@target/" + prefix + replace(t.m_tool.substr(8), "::", "__");
                else if (t.m_tool.find_first_of("/\\") != std::string::npos)
                    t.m_tool = input(t.m_tool);
                for (auto& arg : t.m_args)
                    arg = text(arg);
                if (!prefix.empty())
                {
                    if (t.m_outputName.empty())
                        t.m_outputName = original;
                    t.m_outputDirectory =
                        output(t.m_outputDirectory.empty()
                                   ? (t.m_kind == TargetKind::StaticLibrary ||
                                              (t.m_kind == TargetKind::SharedLibrary && platform != "windows"
#ifdef _WIN32
                                               && !platform.empty()
#endif
                                                   )
                                          ? "lib"
                                          : "bin")
                                   : t.m_outputDirectory);
                    if (!t.m_documentationDirectory.empty())
                        t.m_documentationDirectory = output(t.m_documentationDirectory);
                    t.m_objectDirectory = output(t.m_objectDirectory.empty() ? "obj/" + original : t.m_objectDirectory);
                    t.m_importLibraryDirectory = output(t.m_importLibraryDirectory.empty() ? "lib" : t.m_importLibraryDirectory);
                    for (auto& file : t.m_outputs)
                        file = output(file);
                    if (!t.m_depfile.empty())
                        t.m_depfile = output(t.m_depfile);
                }
                for (auto* hooks : {&t.m_preBuild, &t.m_postBuild})
                    for (auto& h : *hooks)
                    {
                        if (h.m_tool.starts_with("@target/"))
                            h.m_tool = "@target/" + prefix + replace(h.m_tool.substr(8), "::", "__");
                        else if (h.m_tool.find_first_of("/\\") != std::string::npos)
                            h.m_tool = input(h.m_tool);
                        for (auto& arg : h.m_args)
                            arg = text(arg);
                        for (auto& file : h.m_inputs)
                            file = file.find("${") != std::string::npos ? text(file) : input(file);
                        for (auto& file : h.m_outputs)
                            file = output(file);
                        if (!h.m_depfile.empty())
                            h.m_depfile = output(h.m_depfile);
                    }
                result.m_targets.push_back(std::move(t));
            }
            for (auto& m : ws.m_modules)
            {
                m.m_targetName = prefix + m.m_targetName;
                for (auto* group : {&m.m_publicDependencies, &m.m_privateDependencies})
                    for (auto& d : *group)
                        if (d.find('/') != std::string::npos)
                            d = prefix + replace(d, "::", "__");
                paths(m.m_sources);
                paths(m.m_ideFiles);
                compile(m.m_compile);
                result.m_modules.push_back(std::move(m));
            }
            if (ws.m_packageManifest)
            {
                for (auto d : ws.m_packageManifest->m_model.rootPackage.dependencies)
                {
                    d.name = prefix + d.name;
                    deps(d.dependencies);
                    if (d.local || d.sourceType == "local")
                        d.source = input(d.source);
                    paths(d.patches);
                    if (!d.cmake.toolchainFile.empty())
                        d.cmake.toolchainFile = input(d.cmake.toolchainFile);
                    result.m_packageManifest->m_model.rootPackage.dependencies.push_back(std::move(d));
                }
                for (auto o : ws.m_packageManifest->m_model.rootPackage.dependencyOverrides)
                {
                    o.name = prefix + o.name;
                    o.path = input(o.path);
                    result.m_packageManifest->m_model.rootPackage.dependencyOverrides.push_back(std::move(o));
                }
            }
            active.erase(key);
            return prefix;
        };
        load(filename, "");
        auto canonical = [&](std::string value)
        {
            // Longest namespace wins for nested/diamond aliases.
            for (auto it = aliases.rbegin(); it != aliases.rend(); ++it)
                if (value.starts_with(it->first))
                    return it->second + value.substr(it->first.size());
            return value;
        };
        for (auto& m : result.m_modules)
            for (auto* scope : {&m.m_publicDependencies, &m.m_privateDependencies})
                for (auto& d : *scope)
                    if (d.find('/') != std::string::npos)
                        d = canonical(d);
        for (auto& t : result.m_targets)
        {
            for (auto* scope : {&t.m_publicModules, &t.m_privateModules})
                for (auto& d : *scope)
                    d = canonical(d);
            for (auto* deps : {&t.m_publicDependencies, &t.m_privateDependencies, &t.m_interfaceDependencies})
                for (auto& d : *deps)
                    d = canonical(d);
            for (auto* group : {&t.m_references, &t.m_managed.m_analyzerProjects})
                for (auto& ref : *group)
                {
                    ref = canonical(ref);
                    const auto managedTarget = std::find_if(
                        result.m_targets.begin(),
                        result.m_targets.end(),
                        [&](const auto& other)
                        {
                            return other.m_name == ref;
                        }
                    );
                    if (managedTarget != result.m_targets.end() && managedTarget->m_kind != TargetKind::DotNetProject)
                        throw std::runtime_error("managed reference must name a .NET target: " + ref);
                    if (std::any_of(
                            result.m_targets.begin(),
                            result.m_targets.end(),
                            [&](const auto& other)
                            {
                                return other.m_name == ref;
                            }
                        ) &&
                        std::find(t.m_privateDependencies.begin(), t.m_privateDependencies.end(), ref) == t.m_privateDependencies.end())
                        t.m_privateDependencies.push_back(ref);
                }
            if (t.m_tool.starts_with("@target/"))
                t.m_tool = "@target/" + canonical(t.m_tool.substr(8));
            for (auto* hooks : {&t.m_preBuild, &t.m_postBuild})
                for (auto& h : *hooks)
                    if (h.m_tool.starts_with("@target/"))
                        h.m_tool = "@target/" + canonical(h.m_tool.substr(8));
        }
        for (const auto& [alias, actual] : aliases)
            for (const auto& t : result.m_targets)
                if (t.m_name.starts_with(actual))
                {
                    const auto name = alias + t.m_name.substr(actual.size());
                    if (name == t.m_name)
                        continue;
                    if (std::any_of(
                            result.m_targets.begin(),
                            result.m_targets.end(),
                            [&](const auto& other)
                            {
                                return other.m_name == name;
                            }
                        ))
                        throw std::runtime_error("workspace alias collides with target: " + name);
                    result.m_targetAliases[name] = t.m_name;
                }
        if (config.m_name != "__inspect")
        {
            const auto effectivePlatform = platform.empty() ? (ToolchainDiscovery::hostTriple().find("windows") != std::string::npos ? "windows"
                                                               : ToolchainDiscovery::hostTriple().find("apple") != std::string::npos ? "macos"
                                                                                                                                     : "linux")
                                                            : platform;
            BuildModel::selectTargets(result, config, BuildModel::normalizeArchitecture(architecture), effectivePlatform);
            BuildModel::validateWorkspace(result);
        }
        return result;
    }
} // namespace axlum
