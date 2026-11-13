/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/Graph.hpp"
#include "core/Process.hpp"
#include "graph/Hooks.hpp"
#include "graph/Modules.hpp"
#include "graph/NativeOptions.hpp"
#include "graph/NativePaths.hpp"
#include "graph/Pch.hpp"
#include "graph/Resources.hpp"
#include "graph/Runtime.hpp"
#include "managed/NativeProject.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace axlum
{
    namespace
    {
        std::string interfaceKey(const fs::path& p)
        {
            std::string s = fs::absolute(p).lexically_normal().generic_string();
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

        void addUnique(std::vector<std::string>& v, const std::string& value)
        {
            if (std::find(v.begin(), v.end(), value) == v.end())
                v.push_back(value);
        }

        void addUnique(std::vector<fs::path>& v, const fs::path& value)
        {
            if (std::find(v.begin(), v.end(), value) == v.end())
                v.push_back(value);
        }

        fs::path resolve(const Workspace& ws, const fs::path& build, const std::string& value)
        {
            return NativePaths::resolveBuildInput(ws, build, value);
        }

        std::vector<std::string> both(const std::vector<std::string>& a, const std::vector<std::string>& b)
        {
            std::vector<std::string> v = a;
            for (const auto& s : b)
                addUnique(v, s);
            return v;
        }

        struct Interface
        {
            std::vector<std::string> m_includes;
            std::vector<std::string> m_defines, m_frameworkDirectories;
        };

        void merge(Interface& lhs, const Interface& rhs)
        {
            for (const auto& v : rhs.m_includes)
                addUnique(lhs.m_includes, v);
            for (const auto& v : rhs.m_defines)
                addUnique(lhs.m_defines, v);
            for (const auto& v : rhs.m_frameworkDirectories)
                addUnique(lhs.m_frameworkDirectories, v);
        }

        void mergeFrameworks(Interface& iface, const Target& t)
        {
            for (const auto& directory : t.m_link.m_frameworkDirectories)
                addUnique(iface.m_frameworkDirectories, directory);
            for (const auto& framework : t.m_link.m_frameworks)
            {
                const fs::path path(framework);
                if (path.has_parent_path())
                    addUnique(iface.m_frameworkDirectories, path.parent_path().string());
            }
        }

        void mergeLocal(Interface& iface, const CompileOptions& c, bool privateToo)
        {
            for (const auto& s : c.m_publicIncludes)
                addUnique(iface.m_includes, s);
            for (const auto& s : c.m_publicDefines)
                addUnique(iface.m_defines, s);
            if (privateToo)
            {
                for (const auto& s : c.m_privateIncludes)
                    addUnique(iface.m_includes, s);
                for (const auto& s : c.m_privateDefines)
                {
                    const auto name = s.substr(0, s.find('='));
                    std::erase_if(
                        iface.m_defines,
                        [&](const auto& old)
                        {
                            return old.substr(0, old.find('=')) == name;
                        }
                    );
                    addUnique(iface.m_defines, s);
                }
            }
        }
    } // namespace

    void BuildGraph::validate() const
    {
        std::unordered_map<std::string, std::size_t> outputToAction;
        std::unordered_set<std::string> reserved;
        for (std::size_t i = 0; i < m_actions.size(); ++i)
        {
            const auto& action = m_actions[i];
            if (action.m_argv.empty())
                throw std::runtime_error("action '" + action.m_name + "' has no executable");
            if (action.m_outputs.empty())
                throw std::runtime_error("action '" + action.m_name + "' has no declared output");
            for (const auto& file : action.m_outputs)
            {
                const auto key = interfaceKey(file);
                auto [it, inserted] = outputToAction.emplace(key, i);
                if (!inserted)
                    throw std::runtime_error(
                        "duplicate output '" + file.string() + "' from actions '" + m_actions[it->second].m_name + "' and '" + action.m_name + "'"
                    );
            }
            if (!action.m_depfile.empty() && !reserved.insert(interfaceKey(action.m_depfile)).second)
                throw std::runtime_error("duplicate depfile '" + action.m_depfile.string() + "'");
        }
        for (const auto& action : m_actions)
        {
            if (!action.m_depfile.empty() && outputToAction.contains(interfaceKey(action.m_depfile)))
                throw std::runtime_error("depfile overlaps an output: " + action.m_depfile.string());
            for (const auto& file : action.m_inputs)
            {
                if (!outputToAction.contains(interfaceKey(file)) && !fs::is_regular_file(file))
                    throw std::runtime_error("action '" + action.m_name + "' has missing input '" + file.string() + "'");
            }
            for (const auto& file : action.m_orderOnly)
            {
                if (!outputToAction.contains(interfaceKey(file)) && !fs::exists(file))
                    throw std::runtime_error("action '" + action.m_name + "' has missing order-only dependency '" + file.string() + "'");
            }
        }
        std::vector<int> state(m_actions.size(), 0);
        std::function<void(std::size_t)> visit = [&](std::size_t i)
        {
            if (state[i] == 2)
                return;
            if (state[i] == 1)
                throw std::runtime_error("build action cycle involving '" + m_actions[i].m_name + "'");
            state[i] = 1;
            const auto& act = m_actions[i];
            for (const auto* files : {&act.m_inputs, &act.m_orderOnly})
            {
                for (const auto& f : *files)
                {
                    auto it = outputToAction.find(interfaceKey(f));
                    if (it != outputToAction.end())
                        visit(it->second);
                }
            }
            state[i] = 2;
        };
        for (std::size_t i = 0; i < m_actions.size(); ++i)
            visit(i);
    }

    BuildGraph BuildPlanner::planBuild(const Workspace& workspace, const Toolchain& tc, const fs::path& buildDir, Configuration cfg, const fs::path& self)
    {
        auto selected = workspace;
        const auto platform = tc.m_platform.empty() ? (ToolchainDiscovery::hostTriple().find("windows") != std::string::npos ? "windows"
                                                       : ToolchainDiscovery::hostTriple().find("apple") != std::string::npos ? "macos"
                                                                                                                             : "linux")
                                                    : tc.m_platform;
        BuildModel::selectTargets(selected, cfg, tc.m_architecture.empty() ? "x64" : tc.m_architecture, platform);
        const auto ws = ModulePlanner::planModules(selected, tc);
        BuildModel::validateWorkspace(ws);
        if (!ws.m_configurations.empty() && std::find(ws.m_configurations.begin(), ws.m_configurations.end(), cfg) == ws.m_configurations.end())
            throw std::runtime_error("configuration " + BuildModel::toString(cfg) + " is not declared by the workspace");
        const fs::path outputRoot = fs::absolute(buildDir).lexically_normal();
        const auto topo = BuildModel::topologicalTargets(ws);
        std::map<std::string, PchPlan> sharedPchs;
        std::map<std::string, std::string> manualPchKeys;
        std::unordered_map<std::string, const Target*> targets;
        for (const auto& t : ws.m_targets)
            targets[t.m_name] = &t;
        std::unordered_map<std::string, Interface> exported;
        BuildGraph graph;
        graph.m_environment = tc.m_environment;
        graph.m_disabledTargets = ws.m_disabledTargets;
        for (const auto& t : ws.m_targets)
            graph.m_targetDependencies[t.m_name] = both(both(t.m_publicDependencies, t.m_privateDependencies), t.m_interfaceDependencies);
        for (const auto& [alias, target] : ws.m_targetAliases)
            if (!ws.m_disabledTargets.contains(alias) && !ws.m_disabledTargets.contains(target))
                graph.m_targetAliases[alias] = target;

        // Predeclare every artifact, so forward references and host tools resolve consistently.
        for (const auto& target : ws.m_targets)
        {
            std::vector<fs::path> outputs;
            if (target.m_kind == TargetKind::Codegen)
            {
                for (const auto& f : target.m_outputs)
                    outputs.push_back((outputRoot / f).lexically_normal());
            }
            else if (target.m_kind == TargetKind::DotNetProject)
            {
                outputs.push_back(outputRoot / "dotnet" / target.m_name / "built.stamp");
            }
            else if (target.m_kind != TargetKind::HeaderOnly)
            {
                NativeOptions::validateNativeOptions(target, tc);
                outputs.push_back(NativePaths::targetArtifact(target, tc, ws, outputRoot, cfg));
                if (tc.m_kind == CompilerKind::MSVC && target.m_kind == TargetKind::SharedLibrary && target.m_link.m_importLibrary)
                {
                    outputs.push_back(NativePaths::targetImportLibrary(target, ws, outputRoot, cfg));
                    auto exp = outputs.back();
                    exp.replace_extension(".exp");
                    outputs.push_back(exp);
                }
                if (tc.m_kind == CompilerKind::MSVC && target.m_kind != TargetKind::StaticLibrary)
                {
                    if (NativeOptions::symbolsEnabled(target.m_compile, cfg))
                    {
                        auto pdb = outputs.front();
                        pdb.replace_extension(".pdb");
                        outputs.push_back(pdb);
                    }
                    if (target.m_link.m_incremental.value_or(false))
                    {
                        auto ilk = outputs.front();
                        ilk.replace_extension(".ilk");
                        outputs.push_back(ilk);
                    }
                }
            }
            graph.m_targetOutputs[target.m_name] = std::move(outputs);
            graph.m_targetKinds[target.m_name] = target.m_kind;
        }

        for (const auto i : topo)
        {
            const auto& t = ws.m_targets[i];
            Interface iface;
            mergeLocal(iface, t.m_compile, false);
            mergeFrameworks(iface, t);
            for (const auto& m : ws.m_modules)
                if (m.m_targetName == t.m_name)
                    mergeLocal(iface, m.m_compile, false);
            for (const auto& d : both(t.m_publicDependencies, t.m_interfaceDependencies))
                merge(iface, exported.at(d));
            exported[t.m_name] = std::move(iface);
        }
        const auto directDependencies = [&](const Target& target)
        {
            return both(target.m_publicDependencies, target.m_privateDependencies);
        };
        auto orderOnlyFor = [&](const Target& target)
        {
            std::vector<fs::path> order;
            for (const auto& d : directDependencies(target))
            {
                const auto kind = targets.at(d)->m_kind;
                if (kind == TargetKind::Codegen || kind == TargetKind::DotNetProject ||
                    (target.m_kind == TargetKind::DotNetProject && kind != TargetKind::HeaderOnly))
                {
                    for (const auto& p : graph.m_targetOutputs.at(d))
                        addUnique(order, p);
                }
            }
            return order;
        };

        for (const auto i : topo)
        {
            const auto& t = ws.m_targets[i];
            if (t.m_kind == TargetKind::HeaderOnly)
                continue;
            if (t.m_kind == TargetKind::Codegen)
            {
                Action action;
                action.m_name = "Generate " + t.m_name;
                action.m_owner = t.m_name;
                action.m_workingDirectory = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
                action.m_outputs = graph.m_targetOutputs.at(t.m_name);
                action.m_orderOnly = orderOnlyFor(t);
                std::string executable = t.m_tool;
                if (executable.starts_with("@target/"))
                {
                    if (tc.m_host != tc.m_target)
                        throw std::runtime_error("cross compilation requires an external host codegen tool: " + executable);
                    std::string toolTarget = executable.substr(8);
                    if (!targets.contains(toolTarget) || targets.at(toolTarget)->m_kind != TargetKind::Executable)
                        throw std::runtime_error("codegen '" + t.m_name + "' tool requires host executable target: " + executable);
                    const auto deps = directDependencies(t);
                    if (std::find(deps.begin(), deps.end(), toolTarget) == deps.end())
                        throw std::runtime_error("codegen '" + t.m_name + "' must depend on tool target '" + toolTarget + "'");
                    executable = graph.m_targetOutputs.at(toolTarget).at(0).string();
                    action.m_inputs.push_back(executable);
                }
                else if (executable.find('/') != std::string::npos || executable.find('\\') != std::string::npos)
                {
                    executable = resolve(ws, outputRoot, executable).string();
                    action.m_inputs.push_back(executable);
                }
                else
                {
                    const auto installed = Process::findExecutable(executable);
                    if (installed.empty())
                        throw std::runtime_error("codegen '" + t.m_name + "' tool not installed/on PATH: " + executable);
                    executable = installed;
                    action.m_inputs.push_back(executable);
                }
                action.m_argv.push_back(executable);
                for (const auto& arg : t.m_args)
                    action.m_argv.push_back(NativePaths::expandBuildValue(arg, ws, outputRoot, cfg, t.m_name));
                for (const auto& input : t.m_inputs)
                    action.m_inputs.push_back(resolve(ws, outputRoot, input));
                if (!t.m_depfile.empty())
                {
                    action.m_depfile = outputRoot / t.m_depfile;
                    if (!FileSystem::isWithin(action.m_depfile, outputRoot))
                        throw std::runtime_error("codegen depfile escapes build directory: " + t.m_depfile);
                    action.m_depsStyle = "gcc";
                }
                graph.m_actions.push_back(std::move(action));
                continue;
            }
            if (t.m_kind == TargetKind::DotNetProject)
            {
                Action action;
                action.m_name = "dotnet build " + t.m_name;
                action.m_owner = t.m_name;
                action.m_workingDirectory = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
                action.m_inputs = {fs::absolute(ws.m_root / t.m_project).lexically_normal()};
                action.m_orderOnly = orderOnlyFor(t);
                action.m_outputs = graph.m_targetOutputs.at(t.m_name);
                for (const auto* files :
                     {&t.m_managed.m_analyzers, &t.m_managed.m_additionalFiles, &t.m_managed.m_editorConfigs, &t.m_managed.m_nativeLibraries})
                    for (const auto& file : *files)
                        action.m_inputs.push_back(resolve(ws, outputRoot, file));
                action.m_always = true; // MSBuild tracks its own source files, including newly added ones.
                action.m_restat = true; // downstream compilation need not run when the stamp remains unchanged.
                auto artifactsDir = outputRoot / "dotnet" / t.m_name / "artifacts";
                action.m_argv = {
                    self.string(),
                    "__dotnet-build",
                    "--project",
                    action.m_inputs.front().string(),
                    "--config",
                    BuildModel::toString(cfg),
                    "--cwd",
                    (t.m_generatedProject ? (t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot) : fs::path(t.m_project).parent_path()).string(),
                    "--artifacts",
                    artifactsDir.string(),
                    "--stamp",
                    action.m_outputs.front().string()
                };
                graph.m_actions.push_back(std::move(action));
                continue;
            }
            Interface own;
            mergeLocal(own, t.m_compile, true);
            mergeFrameworks(own, t);
            for (const auto& d : directDependencies(t))
                merge(own, exported.at(d));
            if (BuildModel::isNativeProject(t))
            {
                NativeProjectBuilder::planNativeProject(graph, ws, tc, outputRoot, cfg, self, t, own.m_includes, own.m_defines);
                continue;
            }
            std::vector<fs::path> objects, documentation;
            bool wantsDocumentation = t.m_compile.m_documentation.value_or(false);
            std::vector<Module> modules;
            if (!t.m_sources.empty() || !t.m_compile.m_pchSource.empty())
            {
                Module primary;
                primary.m_name = "root";
                primary.m_sources = t.m_sources;
                primary.m_compile.m_cppStandard = t.m_compile.m_cppStandard;
                primary.m_compile.m_cStandard = t.m_compile.m_cStandard;
                modules.push_back(std::move(primary));
            }
            for (const auto& m : ws.m_modules)
                if (m.m_targetName == t.m_name)
                    modules.push_back(m);
            if (!t.m_icon.empty())
            {
                if (tc.m_kind != CompilerKind::MSVC)
                    throw std::runtime_error("win32icon requires MSVC: " + t.m_name);
                auto icon = resolve(ws, outputRoot, t.m_icon).generic_string();
                if (icon.find('"') != std::string::npos)
                    throw std::runtime_error("invalid icon path: " + icon);
                const auto rc = outputRoot / "generated" / t.m_name / "axlum-icon.rc";
                FileSystem::writeIfChanged(rc, "1 ICON \"" + icon + "\"\n");
                Module iconModule;
                iconModule.m_name = "_icon";
                iconModule.m_sources = {rc.string()};
                modules.push_back(std::move(iconModule));
            }
            for (const auto& module : modules)
            {
                Interface local = own;
                for (const auto& define : module.m_compile.m_privateDefines)
                {
                    const auto eq = define.find('=');
                    const auto name = define.substr(0, eq);
                    std::erase_if(
                        local.m_defines,
                        [&](const auto& d)
                        {
                            return d.substr(0, d.find('=')) == name;
                        }
                    );
                }
                mergeLocal(local, module.m_compile, true);
                auto options = NativeOptions::effectiveCompile(t.m_compile, module.m_compile);
                wantsDocumentation |= options.m_documentation.value_or(false);
                // A target's explicit creator is compiled once in its primary group; other modules use automatic
                // creators.
                if (module.m_name != "root" && module.m_compile.m_pchSource.empty())
                    options.m_pchSource.clear();
                auto validationTarget = t;
                validationTarget.m_compile = options;
                NativeOptions::validateNativeOptions(validationTarget, tc);
                if (tc.m_kind == CompilerKind::MSVC &&
                    (NativeOptions::runtimePolicy(options) != NativeOptions::runtimePolicy(t.m_compile) ||
                     options.m_debugCRT.value_or(BuildModel::isDebug(cfg)) != t.m_compile.m_debugCRT.value_or(BuildModel::isDebug(cfg))))
                    throw std::runtime_error("module runtime must match its target: " + t.m_name + "/" + module.m_name);
                if (options.m_lto.value_or(false) && !t.m_compile.m_lto.value_or(false))
                    throw std::runtime_error("module LTO requires LTO on its target: " + t.m_name + "/" + module.m_name);
                const auto objectDirectory = NativePaths::targetObjects(t, ws, outputRoot, cfg) / module.m_name;
                auto arguments = [&](bool cpp)
                {
                    std::vector<std::string> args = {cpp ? tc.m_cppCompiler : tc.m_cCompiler};
                    const auto flags = NativeOptions::nativeCompileFlags(options, tc, cfg, cpp, t.m_kind == TargetKind::SharedLibrary);
                    args.insert(args.end(), flags.begin(), flags.end());
                    for (const auto& directory : local.m_frameworkDirectories)
                        args.push_back("-F" + resolve(ws, outputRoot, directory).string());
                    for (const auto& include : local.m_includes)
                        args.push_back((tc.m_kind == CompilerKind::MSVC ? "/I" : "-I") + resolve(ws, outputRoot, include).string());
                    for (const auto& define : local.m_defines)
                        args.push_back((tc.m_kind == CompilerKind::MSVC ? "/D" : "-D") + define);
                    if (tc.m_kind == CompilerKind::MSVC && !tc.m_systemIncludes.empty())
                    {
                        args.push_back("/external:W0");
                        for (const auto& include : tc.m_systemIncludes)
                            args.push_back("/external:I" + include.string());
                    }
                    return args;
                };
                auto sources = module.m_sources;
                if (!options.m_pchSource.empty() && std::none_of(
                                                        sources.begin(),
                                                        sources.end(),
                                                        [&](const auto& source)
                                                        {
                                                            return interfaceKey(resolve(ws, outputRoot, source)) ==
                                                                   interfaceKey(resolve(ws, outputRoot, options.m_pchSource));
                                                        }
                                                    ))
                    sources.push_back(options.m_pchSource);
                const bool hasCpp = std::any_of(
                    sources.begin(),
                    sources.end(),
                    [](const auto& source)
                    {
                        const auto ext = fs::path(source).extension().string();
                        return ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".C";
                    }
                );
                PchPlan pch;
                if (hasCpp && options.m_pchEnabled.value_or(!options.m_pchHeader.empty()))
                {
                    const auto header = resolve(ws, outputRoot, NativePaths::expandBuildValue(options.m_pchHeader, ws, outputRoot, cfg, t.m_name));
                    const auto source = options.m_pchSource.empty() ? fs::path{} : resolve(ws, outputRoot, options.m_pchSource);
                    if (!options.m_pchFile.empty())
                    {
                        pch = PchPlanner::useExternalPch(
                            tc,
                            header,
                            resolve(ws, outputRoot, options.m_pchFile),
                            options.m_pchObject.empty() ? fs::path{} : resolve(ws, outputRoot, options.m_pchObject)
                        );
                    }
                    else
                    {
                        std::string identity = interfaceKey(header) + "\n" + source.generic_string() + "\n" + ws.m_root.generic_string();
                        identity += options.m_documentation.value_or(false) ? "\ndocumentation:on" : "\ndocumentation:off";
                        for (const auto& arg : arguments(true))
                            identity += "\n" + arg;
                        for (const auto& dep : orderOnlyFor(t))
                            identity += "\norder:" + interfaceKey(dep);
                        if (options.m_sharedPch.value_or(false) && !source.empty())
                        {
                            const auto key = interfaceKey(source);
                            if (manualPchKeys.contains(key) && manualPchKeys.at(key) != identity)
                                throw std::runtime_error("shared manual PCH requires identical compiler options and dependencies: " + source.string());
                            manualPchKeys[key] = identity;
                        }
                        if (options.m_sharedPch.value_or(false) && sharedPchs.contains(identity))
                            pch = sharedPchs.at(identity);
                        else
                        {
                            pch = PchPlanner::planPch(
                                graph,
                                tc,
                                arguments(true),
                                header,
                                source,
                                objectDirectory,
                                ws.m_root,
                                orderOnlyFor(t),
                                t.m_name,
                                module.m_name,
                                options.m_documentation.value_or(false) && !source.empty()
                            );
                            if (options.m_sharedPch.value_or(false))
                                sharedPchs.emplace(identity, pch);
                        }
                    }
                    for (const auto& object : pch.m_objects)
                        addUnique(objects, object);
                    for (const auto& doc : pch.m_documents)
                        addUnique(documentation, doc);
                }
                for (const auto& source : sources)
                {
                    const fs::path src = resolve(ws, outputRoot, source);
                    if (!pch.m_compiledSource.empty() && interfaceKey(src) == interfaceKey(pch.m_compiledSource))
                        continue;
                    const auto extension = src.extension().string();
                    const bool isCpp = extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".C" || extension == ".mm";
                    const bool isC = extension == ".c" || extension == ".m";
                    const bool objective = extension == ".m" || extension == ".mm";
                    if (objective && tc.m_platform != "macos" && !tc.m_platform.starts_with("ios"))
                        throw std::runtime_error("Objective-C sources require an Apple toolchain");
                    const bool resource = extension == ".rc" || extension == ".RC";
                    if (!isC && !isCpp && !resource)
                        throw std::runtime_error("unsupported C/C++ source extension '" + extension + "' in " + src.string());
                    const fs::path obj = objectDirectory /
                                         (src.stem().string() + "_" + FileSystem::shortHash(src.generic_string()) + (resource ? ".res" : tc.m_extensionObject));
                    objects.push_back(obj);
                    Action action;
                    action.m_name = (resource ? "Resource " : "Compile ") + t.m_name + ":" + src.filename().string();
                    action.m_owner = t.m_name;
                    action.m_compile = !resource;
                    action.m_source = src;
                    action.m_workingDirectory = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
                    action.m_inputs = {src};
                    action.m_outputs = {obj};
                    action.m_orderOnly = orderOnlyFor(t);
                    action.m_rsp = !resource;
                    action.m_gnuRsp = tc.m_kind != CompilerKind::MSVC;
                    if (resource)
                    {
                        if (tc.m_kind != CompilerKind::MSVC || !fs::is_regular_file(tc.m_resourceCompiler))
                            throw std::runtime_error("Windows resource compilation requires a detected rc.exe: " + t.m_name);
                        action.m_inputs.push_back(tc.m_resourceCompiler);
                        std::vector<fs::path> resourceIncludes = {ws.m_root};
                        for (const auto& include : local.m_includes)
                            resourceIncludes.push_back(resolve(ws, outputRoot, include));
                        const auto dependencies = ResourceScanner::resourceDependencies(src, resourceIncludes);
                        action.m_inputs.insert(action.m_inputs.end(), dependencies.begin(), dependencies.end());
                        for (const auto& input : t.m_resourceInputs)
                            action.m_inputs.push_back(resolve(ws, outputRoot, NativePaths::expandBuildValue(input, ws, outputRoot, cfg, t.m_name)));
                        action.m_argv = {tc.m_resourceCompiler, "/nologo", "/fo" + obj.string()};
                        for (const auto& include : local.m_includes)
                            action.m_argv.push_back("/i" + resolve(ws, outputRoot, include).string());
                        for (const auto& define : local.m_defines)
                            action.m_argv.push_back("/d" + define);
                        action.m_argv.push_back(src.string());
                    }
                    else
                    {
                        action.m_inputs.push_back(isCpp ? tc.m_cppCompiler : tc.m_cCompiler);
                        action.m_argv = arguments(isCpp);
                        if (isCpp && options.m_documentation.value_or(false))
                        {
                            const auto doc = fs::path(obj.string() + ".xdc");
                            action.m_outputs.push_back(doc);
                            action.m_argv.push_back("/doc" + doc.string());
                            documentation.push_back(doc);
                        }
                        if (isCpp && !objective)
                        {
                            action.m_argv.insert(action.m_argv.end(), pch.m_flags.begin(), pch.m_flags.end());
                            action.m_inputs.insert(action.m_inputs.end(), pch.m_inputs.begin(), pch.m_inputs.end());
                        }
                        action.m_depfile = obj.string() + ".d";
                        if (tc.m_kind == CompilerKind::MSVC)
                        {
                            action.m_argv.insert(
                                action.m_argv.end(), {"/c", "/sourceDependencies", obj.string() + ".json", "/Fo" + obj.string(), src.string()}
                            );
                            action.m_depsStyle = "msvc-json";
                        }
                        else
                        {
                            action.m_argv.insert(
                                action.m_argv.end(), {"-c", "-MMD", "-MF", action.m_depfile.string(), "-MT", obj.string(), "-o", obj.string(), src.string()}
                            );
                            action.m_depsStyle = "gcc";
                        }
                    }
                    graph.m_actions.push_back(std::move(action));
                }
            }
            Action link;
            link.m_name = (t.m_kind == TargetKind::StaticLibrary ? "Archive " : "Link ") + t.m_name;
            link.m_owner = t.m_name;
            link.m_gnuRsp = tc.m_kind != CompilerKind::MSVC;
            link.m_workingDirectory = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
            link.m_inputs = objects;
            link.m_inputs.emplace_back(t.m_kind == TargetKind::StaticLibrary ? tc.m_archiver : tc.m_linker);
            link.m_orderOnly = orderOnlyFor(t);
            link.m_outputs = graph.m_targetOutputs.at(t.m_name);
            std::vector<fs::path> dependencyLibraries;
            std::vector<std::string> externalLibraries, externalDirectories, externalFlags, externalFrameworks, externalFrameworkDirs;
            auto linkTarget = t;
            for (const auto& m : ws.m_modules)
                if (m.m_targetName == t.m_name)
                    for (const auto& sanitizer : m.m_compile.m_sanitizers)
                        addUnique(linkTarget.m_compile.m_sanitizers, sanitizer);
            std::unordered_set<std::string> linked;
            std::function<void(const std::string&)> linkDep = [&](const std::string& name)
            {
                if (!linked.insert(name).second)
                    return;
                const auto& dep = *targets.at(name);
                if (tc.m_kind == CompilerKind::MSVC &&
                    (dep.m_kind == TargetKind::StaticLibrary || (dep.m_kind == TargetKind::HeaderOnly && !dep.m_link.m_libraries.empty())) &&
                    (NativeOptions::runtimePolicy(dep.m_compile) != NativeOptions::runtimePolicy(t.m_compile) ||
                     dep.m_compile.m_debugCRT.value_or(BuildModel::isDebug(cfg)) != t.m_compile.m_debugCRT.value_or(BuildModel::isDebug(cfg))))
                    throw std::runtime_error("incompatible CRT policies between " + t.m_name + " and " + dep.m_name);
                if (dep.m_kind == TargetKind::StaticLibrary || dep.m_kind == TargetKind::SharedLibrary)
                {
                    auto output = graph.m_targetOutputs.at(name);
                    if (dep.m_kind == TargetKind::SharedLibrary && tc.m_kind == CompilerKind::MSVC)
                    {
                        if (!dep.m_link.m_importLibrary)
                            throw std::runtime_error("cannot link a DLL with importlibrary(false): " + dep.m_name);
                        dependencyLibraries.push_back(output.at(1)); // import library
                    }
                    else
                        dependencyLibraries.push_back(output.at(0));
                }
                if (dep.m_kind == TargetKind::StaticLibrary || dep.m_kind == TargetKind::HeaderOnly)
                {
                    for (const auto& sanitizer : dep.m_compile.m_sanitizers)
                        addUnique(linkTarget.m_compile.m_sanitizers, sanitizer);
                    for (const auto& m : ws.m_modules)
                        if (m.m_targetName == dep.m_name)
                            for (const auto& sanitizer : m.m_compile.m_sanitizers)
                                addUnique(linkTarget.m_compile.m_sanitizers, sanitizer);
                    for (const auto& lib : dep.m_link.m_libraries)
                        addUnique(externalLibraries, lib);
                    for (const auto& f : dep.m_link.m_frameworks)
                        addUnique(externalFrameworks, f);
                    for (const auto& f : dep.m_link.m_frameworkDirectories)
                        addUnique(externalFrameworkDirs, f);
                    for (const auto& dir : dep.m_link.m_libraryDirectories)
                        addUnique(externalDirectories, dir);
                    for (const auto& flag : dep.m_link.m_linkFlags)
                        addUnique(externalFlags, flag);
                    for (const auto& sub : directDependencies(dep))
                        linkDep(sub);
                    for (const auto& sub : dep.m_interfaceDependencies)
                        linkDep(sub);
                }
                else if (dep.m_kind == TargetKind::SharedLibrary)
                {
                    for (const auto& sub : dep.m_publicDependencies)
                        linkDep(sub);
                    for (const auto& sub : dep.m_interfaceDependencies)
                        linkDep(sub);
                }
            };
            if (t.m_kind != TargetKind::StaticLibrary)
            {
                for (const auto& d : directDependencies(t))
                    linkDep(d);
                for (const auto& file : dependencyLibraries)
                    addUnique(link.m_inputs, file);
            }
            for (const auto& f : externalFrameworks)
                addUnique(linkTarget.m_link.m_frameworks, f);
            for (const auto& f : externalFrameworkDirs)
                addUnique(linkTarget.m_link.m_frameworkDirectories, f);
            NativeOptions::validateNativeOptions(linkTarget, tc);
            if (t.m_kind == TargetKind::StaticLibrary)
            {
                link.m_argv.push_back(tc.m_archiver);
                if (tc.m_kind == CompilerKind::MSVC)
                {
                    link.m_argv.insert(link.m_argv.end(), {"/nologo", "/OUT:" + link.m_outputs[0].string()});
                    if (t.m_compile.m_lto.value_or(false))
                        link.m_argv.push_back("/LTCG");
                    for (const auto& o : objects)
                        link.m_argv.push_back(o.string());
                }
                else
                {
                    link.m_argv.insert(link.m_argv.end(), {"rcs", link.m_outputs[0].string()});
                    for (const auto& o : objects)
                        link.m_argv.push_back(o.string());
                }
            }
            else if (tc.m_kind == CompilerKind::MSVC)
            {
                link.m_argv = {tc.m_linker, "/NOLOGO", "/OUT:" + link.m_outputs[0].string()};
                if (t.m_kind == TargetKind::SharedLibrary)
                {
                    link.m_argv.push_back("/DLL");
                    if (t.m_link.m_importLibrary)
                        link.m_argv.push_back("/IMPLIB:" + link.m_outputs[1].string());
                    else
                        link.m_argv.push_back("/NOIMPLIB");
                }
                for (const auto& symbol : t.m_link.m_exports)
                    link.m_argv.push_back("/EXPORT:" + symbol);
                const auto flags = NativeOptions::nativeLinkFlags(linkTarget, tc, cfg);
                link.m_argv.insert(link.m_argv.end(), flags.begin(), flags.end());
                if (NativeOptions::symbolsEnabled(t.m_compile, cfg))
                {
                    auto pdb = link.m_outputs.front();
                    pdb.replace_extension(".pdb");
                    link.m_argv.push_back("/PDB:" + pdb.string());
                }
                if (t.m_link.m_incremental.value_or(false))
                {
                    auto ilk = link.m_outputs.front();
                    ilk.replace_extension(".ilk");
                    link.m_argv.push_back("/ILK:" + ilk.string());
                }
                if (!t.m_link.m_moduleDefinition.empty())
                {
                    const auto file = resolve(ws, outputRoot, t.m_link.m_moduleDefinition);
                    link.m_inputs.push_back(file);
                    link.m_argv.push_back("/DEF:" + file.string());
                }
                if (!t.m_link.m_manifest.empty())
                {
                    const auto file = resolve(ws, outputRoot, t.m_link.m_manifest);
                    link.m_inputs.push_back(file);
                    link.m_argv.insert(link.m_argv.end(), {"/MANIFEST:EMBED", "/MANIFESTINPUT:" + file.string()});
                }
                for (const auto& obj : objects)
                    link.m_argv.push_back(obj.string());
                for (const auto& lib : dependencyLibraries)
                    link.m_argv.push_back(lib.string());
                for (const auto& dir : both(t.m_link.m_libraryDirectories, externalDirectories))
                    link.m_argv.push_back("/LIBPATH:" + resolve(ws, outputRoot, dir).string());
                for (const auto& lib : both(t.m_link.m_libraries, externalLibraries))
                {
                    if (lib.find('/') != std::string::npos || lib.find('\\') != std::string::npos)
                    {
                        auto file = resolve(ws, outputRoot, lib);
                        link.m_argv.push_back(file.string());
                        addUnique(link.m_inputs, file);
                    }
                    else
                        link.m_argv.push_back(lib);
                }
                for (const auto& f : both(t.m_link.m_linkFlags, externalFlags))
                    link.m_argv.push_back(f);
                link.m_rsp = true;
            }
            else
            {
                link.m_argv = {tc.m_linker, "-o", link.m_outputs[0].string()};
                if (t.m_kind == TargetKind::SharedLibrary)
                {
                    link.m_argv.push_back((tc.m_platform == "macos" || tc.m_platform.starts_with("ios")) ? "-dynamiclib" : "-shared");
                    link.m_argv.push_back(
                        ((tc.m_platform == "macos" || tc.m_platform.starts_with("ios")) ? "-Wl,-install_name,@rpath/" : "-Wl,-soname,") +
                        link.m_outputs[0].filename().string()
                    );
                }
                else
                {
                    link.m_argv.push_back(
                        (tc.m_platform == "macos" || tc.m_platform.starts_with("ios")) ? "-Wl,-rpath,@loader_path/../lib" : "-Wl,-rpath,$ORIGIN/../lib"
                    );
                }
                link.m_argv.push_back((tc.m_platform == "macos" || tc.m_platform.starts_with("ios")) ? "-Wl,-rpath,@loader_path" : "-Wl,-rpath,$ORIGIN");
                const auto flags = NativeOptions::nativeLinkFlags(linkTarget, tc, cfg);
                link.m_argv.insert(link.m_argv.end(), flags.begin(), flags.end());
                for (const auto& obj : objects)
                    link.m_argv.push_back(obj.string());
                for (const auto& lib : dependencyLibraries)
                    link.m_argv.push_back(lib.string());
                for (const auto& dir : both(t.m_link.m_libraryDirectories, externalDirectories))
                    link.m_argv.push_back("-L" + resolve(ws, outputRoot, dir).string());
                for (const auto& lib : both(t.m_link.m_libraries, externalLibraries))
                {
                    if (lib.find('/') != std::string::npos || lib.find('\\') != std::string::npos)
                    {
                        const fs::path path = resolve(ws, outputRoot, lib);
                        link.m_argv.push_back(path.string());
                        addUnique(link.m_inputs, path);
                    }
                    else
                        link.m_argv.push_back("-l" + lib);
                }
                for (const auto& dir : both(t.m_link.m_frameworkDirectories, externalFrameworkDirs))
                    link.m_argv.push_back("-F" + resolve(ws, outputRoot, dir).string());
                for (const auto& framework : both(t.m_link.m_frameworks, externalFrameworks))
                {
                    const fs::path f(framework);
                    const auto name = f.extension() == ".framework" ? f.stem().string() : f.filename().string();
                    if (f.has_parent_path())
                    {
                        const auto bundle = resolve(ws, outputRoot, framework);
                        link.m_argv.push_back("-F" + bundle.parent_path().string());
                        if (fs::is_regular_file(bundle / name))
                            addUnique(link.m_inputs, bundle / name);
                    }
                    link.m_argv.insert(link.m_argv.end(), {"-framework", name});
                }
                for (const auto& f : both(t.m_link.m_linkFlags, externalFlags))
                    link.m_argv.push_back(f);
                link.m_rsp = true;
            }
            graph.m_actions.push_back(std::move(link));
            if (wantsDocumentation)
            {
                if (documentation.empty())
                    throw std::runtime_error("cppdocumentation requires C++ translation units: " + t.m_name);
                if (!fs::is_regular_file(tc.m_documentationTool))
                    throw std::runtime_error("MSVC xdcmake.exe not found for cppdocumentation: " + t.m_name);
                auto directory = t.m_documentationDirectory.empty() ? "docs/${target}" : t.m_documentationDirectory;
                directory = NativePaths::expandBuildValue(directory, ws, outputRoot, cfg, t.m_name);
                if (directory.starts_with("@build/"))
                    directory.erase(0, 7);
                const auto folder = fs::absolute(outputRoot / directory).lexically_normal();
                if (!FileSystem::isWithin(fs::weakly_canonical(folder), fs::weakly_canonical(outputRoot)))
                    throw std::runtime_error("documentationdir must stay under the build directory");
                const auto name = t.m_outputName.empty() ? t.m_name : t.m_outputName;
                const auto output = folder / (name + ".xml");
                Action doc;
                doc.m_name = "Documentation " + t.m_name;
                doc.m_owner = t.m_name;
                doc.m_workingDirectory = ws.m_root;
                doc.m_inputs = documentation;
                doc.m_inputs.emplace_back(tc.m_documentationTool);
                doc.m_outputs = {output};
                doc.m_argv = {tc.m_documentationTool, "/old", name, "/nologo", "/Fo" + output.string()};
                for (const auto& input : documentation)
                    doc.m_argv.push_back("/Fs" + input.string());
                graph.m_actions.push_back(std::move(doc));
                graph.m_targetOutputs.at(t.m_name).push_back(output);
            }
        }
        RuntimePlanner::planRuntimeCopies(graph, ws, outputRoot, cfg, self);
        HookPlanner::planHooks(graph, ws, tc, outputRoot, cfg);
        graph.validate();
        return graph;
    }
} // namespace axlum
