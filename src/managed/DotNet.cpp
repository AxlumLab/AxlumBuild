/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "managed/DotNet.hpp"
#include "graph/Modules.hpp"
#include "graph/NativePaths.hpp"
#include "managed/NativeImage.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace axlum
{
    namespace ab = axlum;

    namespace
    {
        std::string next(const std::vector<std::string>& args, std::size_t& i)
        {
            if (i + 1 >= args.size())
                throw std::runtime_error("missing managed option value: " + args[i]);
            return args[++i];
        }

        std::string sdkVersion(const std::string& dotnet, const fs::path& cwd, const std::map<std::string, std::string>& env = {})
        {
            std::string output;
            const auto rc = Process::run(
                {dotnet, "--version"},
                cwd,
                env,
                [&](std::string_view text)
                {
                    output.append(text);
                }
            );
            std::istringstream lines(output);
            std::string line;
            while (std::getline(lines, line))
                if (!line.empty() && std::isdigit(static_cast<unsigned char>(line.front())))
                {
                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                        line.pop_back();
                    if (std::stoi(line) < 8)
                        throw std::runtime_error("AxlumBuild requires .NET SDK 8 or newer; selected SDK=" + line);
                    if (rc == 0)
                        return line;
                }
            throw std::runtime_error("cannot select a .NET SDK: " + output);
        }

    } // namespace

    std::map<std::string, fs::file_time_type> snapshot(const fs::path& dir)
    {
        std::map<std::string, fs::file_time_type> files;
        if (!fs::exists(dir))
            return files;
        for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied))
        {
            if (entry.is_regular_file())
                files.emplace(entry.path().generic_string(), entry.last_write_time());
        }
        return files;
    }

    int DotNetBuilder::buildManaged(const std::vector<std::string>& args)
    {
        std::map<std::string, fs::path> opts;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i].starts_with("--"))
            {
                const auto key = args[i];
                opts[key] = next(args, i);
            }
            else
                throw std::runtime_error("unknown __dotnet-build parameter: " + args[i]);
        }
        for (const auto* key : {"--project", "--config", "--artifacts", "--stamp"})
            if (!opts.contains(key))
                throw std::runtime_error(std::string("__dotnet-build missing ") + key);
        const auto dotnet = ab::Process::findExecutable("dotnet");
        if (dotnet.empty())
            throw std::runtime_error("dotnet SDK not found; install official .NET SDK to build managed targets");
        const auto project = fs::absolute(opts.at("--project"));
        const auto cwd = opts.contains("--cwd") ? fs::absolute(opts.at("--cwd")) : project.parent_path();
        (void)sdkVersion(dotnet, cwd);
        const auto artifacts = fs::absolute(opts.at("--artifacts"));
        const auto stamp = fs::absolute(opts.at("--stamp"));
        fs::create_directories(artifacts);
        fs::create_directories(stamp.parent_path());
        // The official .NET 8+ artifacts layout isolates ProjectReference outputs by project name.
        // Snapshot only compiled outputs; NuGet restore can update intermediate metadata every run.
        const auto before = snapshot(artifacts / "bin");
        auto trailing = [](fs::path p)
        {
            std::string s = p.string();
            if (!s.empty() && s.back() != fs::path::preferred_separator)
                s += fs::path::preferred_separator;
            return s;
        };
        std::vector<std::string> properties = {
            "-p:UseArtifactsOutput=true",
            "-p:AxlumInvocation=true",
            "-p:Configuration=" + opts.at("--config").string(),
            "-p:ArtifactsPath=" + artifacts.string(),
            "-p:AxlumGeneratedDir=" + trailing(stamp.parent_path().parent_path().parent_path() / "generated")
        };
        if (const auto* jobs = std::getenv("AXLUM_BUILD_JOBS"); jobs && *jobs)
            properties.push_back("-m:" + std::to_string(std::stoi(jobs)));
        std::vector<std::string> restore = {dotnet, "restore", project.string()};
        restore.insert(restore.end(), properties.begin(), properties.end());
        std::cout << "[dotnet] restore " << project << '\n' << std::flush;
        int rc = ab::Process::run(restore, cwd);
        if (rc != 0)
        {
            std::error_code ignored;
            fs::remove(stamp, ignored);
            return rc;
        }
        std::vector<std::string> build = {dotnet, "build", project.string(), "--no-restore", "-c", opts.at("--config").string()};
        build.insert(build.end(), properties.begin(), properties.end());
        if (const auto* rebuild = std::getenv("AXLUM_DOTNET_REBUILD"); rebuild && std::string(rebuild) == "1")
            build.push_back("-t:Rebuild");
        std::cout << "[dotnet] build " << project << '\n' << std::flush;
        rc = ab::Process::run(build, cwd);
        if (rc != 0)
        {
            std::error_code ignored;
            fs::remove(stamp, ignored);
            return rc;
        }
        if (!fs::exists(stamp) || before != snapshot(artifacts / "bin"))
        {
            std::ofstream out(stamp, std::ios::binary | std::ios::trunc);
            out << "ok\n";
            if (!out)
                throw std::runtime_error("cannot write dotnet stamp: " + stamp.string());
        }
        return 0;
    }

    int DotNetBuilder::publishManaged(
        const Workspace& ws,
        const Target& t,
        const Toolchain& tc,
        const fs::path& build,
        Configuration config,
        const fs::path& self,
        const PublishOptions& overrides,
        const ExecutionOptions& execution,
        bool buildFirst,
        fs::path* publishedDirectory
    )
    {
        using Json = nlohmann::json;
        if (t.m_kind != TargetKind::DotNetProject)
            throw std::runtime_error("publish requires a .NET target: " + t.m_name);
        const auto& declared = t.m_publish;
        const auto nativeLibrary = overrides.m_nativeLibrary.empty() ? declared.m_nativeLibrary : overrides.m_nativeLibrary;
        const bool aot = overrides.m_aot.value_or(declared.m_aot.value_or(!nativeLibrary.empty()));
        const bool library = aot && (!nativeLibrary.empty() || (t.m_generatedProject && !t.m_managedExecutable));
        const bool selfContained = overrides.m_selfContained.value_or(declared.m_selfContained.value_or(aot));
        const bool trimmed = overrides.m_trimmed.value_or(declared.m_trimmed.value_or(aot));
        const bool single = overrides.m_singleFile.value_or(declared.m_singleFile.value_or(false));
        if (aot && !selfContained)
            throw std::runtime_error("NativeAOT requires self-contained publication");
        if (aot && single)
            throw std::runtime_error("NativeAOT already produces a native binary; PublishSingleFile cannot be combined with it");
        if (trimmed && !selfContained)
            throw std::runtime_error("PublishTrimmed requires self-contained publication");
        if (!aot && (!nativeLibrary.empty() || !declared.m_exports.empty()))
            throw std::runtime_error("native library/export checks require PublishAot=true");
        if (library && t.m_managedExecutable)
            throw std::runtime_error("NativeLib=Shared requires a library target");
        auto rid = overrides.m_rid.empty() ? declared.m_rid : overrides.m_rid;
        if (rid.empty() && (aot || selfContained || single))
            rid = (tc.m_platform == "windows" ? "win"
                   : tc.m_platform == "macos" ? "osx"
                   : tc.m_platform == "linux" ? "linux"
                                              : "unsupported") +
                  std::string("-") + tc.m_architecture;
        static const std::set<std::string> s_supported = {
            "win-x64", "win-arm64", "linux-x64", "linux-arm64", "linux-musl-x64", "linux-musl-arm64", "osx-x64", "osx-arm64"
        };
        if (!rid.empty() && !s_supported.contains(rid))
            throw std::runtime_error("unsupported desktop .NET RID (Mono runtime targets are not supported): " + rid);
        if (aot)
        {
            const auto platform = rid.starts_with("win-") ? "windows" : rid.starts_with("osx-") ? "macos" : "linux";
            const auto architecture = rid.ends_with("arm64") ? "arm64" : "x64";
            if (platform != tc.m_platform || architecture != tc.m_architecture)
                throw std::runtime_error(
                    "NativeAOT RID must match the configured native platform/architecture; "
                    "configure a separate matching -B directory"
                );
        }
        const auto label = rid.empty() ? "portable" : rid;
        const auto mode = aot ? (library ? "aot-library" : "aot") : "managed";
        auto directory = overrides.m_directory.empty() ? declared.m_directory : overrides.m_directory;
        if (directory.empty())
            directory = "publish/${target}/${rid}/${mode}";
        auto replace = [&](std::string& value, const std::string& from, const std::string& to)
        {
            for (std::size_t at = 0; (at = value.find(from, at)) != std::string::npos; at += to.size())
                value.replace(at, from.size(), to);
        };
        replace(directory, "${rid}", label);
        replace(directory, "${mode}", mode);
        directory = NativePaths::expandBuildValue(directory, ws, build, config, t.m_name);
        if (directory.starts_with("@build/"))
            directory.erase(0, 7);
        const auto destination = fs::absolute(build / directory).lexically_normal();
        const auto root = fs::weakly_canonical(build);
        if (destination == build || !FileSystem::isWithin(fs::weakly_canonical(destination), root) || FileSystem::isWithin(destination, build / "dotnet") ||
            FileSystem::isWithin(destination, build / ".axlum-publish"))
            throw std::runtime_error("publish output must stay in its own directory under the build root: " + destination.string());
        const auto stage = fs::absolute(build / ".axlum-publish" / t.m_name / label / mode).lexically_normal();
        const auto artifacts = fs::absolute(build / "dotnet" / t.m_name / "publish-artifacts" / label / mode).lexically_normal();
        CommandSession session("publish", build, execution, {t.m_name});
        try
        {
            std::vector<std::string> prepare = {self.string(), "build", "-B", build.string(), "-t", t.m_name};
            if (execution.m_jobs)
                prepare.insert(prepare.end(), {"-j", std::to_string(execution.m_jobs)});
            if (execution.m_verbose)
                prepare.push_back("--verbose");
            if (execution.m_explain)
                prepare.push_back("--why-rebuild");
            int rc = buildFirst ? session.run("prepare", prepare) : 0;
            if (rc)
            {
                session.finish(rc);
                return rc;
            }
            const auto dotnet = tc.m_dotnet.empty() ? Process::findExecutable("dotnet") : tc.m_dotnet;
            if (dotnet.empty())
                throw std::runtime_error("dotnet SDK not found");
            const auto cwd = t.m_generatedProject ? (t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot) : fs::path(t.m_project).parent_path();
            auto env = FileSystem::readEnvironment(build / "axlum.environment.json");
            const auto sdk = sdkVersion(dotnet, cwd, env);
            if (!FileSystem::isWithin(fs::weakly_canonical(stage), root) || stage == build)
                throw std::runtime_error("invalid publish staging directory");
            if (fs::exists(stage))
                fs::remove_all(stage);
            fs::create_directories(stage);
            std::vector<std::string> command = {
                dotnet,
                "publish",
                t.m_project,
                "--configuration",
                BuildModel::toString(config),
                "--output",
                stage.string(),
                "-p:UseArtifactsOutput=true",
                "-p:AxlumInvocation=true",
                "-p:ArtifactsPath=" + artifacts.generic_string(),
                "-p:AxlumGeneratedDir=" + fs::absolute(build / "generated").generic_string() + "/",
                std::string("-p:PublishAot=") + (aot ? "true" : "false"),
                std::string("-p:SelfContained=") + (selfContained ? "true" : "false"),
                std::string("-p:PublishTrimmed=") + (trimmed ? "true" : "false"),
                std::string("-p:PublishSingleFile=") + (single ? "true" : "false")
            };
            if (!rid.empty())
                command.insert(command.end(), {"--runtime", rid});
            if (library)
                command.push_back("-p:NativeLib=Shared");
            const auto strip = overrides.m_stripSymbols.has_value() ? overrides.m_stripSymbols : declared.m_stripSymbols;
            if (strip.has_value())
                command.push_back(std::string("-p:StripSymbols=") + (*strip ? "true" : "false"));
            if (execution.m_jobs)
                command.push_back("-m:" + std::to_string(execution.m_jobs));
            if (execution.m_verbose)
                command.push_back("--verbosity=detailed");
            rc = session.run("dotnet publish", command, cwd, env);
            if (rc)
            {
                session.finish(rc);
                return rc;
            }
            const auto assembly =
                t.m_managed.m_properties.contains("AssemblyName") ? t.m_managed.m_properties.at("AssemblyName") : fs::path(t.m_project).stem().string();
            fs::path primary;
            std::vector<fs::path> candidates;
            if (aot && library)
                candidates = {stage / (assembly + ".dll"), stage / (assembly + ".so"), stage / (assembly + ".dylib")};
            else if (t.m_managedExecutable || aot)
                candidates = {stage / (assembly + ".exe"), stage / assembly, stage / (assembly + ".dll")};
            else
                candidates = {stage / (assembly + ".dll"), stage / (assembly + ".exe"), stage / assembly};
            for (const auto& file : candidates)
                if (fs::is_regular_file(file))
                {
                    primary = file;
                    break;
                }
            if (primary.empty())
            {
                for (const auto& file : fs::directory_iterator(stage))
                {
                    const auto ext = file.path().extension().string();
                    if (!file.is_regular_file() || (ext != ".dll" && ext != ".exe" && ext != ".so" && ext != ".dylib" && !ext.empty()))
                        continue;
                    try
                    {
                        const auto image = NativeImageInspector::inspectNativeImage(file.path());
                        if (!aot || !image.m_managed)
                        {
                            primary = file.path();
                            break;
                        }
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }
            if (primary.empty())
                throw std::runtime_error("dotnet publish produced no primary artifact");
            Json native = nullptr;
            if (aot)
            {
                const auto image = NativeImageInspector::inspectNativeImage(primary);
                if (image.m_managed)
                    throw std::runtime_error("NativeAOT publication produced a CLR-managed image");
                const auto architecture = rid.ends_with("arm64") ? "arm64" : "x64";
                if (image.m_architecture != architecture)
                    throw std::runtime_error("published native architecture does not match RID");
                for (const auto& name : declared.m_exports)
                    if (std::find(image.m_exports.begin(), image.m_exports.end(), name) == image.m_exports.end())
                        throw std::runtime_error("missing native export: " + name);
                native = {
                    {"format", image.m_format},
                    {"architecture", image.m_architecture},
                    {"exports", image.m_exports},
                    {"imports", image.m_imports},
                    {"managedHeader", image.m_managed}
                };
            }
            const auto buildArtifacts =
                fs::is_regular_file(build / "axlum.artifacts.json") ? Json::parse(FileSystem::readAll(build / "axlum.artifacts.json")) : Json::object();
            std::set<std::string> copiedRuntime;
            auto copy = [&](const std::vector<std::string>& files, bool optional, const std::string& owner)
            {
                for (const auto& file : files)
                {
                    auto value = file;
                    replace(value, "${rid}", label);
                    replace(value, "${mode}", mode);
                    const auto source = NativePaths::resolveBuildInput(ws, build, NativePaths::expandBuildValue(value, ws, build, config, owner));
                    if (!fs::is_regular_file(source))
                    {
                        if (optional)
                            continue;
                        throw std::runtime_error("missing publish runtime input: " + source.string());
                    }
                    const auto output = stage / source.filename();
                    if (fs::exists(output) && FileSystem::readAll(output) != FileSystem::readAll(source))
                        throw std::runtime_error("publish runtime filename collision: " + source.filename().string());
                    FileSystem::writeIfChanged(output, FileSystem::readAll(source));
                    copiedRuntime.insert(output.filename().string());
                }
            };
            const auto expanded = ModulePlanner::planModules(ws, tc);
            std::map<std::string, const Target*> targets;
            for (const auto& candidate : expanded.m_targets)
                targets[candidate.m_name] = &candidate;
            std::set<std::string> visited;
            std::function<void(const Target&)> collect = [&](const Target& owner)
            {
                if (!visited.insert(owner.m_name).second)
                    return;
                copy(owner.m_runtimeFiles, false, owner.m_name);
                copy(owner.m_optionalRuntimeFiles, true, owner.m_name);
                if (owner.m_kind == TargetKind::SharedLibrary && buildArtifacts.contains(owner.m_name))
                {
                    const auto& outputs = buildArtifacts.at(owner.m_name).at("outputs");
                    if (!outputs.empty())
                        copy({outputs.at(0).get<std::string>()}, false, owner.m_name);
                }
                for (const auto* deps : {&owner.m_publicDependencies, &owner.m_privateDependencies, &owner.m_interfaceDependencies})
                    for (const auto& name : *deps)
                        collect(*targets.at(name));
            };
            collect(*targets.at(t.m_name));
            for (const auto& name : declared.m_dependencies)
                if (fs::path(name).filename() != fs::path(name) || !fs::is_regular_file(stage / name))
                    throw std::runtime_error("missing declared native dependency: " + name);
            Json dependencies = Json::array(), nativeImages = Json::array();
            std::vector<fs::path> images;
            if (aot)
                images.push_back(primary);
            for (const auto& entry : fs::directory_iterator(stage))
                if (entry.is_regular_file())
                {
                    const auto extension = entry.path().extension().string();
                    if ((extension == ".dll" || extension == ".so" || extension == ".dylib") &&
                        (aot || copiedRuntime.contains(entry.path().filename().string()) ||
                         std::find(declared.m_dependencies.begin(), declared.m_dependencies.end(), entry.path().filename().string()) !=
                             declared.m_dependencies.end()))
                        images.push_back(entry.path());
                }
            std::set<fs::path> inspected;
            for (std::size_t i = 0; i < images.size(); ++i)
            {
                const auto file = images[i].lexically_normal();
                if (!inspected.insert(file).second)
                    continue;
                const auto image = NativeImageInspector::inspectNativeImage(file);
                if (image.m_managed)
                {
                    if (std::find(declared.m_dependencies.begin(), declared.m_dependencies.end(), file.filename().string()) != declared.m_dependencies.end())
                        throw std::runtime_error("declared native dependency is managed: " + file.filename().string());
                    continue;
                }
                if (!rid.empty() && image.m_architecture != (rid.ends_with("arm64") ? "arm64" : "x64"))
                    throw std::runtime_error("native dependency architecture mismatch: " + file.filename().string());
                nativeImages.push_back(
                    {{"file", file.lexically_relative(stage).generic_string()},
                     {"format", image.m_format},
                     {"architecture", image.m_architecture},
                     {"exports", image.m_exports},
                     {"imports", image.m_imports}}
                );
                for (const auto& name : image.m_imports)
                {
                    auto lower = name;
                    std::transform(
                        lower.begin(),
                        lower.end(),
                        lower.begin(),
                        [](unsigned char c)
                        {
                            return static_cast<char>(std::tolower(c));
                        }
                    );
                    if (aot && (lower.find("coreclr") != std::string::npos || lower.find("mono") != std::string::npos || lower == "mscoree.dll"))
                        throw std::runtime_error("NativeAOT image imports a managed runtime: " + name);
                    auto shipped = stage / fs::path(name).filename();
                    if (!fs::is_regular_file(shipped))
                        shipped = file.parent_path() / fs::path(name).filename();
                    const auto present = fs::is_regular_file(shipped);
                    const auto system =
                        NativeImageInspector::systemNativeDependency(name) ||
                        std::find(declared.m_systemDependencies.begin(), declared.m_systemDependencies.end(), name) != declared.m_systemDependencies.end();
                    dependencies.push_back(
                        {{"from", file.lexically_relative(stage).generic_string()},
                         {"name", name},
                         {"status",
                          present  ? "shipped"
                          : system ? "platform"
                                   : "missing"}}
                    );
                    if (!present && !system)
                        throw std::runtime_error("unresolved native import; ship it with runtimefiles or declare system_dependencies: " + name);
                    if (present)
                        images.push_back(shipped);
                }
            }
            Json files = Json::array();
            std::set<std::string> current;
            for (const auto& entry : fs::recursive_directory_iterator(stage))
                if (entry.is_regular_file())
                {
                    const auto relative = entry.path().lexically_relative(stage).generic_string();
                    if (relative == "axlum.publish.json")
                        throw std::runtime_error("axlum.publish.json is reserved for publication metadata");
                    auto lower = entry.path().filename().string();
                    std::transform(
                        lower.begin(),
                        lower.end(),
                        lower.begin(),
                        [](unsigned char c)
                        {
                            return static_cast<char>(std::tolower(c));
                        }
                    );
                    if (aot && (lower == "coreclr.dll" || lower == "libcoreclr.so" || lower == "mono-2.0-sgen.dll"))
                        throw std::runtime_error("NativeAOT output contains a managed runtime");
                    current.insert(relative);
                    files.push_back({{"path", relative}, {"size", entry.file_size()}, {"sha256", NativeImageInspector::fileSha256(entry.path())}});
                }
            // Validate ownership conflicts before touching the previous publication.
            if (fs::is_regular_file(build / "axlum.artifacts.json"))
            {
                const auto& graph = buildArtifacts;
                for (const auto& target : graph)
                    for (const auto& output : target.at("outputs"))
                        for (const auto& file : current)
                            if (fs::absolute(output.get<std::string>()).lexically_normal() == (destination / file).lexically_normal())
                                throw std::runtime_error("publish output overlaps a build artifact: " + file);
            }
            std::sort(
                files.begin(),
                files.end(),
                [](const auto& a, const auto& b)
                {
                    return a.at("path").template get<std::string>() < b.at("path").template get<std::string>();
                }
            );
            std::sort(
                nativeImages.begin(),
                nativeImages.end(),
                [](const auto& a, const auto& b)
                {
                    return a.at("file").template get<std::string>() < b.at("file").template get<std::string>();
                }
            );
            std::sort(
                dependencies.begin(),
                dependencies.end(),
                [](const auto& a, const auto& b)
                {
                    return a.at("from").template get<std::string>() + a.at("name").template get<std::string>() <
                           b.at("from").template get<std::string>() + b.at("name").template get<std::string>();
                }
            );
            const auto manifest = destination / "axlum.publish.json";
            const auto previous = fs::is_regular_file(manifest) ? Json::parse(FileSystem::readAll(manifest)) : Json::object();
            for (const auto& file : current)
                FileSystem::writeIfChanged(destination / file, FileSystem::readAll(stage / file));
            if (previous.contains("files"))
                for (const auto& file : previous.at("files"))
                {
                    const auto relative = file.at("path").get<std::string>();
                    const auto output = destination / relative;
                    if (!current.contains(relative) && FileSystem::isWithin(fs::weakly_canonical(output), fs::weakly_canonical(destination)) &&
                        fs::is_regular_file(output))
                        fs::remove(output);
                }
            Json result = {
                {"schemaVersion", 1},
                {"target", t.m_name},
                {"configuration", BuildModel::toString(config)},
                {"rid", label},
                {"mode", mode},
                {"dotnetSdk", sdk},
                {"selfContained", selfContained},
                {"entrypoint", primary.lexically_relative(stage).generic_string()},
                {"native", native},
                {"nativeImages", nativeImages},
                {"nativeDependencies", dependencies},
                {"declaredNativeDependencies", declared.m_dependencies},
                {"files", files}
            };
            FileSystem::writeIfChanged(manifest, result.dump(2) + "\n");
            if (publishedDirectory)
                *publishedDirectory = destination;
            std::cout << "Published " << t.m_name << " -> " << destination << '\n';
            session.finish(0);
            return 0;
        }
        catch (const std::exception& error)
        {
            session.recordFailure(error.what());
            session.finish(2);
            return 2;
        }
    }

} // namespace axlum
