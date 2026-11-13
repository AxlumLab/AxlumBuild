/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "core/BuildOperations.hpp"
#include "graph/NativePaths.hpp"
#include "managed/DotNet.hpp"
#include "managed/NativeImage.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        using Json = nlohmann::json;

        fs::path normalized(const fs::path& path)
        {
            return fs::weakly_canonical(fs::absolute(path)).lexically_normal();
        }

        std::string pathKey(const fs::path& path)
        {
            auto value = normalized(path).generic_string();
#ifdef _WIN32
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char c)
                {
                    return char(std::tolower(c));
                }
            );
#endif
            return value;
        }

        fs::path childPath(const fs::path& root, const fs::path& relative)
        {
            if (relative.empty() || relative.is_absolute() || relative.has_root_name())
                throw std::runtime_error("expected a nonempty relative installation path: " + relative.string());
            const auto child = normalized(root / relative), parent = normalized(root);
            if (child == parent || !FileSystem::isWithin(child, parent))
                throw std::runtime_error("path escapes its owning directory: " + relative.string());
            return child;
        }

        void removeOwned(const fs::path& path, const fs::path& root, bool dryRun, bool recursive = false)
        {
            const auto resolved = normalized(path), owner = normalized(root);
            if (resolved == owner || !FileSystem::isWithin(resolved, owner))
                throw std::runtime_error("refusing to remove a path outside its owner: " + path.string());
            if (!fs::exists(path) && !fs::is_symlink(path))
                return;
            std::cout << (dryRun ? "Would remove " : "Remove ") << path << '\n';
            if (!dryRun)
            {
                if (recursive && !fs::is_symlink(path))
                    fs::remove_all(path);
                else
                    fs::remove(path);
            }
        }

        std::string resolveTarget(std::string name, const BuildGraph& graph)
        {
            for (std::size_t at; (at = name.find("::")) != std::string::npos;)
                name.replace(at, 2, "__");
            if (graph.m_targetAliases.contains(name))
                name = graph.m_targetAliases.at(name);
            if (graph.m_disabledTargets.contains(name))
                throw std::runtime_error(graph.m_disabledTargets.at(name));
            if (!graph.m_targetKinds.contains(name))
                throw std::runtime_error("unknown target: " + name);
            return name;
        }

        std::set<std::string> selectedTargets(const CommandContext& context, const std::vector<std::string>& names, bool dependencies)
        {
            std::set<std::string> selected;
            std::function<void(std::string)> visit = [&](std::string name)
            {
                name = resolveTarget(name, context.m_graph);
                if (!selected.insert(name).second || !dependencies)
                    return;
                const auto found = context.m_graph.m_targetDependencies.find(name);
                if (found != context.m_graph.m_targetDependencies.end())
                    for (const auto& dep : found->second)
                        visit(dep);
            };
            if (names.empty() || std::find(names.begin(), names.end(), "all") != names.end())
                for (const auto& target : context.m_workspace.m_targets)
                {
                    if (!target.m_name.starts_with("_rhodium_"))
                        visit(target.m_name);
                }
            else
                for (const auto& name : names)
                    visit(name);
            return selected;
        }

        bool componentMatches(const std::string& requested, const std::string& actual)
        {
            return requested == "all" || requested == actual;
        }

        void rememberPublication(const fs::path& build, const fs::path& prefix, const std::string& manifest)
        {
            const auto file = build / "axlum.installations.json";
            auto records = fs::is_regular_file(file) ? Json::parse(FileSystem::readAll(file)) : Json::array();
            for (const auto& record : records)
                if (pathKey(record.at("prefix").get<std::string>()) == pathKey(prefix) && record.at("manifest") == manifest)
                    return;
            records.push_back({{"prefix", normalized(prefix).generic_string()}, {"manifest", manifest}});
            FileSystem::writeIfChanged(file, records.dump(2) + "\n");
        }

        std::set<std::string> indexSelection(const Json& index, const fs::path& build, const std::vector<std::string>& requested, bool dependencies)
        {
            const auto selection = fs::is_regular_file(build / "axlum.target-selection.json")
                                       ? Json::parse(FileSystem::readAll(build / "axlum.target-selection.json"))
                                       : Json::object();
            std::set<std::string> result;
            std::function<void(std::string)> visit = [&](std::string name)
            {
                for (std::size_t at; (at = name.find("::")) != std::string::npos;)
                    name.replace(at, 2, "__");
                if (selection.contains("aliases") && selection.at("aliases").contains(name))
                    name = selection.at("aliases").at(name).get<std::string>();
                if (!index.contains(name))
                    throw std::runtime_error("unknown or unavailable target: " + name);
                if (!result.insert(name).second || !dependencies)
                    return;
                for (const auto& dep : index.at(name).value("dependencies", std::vector<std::string>{}))
                    visit(dep);
            };
            if (requested.empty() || std::find(requested.begin(), requested.end(), "all") != requested.end())
                for (const auto& [name, value] : index.items())
                {
                    (void)value;
                    visit(name);
                }
            else
                for (const auto& name : requested)
                    visit(name);
            return result;
        }
    } // namespace

    int BuildOperations::installTargets(
        const CommandContext& context,
        const std::vector<std::string>& names,
        const InstallRequest& request,
        const PublishOptions& publication,
        const ExecutionOptions& execution
    )
    {
        const auto& build = context.m_buildDirectory;
        const auto selected = selectedTargets(context, names, true);
        const auto roots = selectedTargets(context, names, false);
        std::map<std::string, const Target*> targets;
        for (const auto& target : context.m_workspace.m_targets)
            targets[target.m_name] = &target;
        bool hasManaged = false, hasNative = false;
        for (const auto& name : selected)
        {
            const auto kind = context.m_graph.m_targetKinds.at(name);
            hasManaged |= kind == TargetKind::DotNetProject;
            hasNative |= kind == TargetKind::Executable || kind == TargetKind::StaticLibrary || kind == TargetKind::SharedLibrary;
        }
        if (!hasManaged && (publication.m_aot.has_value() || !publication.m_rid.empty() || publication.m_selfContained.has_value() ||
                            publication.m_trimmed.has_value() || publication.m_singleFile.has_value() || !publication.m_nativeLibrary.empty()))
            throw std::runtime_error(".NET publication options require a managed target");
        if (!hasManaged && !hasNative && selected.empty())
            throw std::runtime_error("no installable targets");
        if (!request.m_noBuild && !request.m_dryRun)
        {
            std::vector<std::string> buildTargets;
            for (const auto& name : roots)
                buildTargets.push_back(name);
            const int rc = BuildExecutor::executeNinja(build, context.m_toolchain.m_ninja, "build", buildTargets, execution);
            if (rc)
                return rc;
        }
        const bool managedRoots = !roots.empty() && std::all_of(
                                                        roots.begin(),
                                                        roots.end(),
                                                        [&](const auto& name)
                                                        {
                                                            return context.m_graph.m_targetKinds.at(name) == TargetKind::DotNetProject;
                                                        }
                                                    );
        const bool legacyManaged = !request.m_install && managedRoots && request.m_prefix.empty();
        const auto prefix =
            normalized(request.m_prefix.empty() ? (publication.m_directory.empty() ? build / "publish" : build / publication.m_directory) : request.m_prefix);
        if (prefix == normalized(build) || prefix == normalized(context.m_workspace.m_root))
            throw std::runtime_error("installation prefix cannot equal the build or source root");
        const auto manifest = prefix / "axlum.install.json";
        auto previous = fs::is_regular_file(manifest) ? Json::parse(FileSystem::readAll(manifest)) : Json{{"files", Json::array()}};

        struct File
        {
            fs::path m_source, m_relative;
            std::string m_component, m_hash;
            std::set<std::string> m_owners;
        };

        std::map<std::string, File> files;
        const auto add = [&](const fs::path& source, const fs::path& relative, const std::string& component, const std::string& owner)
        {
            if (!componentMatches(request.m_component, component))
                return;
            const auto destination = childPath(prefix, relative);
            if (pathKey(destination) == pathKey(source))
                throw std::runtime_error("installation cannot overwrite its source: " + source.string());
            for (const auto& [target, outputs] : context.m_graph.m_targetOutputs)
                for (const auto& output : outputs)
                    if (pathKey(destination) == pathKey(output))
                        throw std::runtime_error("installation overlaps a build artifact: " + destination.string());
            if (!request.m_dryRun && !fs::is_regular_file(source))
                throw std::runtime_error("missing install input; build first: " + source.string());
            const auto hash = fs::is_regular_file(source) ? NativeImageInspector::fileSha256(source) : std::string{};
            auto [it, inserted] = files.try_emplace(pathKey(destination), File{source, relative, component, hash, {owner}});
            if (!inserted)
            {
                if (it->second.m_hash != hash || it->second.m_component != component)
                    throw std::runtime_error("installation filename collision: " + relative.string());
                it->second.m_owners.insert(owner);
            }
        };
        for (const auto& name : selected)
        {
            if (name.starts_with("_rhodium_"))
                continue;
            const Target* target = targets.contains(name) ? targets.at(name) : nullptr;
            if (target && !target->m_install.m_enabled)
                continue;
            const InstallOptions policy = target ? target->m_install : InstallOptions{};
            const auto kind = context.m_graph.m_targetKinds.at(name);
            if (legacyManaged && kind != TargetKind::DotNetProject)
                continue;
            if (kind == TargetKind::DotNetProject)
            {
                if (!componentMatches(request.m_component, "runtime"))
                    continue;
                if (request.m_dryRun)
                {
                    std::cout << "Would publish .NET target " << name << (legacyManaged ? " using its SDK layout" : " to " + prefix.string()) << '\n';
                    continue;
                }
                auto overrides = publication;
                if (!legacyManaged)
                    overrides.m_directory = ".axlum-install-stage/" + name;
                fs::path directory;
                const int rc = DotNetBuilder::publishManaged(
                    context.m_workspace,
                    *target,
                    context.m_toolchain,
                    build,
                    context.m_configuration,
                    context.m_executable,
                    overrides,
                    execution,
                    false,
                    &directory
                );
                if (rc)
                    return rc;
                rememberPublication(build, directory, "axlum.publish.json");
                if (legacyManaged)
                    continue;
                for (const auto& entry : fs::recursive_directory_iterator(directory))
                    if (entry.is_regular_file())
                        add(entry.path(), fs::path(policy.m_bindir) / name / entry.path().lexically_relative(directory), "runtime", name);
                continue;
            }
            if (kind != TargetKind::Codegen && kind != TargetKind::HeaderOnly)
                for (const auto& source : context.m_graph.m_targetOutputs.at(name))
                {
                    auto ext = source.extension().string();
                    std::transform(
                        ext.begin(),
                        ext.end(),
                        ext.begin(),
                        [](unsigned char c)
                        {
                            return char(std::tolower(c));
                        }
                    );
                    if (ext == ".exp" || ext == ".ilk" || ext == ".stamp" || ext == ".xdc")
                        continue;
                    const bool symbol = ext == ".pdb" || ext == ".debug";
                    if (symbol && publication.m_stripSymbols.value_or(false))
                        continue;
                    const bool library = ext == ".lib" || ext == ".a" || ext == ".so" || ext == ".dylib";
                    const auto component = symbol ? "symbols" : ext == ".lib" || ext == ".a" ? "development" : "runtime";
                    add(source, fs::path(library ? policy.m_libdir : policy.m_bindir) / source.filename(), component, name);
                }
            if (!target)
                continue;
            const auto base = normalized(context.m_workspace.m_root / policy.m_headerBase);
            for (const auto& header : policy.m_headers)
            {
                const auto source = normalized(NativePaths::resolveBuildInput(context.m_workspace, build, header));
                if (!FileSystem::isWithin(source, base) || source == base)
                    throw std::runtime_error("install header is outside installheaderbase: " + source.string());
                add(source, fs::path(policy.m_includedir) / source.lexically_relative(base), "development", name);
            }
            for (const auto& file : policy.m_files)
            {
                const auto source = NativePaths::resolveBuildInput(context.m_workspace, build, file);
                add(source, fs::path(policy.m_datadir) / source.filename(), "runtime", name);
            }
        }
        if (legacyManaged)
            return 0;
        auto records = Json::array();
        std::vector<fs::path> stale;
        for (auto record : previous.at("files"))
        {
            const auto destination = childPath(prefix, record.at("path").get<std::string>());
            auto owners = record.at("owners").get<std::set<std::string>>();
            if (componentMatches(request.m_component, record.at("component")))
                for (const auto& name : selected)
                    owners.erase(name);
            const auto current = files.find(pathKey(destination));
            if (current != files.end())
            {
                if (!owners.empty() && record.at("sha256").get<std::string>() != current->second.m_hash)
                    throw std::runtime_error("installed file is owned by another target: " + destination.string());
                current->second.m_owners.insert(owners.begin(), owners.end());
            }
            else if (!owners.empty())
            {
                record["owners"] = owners;
                records.push_back(record);
            }
            else
            {
                if (fs::is_regular_file(destination) && NativeImageInspector::fileSha256(destination) != record.at("sha256").get<std::string>())
                    throw std::runtime_error("stale installed file was modified: " + destination.string());
                stale.push_back(destination);
            }
        }
        for (const auto& [key, file] : files)
        {
            (void)key;
            std::cout << (request.m_dryRun ? "Would install " : "Install ") << file.m_source << " -> " << prefix / file.m_relative << '\n';
            if (!request.m_dryRun)
            {
                const auto destination = childPath(prefix, file.m_relative);
                FileSystem::writeIfChanged(destination, FileSystem::readAll(file.m_source));
                fs::permissions(destination, fs::status(file.m_source).permissions());
            }
            records.push_back(
                {{"path", file.m_relative.generic_string()}, {"component", file.m_component}, {"owners", file.m_owners}, {"sha256", file.m_hash}}
            );
        }
        for (const auto& file : stale)
            removeOwned(file, prefix, request.m_dryRun);
        if (!request.m_dryRun)
        {
            FileSystem::writeIfChanged(
                manifest, Json{{"schemaVersion", 1}, {"configuration", BuildModel::toString(context.m_configuration)}, {"files", records}}.dump(2) + "\n"
            );
            rememberPublication(build, prefix, "axlum.install.json");
        }
        return 0;
    }

    int BuildOperations::uninstallTargets(const fs::path& prefix, const std::vector<std::string>& targets, const std::string& component, bool dryRun)
    {
        const auto manifest = prefix / "axlum.install.json";
        if (!fs::is_regular_file(manifest))
            throw std::runtime_error("installation manifest not found: " + manifest.string());
        auto data = Json::parse(FileSystem::readAll(manifest)), kept = Json::array();
        std::vector<fs::path> remove;
        std::set<std::string> known;
        for (const auto& record : data.at("files"))
            for (const auto& owner : record.at("owners"))
                known.insert(owner.get<std::string>());
        for (const auto& target : targets)
            if (!known.contains(target))
                throw std::runtime_error("target is not installed: " + target);
        for (auto record : data.at("files"))
        {
            auto owners = record.at("owners").get<std::set<std::string>>();
            if (componentMatches(component, record.at("component")))
            {
                if (targets.empty())
                    owners.clear();
                else
                    for (const auto& target : targets)
                        owners.erase(target);
            }
            if (!owners.empty())
            {
                record["owners"] = owners;
                kept.push_back(record);
                continue;
            }
            const auto file = childPath(prefix, record.at("path").get<std::string>());
            if (fs::is_regular_file(file) && NativeImageInspector::fileSha256(file) != record.at("sha256").get<std::string>())
                throw std::runtime_error("installed file was modified; leaving it in place: " + file.string());
            remove.push_back(file);
        }
        for (const auto& file : remove)
            removeOwned(file, prefix, dryRun);
        if (!dryRun)
        {
            if (kept.empty())
                removeOwned(manifest, prefix, false);
            else
            {
                data["files"] = kept;
                FileSystem::writeIfChanged(manifest, data.dump(2) + "\n");
            }
        }
        return 0;
    }

    int BuildOperations::runTarget(
        const CommandContext& context,
        std::string target,
        const std::vector<std::string>& arguments,
        const fs::path& workingDirectory,
        const std::vector<std::string>& environment,
        bool noBuild,
        const ExecutionOptions& execution
    )
    {
        if (execution.m_dryRun)
            throw std::runtime_error("run does not support --dry-run");
        if (target.empty())
            for (const auto& candidate : context.m_workspace.m_targets)
                if (candidate.m_kind == TargetKind::Executable || candidate.m_managedExecutable)
                {
                    if (!target.empty())
                        throw std::runtime_error("run requires --target when multiple runnable targets exist");
                    target = candidate.m_name;
                }
        target = resolveTarget(target, context.m_graph);
        const auto kind = context.m_graph.m_targetKinds.at(target);
        if (kind != TargetKind::Executable && kind != TargetKind::DotNetProject)
            throw std::runtime_error("target is not runnable: " + target);
        if (kind == TargetKind::Executable && context.m_toolchain.m_target != context.m_toolchain.m_host)
        {
            const auto host = ToolchainDiscovery::hostTriple();
            const bool archMatches = (host.starts_with("x86_64") && context.m_toolchain.m_architecture == "x64") ||
                                     ((host.starts_with("arm64") || host.starts_with("aarch64")) && context.m_toolchain.m_architecture == "arm64");
            const bool platformMatches = (host.find("windows") != std::string::npos && context.m_toolchain.m_platform == "windows") ||
                                         (host.find("linux") != std::string::npos && context.m_toolchain.m_platform == "linux") ||
                                         (host.find("apple") != std::string::npos && context.m_toolchain.m_platform == "macos");
            if (!archMatches || !platformMatches)
                throw std::runtime_error("cannot run a cross-compiled target on this host");
        }
        if (!noBuild)
        {
            const int rc = BuildExecutor::executeNinja(context.m_buildDirectory, context.m_toolchain.m_ninja, "build", {target}, execution);
            if (rc)
                return rc;
        }
        fs::path executable;
        std::vector<std::string> command;
        if (kind == TargetKind::Executable)
        {
            executable = context.m_graph.m_targetOutputs.at(target).front();
            command.push_back(executable.string());
        }
        else
        {
            const auto root = context.m_buildDirectory / "dotnet" / target / "artifacts/bin";
            if (fs::is_directory(root))
                for (const auto& file : fs::recursive_directory_iterator(root))
                    if (file.is_regular_file() && file.path().filename().string().ends_with(".runtimeconfig.json"))
                    {
                        if (!executable.empty())
                            throw std::runtime_error("multiple managed runnable outputs; use a single-framework project");
                        auto name = file.path().filename().string();
                        name.resize(name.size() - std::string(".runtimeconfig.json").size());
                        executable = file.path().parent_path() / (name + ".dll");
                    }
            if (executable.empty())
                throw std::runtime_error("managed target has no runnable output; build an executable project first");
            command = {context.m_toolchain.m_dotnet, executable.string()};
        }
        if (!fs::is_regular_file(executable))
            throw std::runtime_error("runnable output is missing; build first: " + executable.string());
        command.insert(command.end(), arguments.begin(), arguments.end());
        auto env = FileSystem::readEnvironment(context.m_buildDirectory / "axlum.environment.json");
        std::string dirs = executable.parent_path().string();
#ifdef _WIN32
        const std::string libraryPath = "PATH", separator = ";";
#elif defined(__APPLE__)
        const std::string libraryPath = "DYLD_LIBRARY_PATH", separator = ":";
#else
        const std::string libraryPath = "LD_LIBRARY_PATH", separator = ":";
#endif
        for (const auto& [name, outputs] : context.m_graph.m_targetOutputs)
            if (context.m_graph.m_targetKinds.at(name) == TargetKind::SharedLibrary && !outputs.empty())
                dirs += separator + outputs.front().parent_path().string();
        const auto inherited = env.contains(libraryPath) ? env.at(libraryPath) : std::getenv(libraryPath.c_str()) ? std::getenv(libraryPath.c_str()) : "";
        env[libraryPath] = dirs + separator + inherited;
        for (const auto& value : environment)
        {
            const auto at = value.find('=');
            if (at == std::string::npos || at == 0)
                throw std::runtime_error("--env expects NAME=VALUE");
            env[value.substr(0, at)] = value.substr(at + 1);
        }
        CommandSession session("run", context.m_buildDirectory, execution, {target});
        const int rc = session.run("run", command, workingDirectory.empty() ? executable.parent_path() : fs::absolute(workingDirectory), env);
        session.finish(rc);
        return rc;
    }

    int BuildOperations::cleanBuild(
        const fs::path& directory,
        const std::string& ninja,
        const std::vector<std::string>& targets,
        const CleanRequest& request,
        const ExecutionOptions& execution
    )
    {
        if ((request.m_mode == "cache" || request.m_mode == "all") && !targets.empty())
            throw std::runtime_error("configuration cache cleaning is workspace-wide; omit --target");
        const auto index = Json::parse(FileSystem::readAll(directory / "axlum.artifacts.json"));
        const auto selected = indexSelection(index, directory, targets, request.m_dependencies);
        const bool build = request.m_mode == "build" || request.m_mode == "all";
        if (build && request.m_dependencies && !request.m_dryRun)
        {
            const int rc = BuildExecutor::executeNinja(directory, ninja, "clean", targets, execution);
            if (rc)
                return rc;
        }
        if (build || request.m_mode == "intermediates")
            for (const auto& name : selected)
            {
                for (const auto& field : {"intermediates", "outputs"})
                {
                    if (std::string(field) == "outputs" && !build)
                        continue;
                    for (const auto& file : index.at(name).value(field, std::vector<std::string>{}))
                        removeOwned(file, directory, request.m_dryRun);
                }
                if (index.at(name).at("kind") == "dotnet_project")
                {
                    const auto artifacts = childPath(directory, fs::path("dotnet") / name / "artifacts");
                    removeOwned(build ? artifacts : artifacts / "obj", directory, request.m_dryRun, true);
                }
                if (index.at(name).contains("nativeProjectPlan"))
                    removeOwned(childPath(directory, fs::path("native-projects") / name / "obj"), directory, request.m_dryRun, true);
            }
        if (request.m_mode == "publish" || request.m_mode == "all")
        {
            const auto registry = directory / "axlum.installations.json";
            if (fs::is_regular_file(registry))
                for (const auto& entry : Json::parse(FileSystem::readAll(registry)))
                {
                    const fs::path prefix = entry.at("prefix").get<std::string>();
                    if (normalized(prefix) == normalized(directory) || !FileSystem::isWithin(normalized(prefix), normalized(directory)))
                        continue;
                    const auto manifest = childPath(prefix, entry.at("manifest").get<std::string>());
                    if (!fs::is_regular_file(manifest))
                        continue;
                    if (entry.at("manifest") == "axlum.install.json")
                    {
                        const auto data = Json::parse(FileSystem::readAll(manifest));
                        std::set<std::string> owners;
                        for (const auto& file : data.at("files"))
                            for (const auto& owner : file.at("owners"))
                                if (selected.contains(owner.get<std::string>()))
                                    owners.insert(owner.get<std::string>());
                        if (!owners.empty())
                            BuildOperations::uninstallTargets(prefix, {owners.begin(), owners.end()}, "all", request.m_dryRun);
                    }
                    else
                    {
                        const auto data = Json::parse(FileSystem::readAll(manifest));
                        if (!selected.contains(data.value("target", "")))
                            continue;
                        for (const auto& file : data.at("files"))
                        {
                            const auto path = childPath(prefix, file.at("path").get<std::string>());
                            if (fs::is_regular_file(path) && NativeImageInspector::fileSha256(path) != file.at("sha256").get<std::string>())
                                throw std::runtime_error("published file was modified: " + path.string());
                        }
                        for (const auto& file : data.at("files"))
                            removeOwned(childPath(prefix, file.at("path").get<std::string>()), prefix, request.m_dryRun);
                        removeOwned(manifest, prefix, request.m_dryRun);
                    }
                }
        }
        if (request.m_mode == "cache" || request.m_mode == "all")
        {
            if (!targets.empty())
                throw std::runtime_error("configuration cache cleaning is workspace-wide; omit --target");
            for (const auto& name :
                 {"build.ninja",
                  "compile_commands.json",
                  "axlum.environment.json",
                  "axlum.packages",
                  "axlum.outputs",
                  "axlum.target-selection.json",
                  "axlum.artifacts.json",
                  ".ninja_log",
                  ".ninja_deps",
                  "axlum.state"})
                removeOwned(directory / name, directory, request.m_dryRun);
        }
        return 0;
    }
} // namespace axlum
