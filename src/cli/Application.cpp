/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "cli/Application.hpp"
#include "backend/Ninja.hpp"
#include "cli/ConfigurationCache.hpp"
#include "core/BuildOperations.hpp"
#include "core/Diagnostics.hpp"
#include "core/Process.hpp"
#include "graph/Graph.hpp"
#include "managed/DotNet.hpp"
#include "managed/NativeProject.hpp"
#include "manifest/Features.hpp"
#include "manifest/Manifest.hpp"
#include "packages/Rhodium.hpp"
#include "toolchain/Toolchain.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ab = axlum;
namespace fs = std::filesystem;

namespace axlum
{
    namespace
    {
        using Json = nlohmann::json;

        Json fileVersion(const fs::path& file)
        {
            std::error_code error;
            const auto time = fs::last_write_time(file, error);
            if (error)
                return nullptr;
            const auto ticks = time.time_since_epoch();
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ticks);
            const auto fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(ticks - seconds);
            return std::to_string(seconds.count()) + ":" + std::to_string(fraction.count());
        }

        Json configureEnvironment()
        {
            Json values = Json::object();
            // MSBuild injects INCLUDE/LIB/LIBPATH for its own C++ tasks. Our
            // selected toolchain supplies these paths, so caller copies must not
            // invalidate the graph when switching between VS and a terminal.
            for (const auto* key :
                 {"CC", "CXX", "CL", "_CL_", "LINK", "_LINK_", "CFLAGS", "CXXFLAGS", "LDFLAGS", "ANDROID_SDK_ROOT", "ANDROID_NDK_ROOT", "RHODIUM_ANDROID_API"})
                values[key] = std::getenv(key) ? std::getenv(key) : "";
            return values;
        }

        bool ignoredConfigureDirectory(const fs::path& path, const fs::path& build)
        {
            const auto name = path.filename().string();
            return (name.starts_with(".") && name != ".rhodium") || name == "build" || name == "obj" || name == "bin" || FileSystem::isWithin(path, build) ||
                   fs::is_regular_file(path / "axlum.state") || fs::is_regular_file(path / "CMakeCache.txt");
        }

        Json directoryMembers(const fs::path& directory, const fs::path& build)
        {
            if (!fs::is_directory(directory))
                return nullptr;
            std::vector<std::string> members;
            for (const auto& entry : fs::directory_iterator(directory))
            {
                if (entry.is_directory() && ignoredConfigureDirectory(entry.path(), build))
                    continue;
                members.push_back(entry.path().filename().generic_string() + (entry.is_directory() ? "/" : ""));
            }
            std::sort(members.begin(), members.end());
            return members;
        }

        // Record directory membership as well as configuration files. A normal source
        // edit leaves directory mtimes unchanged; additions/removals invalidate globs.
        void saveConfigureInputs(const Workspace& ws, const fs::path& build, const fs::path& self)
        {
            Json files = Json::object(), directories = Json::object();
            auto record = [&](const fs::path& path)
            {
                files[fs::absolute(path).lexically_normal().generic_string()] = fileVersion(path);
            };
            auto scan = [&](const fs::path& root, bool package)
            {
                if (!fs::is_directory(root))
                {
                    record(root);
                    return;
                }
                directories[fs::absolute(root).lexically_normal().generic_string()] = directoryMembers(root, build);
                for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it)
                {
                    const auto path = it->path();
                    if (it->is_directory())
                    {
                        if (ignoredConfigureDirectory(path, build))
                            it.disable_recursion_pending();
                        else
                            directories[fs::absolute(path).lexically_normal().generic_string()] = directoryMembers(path, build);
                    }
                    else if (it->is_regular_file())
                    {
                        const auto extension = path.extension().string();
                        if (package || extension == ".lua" || extension == ".toml" || extension == ".csproj" || extension == ".vcxproj")
                            record(path);
                    }
                }
            };
            for (const auto& root : ws.m_configurationRoots)
                scan(root, false);
            for (const auto& root : ws.m_localPackageRoots)
                scan(root, true);
            record(ws.m_manifest);
            record(self);
            record(build / "axlum.options.json");
            FileSystem::writeIfChanged(
                build / "axlum.configure-inputs.json", Json{{"files", files}, {"directories", directories}, {"environment", configureEnvironment()}}.dump()
            );
        }

        bool configureInputsChanged(const fs::path& build)
        {
            const auto file = build / "axlum.configure-inputs.json";
            if (!fs::is_regular_file(file))
                return true;
            const auto saved = Json::parse(FileSystem::readAll(file));
            if (saved.at("environment") != configureEnvironment())
                return true;
            for (const auto& [path, version] : saved.at("files").items())
                if (fileVersion(path) != version)
                    return true;
            return false;
        }

        void saveOutputLedger(const BuildGraph& graph, const fs::path& buildDir)
        {
            // Remove only previously declared, now orphaned files inside this build directory.
            // Never delete a file reused as an input by the replacement graph.
            auto pathKey = [](const fs::path& path)
            {
                auto key = fs::absolute(path).lexically_normal().generic_string();
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
                return key;
            };
            std::set<std::string> outputs, inputs, outputPaths;
            for (const auto& action : graph.m_actions)
            {
                for (const auto& file : action.m_outputs)
                {
                    outputs.insert(pathKey(file));
                    outputPaths.insert(file.generic_string());
                }
                for (const auto& file : action.m_inputs)
                    inputs.insert(pathKey(file));
                for (const auto& file : action.m_orderOnly)
                    inputs.insert(pathKey(file));
            }
            const auto ledger = buildDir / "axlum.outputs";
            if (fs::is_regular_file(ledger))
            {
                std::ifstream previous(ledger);
                std::string file;
                while (std::getline(previous, file))
                {
                    const auto resolved = fs::weakly_canonical(file);
                    if (!outputs.contains(pathKey(file)) && !inputs.contains(pathKey(file)) &&
                        ab::FileSystem::isWithin(resolved, fs::weakly_canonical(buildDir)) && fs::is_regular_file(resolved))
                        fs::remove(fs::path(file));
                }
            }
            std::string outputList;
            for (const auto& file : outputPaths)
                outputList += file + "\n";
            ab::FileSystem::writeIfChanged(ledger, outputList);
        }

        void updateGeneratedSources(Workspace& ws, const fs::path& build)
        {
            std::map<std::string, std::vector<std::string>> oldOutputs, newOutputs;
            for (auto& target : ws.m_targets)
            {
                if (!target.m_features.contains("outputmanifest"))
                    continue;
                auto& old = oldOutputs[target.m_name];
                old = target.m_features.value("dynamic_outputs", std::vector<std::string>{});
                for (const auto& output : old)
                    std::erase(target.m_outputs, output);
                auto& outputs = newOutputs[target.m_name];
                for (const auto& item : target.m_features.at("outputmanifest"))
                {
                    auto path = item.get<std::string>();
                    if (path.starts_with("@build/"))
                        path.erase(0, 7);
                    const auto manifest = fs::absolute(build / path).lexically_normal();
                    if (!fs::is_regular_file(manifest))
                        continue;
                    const auto files = Json::parse(FileSystem::readAll(manifest));
                    if (!files.is_array())
                        throw std::runtime_error("output manifest must contain an array: " + manifest.string());
                    for (const auto& file : files)
                    {
                        const auto output = fs::absolute(manifest.parent_path() / file.get<std::string>()).lexically_normal();
                        if (!FileSystem::isWithin(output, manifest.parent_path()) || output == manifest.parent_path())
                            throw std::runtime_error("generated output escapes manifest directory: " + output.string());
                        const auto relative = output.lexically_relative(fs::absolute(build)).generic_string();
                        if (std::find(outputs.begin(), outputs.end(), relative) == outputs.end())
                            outputs.push_back(relative);
                        if (std::find(target.m_outputs.begin(), target.m_outputs.end(), relative) == target.m_outputs.end())
                            target.m_outputs.push_back(relative);
                    }
                }
                target.m_features["dynamic_outputs"] = outputs;
            }
            for (auto& target : ws.m_targets)
                for (const auto& spec : target.m_features.value("generatedsources", Json::array()))
                {
                    const auto producer = spec.at("producer").get<std::string>();
                    for (const auto& output : oldOutputs[producer])
                        std::erase(target.m_sources, "@build/" + output);
                    const auto extensions = spec.value("extensions", std::vector<std::string>{".cpp", ".c", ".cc", ".cxx", ".cs"});
                    for (const auto& output : newOutputs[producer])
                        if (std::find(extensions.begin(), extensions.end(), fs::path(output).extension().string()) != extensions.end() &&
                            std::find(target.m_sources.begin(), target.m_sources.end(), "@build/" + output) == target.m_sources.end())
                            target.m_sources.push_back("@build/" + output);
                }
        }

        void saveDynamicSources(const Workspace& ws, const BuildGraph& graph, const fs::path& build, const std::vector<std::string>& hostRoots = {})
        {
            Json manifests = Json::object();
            for (const auto& target : ws.m_targets)
                if (target.m_features.contains("outputmanifest"))
                    for (const auto& value : target.m_features.at("outputmanifest"))
                    {
                        auto path = value.get<std::string>();
                        if (path.starts_with("@build/"))
                            path.erase(0, 7);
                        const auto file = fs::absolute(build / path).lexically_normal();
                        manifests[target.m_name][file.generic_string()] = fs::is_regular_file(file) ? Json(FileSystem::readAll(file)) : Json(nullptr);
                    }
            FileSystem::writeIfChanged(
                build / "axlum.dynamic.json",
                Json{
                    {"manifests", manifests}, {"dependencies", graph.m_targetDependencies}, {"aliases", graph.m_targetAliases}, {"hostRoots", hostRoots}
                }.dump()
            );
        }

        // Called only from Ninja's build-time graph check, never from configure.
        bool prepareDynamicSources(
            const fs::path& build,
            const std::string& ninja,
            const std::vector<std::string>* requested = nullptr,
            const fs::path& executionDirectory = {}
        )
        {
            const auto file = build / "axlum.dynamic.json";
            if (!fs::is_regular_file(file))
                return false;
            const auto dynamic = Json::parse(FileSystem::readAll(file));
            const auto& dependencies = dynamic.at("dependencies");
            const auto& aliases = dynamic.at("aliases");
            std::set<std::string> closure;
            std::function<void(std::string)> visit = [&](std::string name)
            {
                if (aliases.contains(name))
                    name = aliases.at(name).get<std::string>();
                if (!closure.insert(name).second || !dependencies.contains(name))
                    return;
                for (const auto& dependency : dependencies.at(name))
                    visit(dependency.get<std::string>());
            };
            const auto* selected = std::getenv("AXLUM_BUILD_TARGETS");
            auto roots = requested ? *requested : selected && *selected ? Json::parse(selected).get<std::vector<std::string>>() : std::vector<std::string>{};
            if (roots.empty() || std::find(roots.begin(), roots.end(), "all") != roots.end())
                for (const auto& [name, value] : dependencies.items())
                    visit(name);
            else
                for (const auto& name : roots)
                    visit(name);
            std::vector<std::string> hostRoots;
            for (const auto& name : dynamic.at("hostRoots"))
                if (closure.contains(name.get<std::string>()))
                    hostRoots.push_back(name.get<std::string>());
            // Host generators may themselves consume dynamically discovered sources.
            // Replan those consumers before executing target-context generators.
            if (!hostRoots.empty() && prepareDynamicSources(build / "host", ninja, &hostRoots, build))
                return true;
            const auto executionRoot = executionDirectory.empty() ? build : executionDirectory;
            std::vector<std::string> command = {ninja, "-C", executionRoot.string(), "-f", requested ? "host-generators.ninja" : "generators.ninja"};
            if (const auto* jobs = std::getenv("AXLUM_BUILD_JOBS"); jobs && *jobs)
                command.insert(command.end(), {"-j", jobs});
            else
                command.insert(command.end(), {"-j", "8"});
            const auto start = command.size();
            for (const auto& [root, manifests] : dynamic.at("manifests").items())
                if (closure.contains(root))
                    command.push_back(root);
            if (command.size() == start)
                return false;
            std::string output;
            const auto status = Process::run(
                command,
                {},
                FileSystem::readEnvironment(executionRoot / "axlum.environment.json"),
                [&](std::string_view text)
                {
                    output += text;
                }
            );
            if (status != 0 || output.find("no work to do") == std::string::npos)
                std::cout << output;
            if (status != 0)
                throw std::runtime_error("dynamic source generation failed");
            for (const auto& [root, manifests] : dynamic.at("manifests").items())
                if (closure.contains(root))
                    for (const auto& [path, content] : manifests.items())
                        if (content.is_null() || !fs::is_regular_file(path) || FileSystem::readAll(path) != content.get<std::string>())
                            return true;
            return false;
        }
    } // namespace

    std::string Application::needArg(const std::vector<std::string>& args, std::size_t& i)
    {
        if (i + 1 >= args.size())
            throw std::runtime_error("missing value for option '" + args[i] + "'");
        return args[++i];
    }

    void Application::configureCli(CLI::App& app, Arguments& options)
    {
        app.require_subcommand(1, 1);
        app.set_version_flag("-v,--version", "AxlumBuild 0.1.0");
        app.set_help_all_flag("--help-all", "Show all commands and their options");
        auto* configure = app.add_subcommand("configure", "Load a manifest and generate Ninja + compile_commands.json");
        configure->add_option("-f,--file", options.m_file, "Lua or legacy TOML manifest")->check(CLI::ExistingFile)->capture_default_str();
        configure->add_option("-B,--build-dir", options.m_buildDir, "Build output directory")->capture_default_str();
        configure->add_option("--config", options.m_config, "Declared build configuration")->capture_default_str();
        configure->add_option("-S,--source-dir", options.m_sourceDir, "Workspace source directory")->check(CLI::ExistingDirectory);
        configure->add_option("-G,--generator", options.m_generator, "Build generator")->check(CLI::IsMember({"Ninja"}));
        configure->add_option("--arch", options.m_architecture, "Target architecture: x64 or arm64");
        configure->add_option("--platform", options.m_platform, "Target platform: windows/linux/macos/android/ios/ios-simulator");
        configure->add_option("--preset", options.m_preset, "Workspace preset name");
        configure->add_option("--compiler", options.m_compiler, "Native compiler toolchain")
            ->check(CLI::IsMember({"auto", "gcc", "clang", "msvc"}))
            ->capture_default_str();
        configure->add_option("--toolchain", options.m_toolchain, "Rhodium toolchain profile ID (see doctor)");
        configure->callback(
            [&options]
            {
                options.m_command = "configure";
            }
        );
        configure->add_option("-j,--jobs,--parallel", options.m_execution.m_jobs, "Generator bootstrap parallelism")->check(CLI::Range(1, 65535));
        configure->add_option("--option", options.m_optionValues, "Project option NAME=VALUE (repeatable)");
        auto* optionCommand = app.add_subcommand("options", "List typed project options");
        optionCommand->add_option("-f,--file", options.m_file);
        optionCommand->add_option("-S,--source-dir", options.m_sourceDir);
        optionCommand->add_option("-B,--build-dir", options.m_buildDir);
        optionCommand->add_flag("--json", options.m_json);
        optionCommand->callback(
            [&options]
            {
                options.m_command = "options";
            }
        );
        auto* testCommand = app.add_subcommand("test", "Build and run declared tests");
        testCommand->add_option("-B,--build-dir", options.m_buildDir);
        testCommand->add_option("-R,--filter", options.m_testPattern);
        testCommand->add_option("-L,--label", options.m_testLabel);
        testCommand->add_flag("--no-build", options.m_noBuild);
        testCommand->add_option("-j,--jobs,--parallel", options.m_execution.m_jobs);
        testCommand->callback(
            [&options]
            {
                options.m_command = "test";
            }
        );
        configure->add_flag("--locked", options.m_locked, "Require package inputs to match the Rhodium lockfile");
        for (const auto& [name, description] : std::vector<std::pair<std::string, std::string>>{
                 {"build", "Build all targets or a selected target using Ninja"},
                 {"clean", "Clean all outputs or selected targets using Ninja"},
                 {"rebuild", "Clean selected target closures and force native/.NET rebuilds"},
                 {"graph", "Print the configured build action graph"}
             })
        {
            auto* command = app.add_subcommand(name, description);
            command->add_option("-B,--build-dir", options.m_buildDir, "Configured build directory")->capture_default_str();
            command->add_option("--config", options.m_requestedConfig, "Validate the configured build configuration");
            if (name != "graph")
            {
                command->add_option("-t,--target", options.m_targets, "Targets to build/clean (repeatable)");
                command->add_option("targets", options.m_targets, "Optional target names");
                command->add_option("--logfile", options.m_execution.m_logfile, "Stream merged command output to a file");
                command->add_option("--report", options.m_execution.m_report, "JSON report (default: <B>/axlum.last-build.json)");
                command->add_option("--trace", options.m_execution.m_trace, "Chrome Trace JSON file");
                command->add_flag("--timing", options.m_execution.m_timing, "Print elapsed time and completed action count");
                if (name != "clean")
                {
                    command->add_option("-j,--jobs,--parallel", options.m_execution.m_jobs, "Parallel Ninja jobs")->check(CLI::Range(1, 65535));
                    command->add_flag("-v,--verbose", options.m_execution.m_verbose, "Show executed commands");
                    command->add_flag("--why-rebuild", options.m_execution.m_explain, "Explain Ninja invalidation decisions");
                }
                if (name == "build")
                {
                    command->add_flag("-n,--dry-run", options.m_execution.m_dryRun, "Show the incremental build plan without executing actions");
                    command->add_flag("--clean-first", options.m_cleanFirst, "Clean selected targets before building");
                }
                if (name == "clean")
                {
                    command->add_option("--mode", options.m_cleaning.m_mode, "build/intermediates/publish/cache/all")
                        ->check(CLI::IsMember({"build", "intermediates", "publish", "cache", "all"}));
                    command->add_flag("--all", options.m_cleanAll, "Clean build outputs, publications and configuration cache");
                    command->add_flag_function(
                        "--no-dependencies",
                        [&options](std::int64_t)
                        {
                            options.m_cleaning.m_dependencies = false;
                        }
                    );
                    command->add_flag("-n,--dry-run", options.m_cleaning.m_dryRun, "List owned files without removing them");
                }
            }
            command->callback(
                [&options, name]
                {
                    options.m_command = name;
                }
            );
        }
        for (const auto& name : {"ide", "matrix"})
        {
            auto* command =
                app.add_subcommand(name, std::string(name) == "ide" ? "Generate IDE projects from cached configuration" : "Build existing configured entries");
            command->add_option("-f,--file", options.m_file, "Workspace manifest")->check(CLI::ExistingFile);
            command->add_option("-B,--build-dir", options.m_buildDir, "Configured build directory or root of configured entries");
            command->add_option("--arch", options.m_architecture, "Architecture (IDE default)");
            command->add_option("--platform", options.m_platform, "Target platform");
            command->add_option("--toolchain", options.m_toolchain, "Toolchain profile ID");
            command->add_option("--preset", options.m_presets, "Preset names (repeatable); matrix defaults to all");
            command->add_option("--option", options.m_optionValues, "Project option NAME=VALUE (repeatable)");
            command->add_flag("--locked", options.m_locked, "Validate package lock identity");
            if (std::string(name) == "ide")
            {
                command->add_option("--format", options.m_format, "vscode/vs/desktop/xcode/android-studio/all")
                    ->check(CLI::IsMember({"vscode", "vs", "desktop", "xcode", "android-studio", "all"}));
                command->add_option("-o,--output", options.m_output, "Generated IDE project directory (default: <B>/ide)");
            }
            else
            {
                command->add_flag("--configure-only", options.m_configureOnly, "Validate cached entries without building");
                command->add_option("-j,--jobs", options.m_execution.m_jobs, "Jobs per matrix entry")->check(CLI::Range(1, 65535));
                command->add_flag("-v,--verbose", options.m_execution.m_verbose, "Show entry build commands");
                command->add_flag("--why-rebuild", options.m_execution.m_explain, "Explain entry invalidation");
                command->add_flag("--timing", options.m_execution.m_timing, "Print each entry timing");
                command->add_option("-t,--target", options.m_target, "Target to build in each entry");
            }
            command->callback(
                [&options, name]
                {
                    options.m_command = name;
                }
            );
        }
        for (const std::string commandName : {"publish", "install"})
        {
            auto* publish = app.add_subcommand(commandName, "Install native artifacts and publish .NET targets using the official SDK");
            publish->add_option("-B,--build-dir", options.m_buildDir, "Configured build directory");
            publish->add_option("-t,--target", options.m_targets, "Targets (repeatable; defaults to all installable targets)");
            publish->add_option("--prefix", options.m_installation.m_prefix, "Installation prefix, including outside the build root");
            publish->add_option("--component", options.m_installation.m_component, "runtime/development/symbols/all")
                ->check(CLI::IsMember({"runtime", "development", "symbols", "all"}));
            publish->add_option("--config", options.m_requestedConfig, "Validate the configured build configuration");
            publish->add_flag("--no-build", options.m_installation.m_noBuild, "Skip native/graph build preparation");
            publish->add_flag("--build-first", options.m_buildFirst, "Build before installation (install otherwise uses existing outputs)");
            publish->add_flag("-n,--dry-run", options.m_installation.m_dryRun, "Preview installation without building or copying");
            publish->add_option("-r,--rid", options.m_publication.m_rid, "Desktop .NET runtime identifier");
            publish->add_option("-o,--output", options.m_publication.m_directory, "Publication directory under the build root");
            publish->add_option("--native-lib", options.m_publication.m_nativeLibrary, "NativeAOT library kind")->check(CLI::IsMember({"Shared"}));
            auto toggle = [&](const char* on, const char* off, std::optional<bool>& destination)
            {
                auto* yes = publish->add_flag_function(
                    on,
                    [&destination](std::int64_t)
                    {
                        destination = true;
                    }
                );
                auto* no = publish->add_flag_function(
                    off,
                    [&destination](std::int64_t)
                    {
                        destination = false;
                    }
                );
                yes->excludes(no);
            };
            toggle("--aot", "--no-aot", options.m_publication.m_aot);
            toggle("--self-contained", "--framework-dependent", options.m_publication.m_selfContained);
            toggle("--trimmed", "--no-trim", options.m_publication.m_trimmed);
            toggle("--single-file", "--no-single-file", options.m_publication.m_singleFile);
            toggle("--strip-symbols", "--keep-symbols", options.m_publication.m_stripSymbols);
            publish->add_option("-j,--jobs", options.m_execution.m_jobs, "Prepare/publish parallelism")->check(CLI::Range(1, 65535));
            publish->add_flag("-v,--verbose", options.m_execution.m_verbose, "Detailed SDK output");
            publish->add_flag("--why-rebuild", options.m_execution.m_explain, "Explain preparation invalidation");
            publish->add_flag("--timing", options.m_execution.m_timing, "Print publication timing");
            publish->add_option("--logfile", options.m_execution.m_logfile, "Publication command logfile");
            publish->add_option("--report", options.m_execution.m_report, "Publication timing/result JSON");
            publish->add_option("--trace", options.m_execution.m_trace, "Chrome Trace JSON");
            publish->callback(
                [&options, commandName]
                {
                    options.m_command = commandName;
                    options.m_installation.m_install = commandName == "install";
                    if (commandName == "install" && !options.m_buildFirst)
                        options.m_installation.m_noBuild = true;
                }
            );
        }
        auto* run = app.add_subcommand("run", "Build and run a native or managed executable; pass arguments after --");
        run->add_option("-B,--build-dir", options.m_buildDir, "Configured build directory");
        run->add_option("target,-t,--target", options.m_target, "Executable target; optional if exactly one exists");
        run->add_option("--config", options.m_requestedConfig, "Validate the configured build configuration");
        run->add_flag("--no-build", options.m_noBuild, "Run an existing output");
        run->add_option("--cwd", options.m_workingDirectory, "Process working directory")->check(CLI::ExistingDirectory);
        run->add_option("--env", options.m_environment, "NAME=VALUE process environment override (repeatable)");
        run->add_option("-j,--jobs,--parallel", options.m_execution.m_jobs, "Build parallelism")->check(CLI::Range(1, 65535));
        run->add_flag("-v,--verbose", options.m_execution.m_verbose, "Print commands");
        run->callback(
            [&options]
            {
                options.m_command = "run";
            }
        );
        for (const std::string name : {"targets", "info"})
        {
            auto* command =
                app.add_subcommand(name, name == "targets" ? "List configured targets and support limits" : "Show configured workspace and artifacts");
            command->add_option("-B,--build-dir", options.m_buildDir, "Configured build directory");
            command->add_flag("--json", options.m_json, "Machine-readable JSON");
            command->callback(
                [&options, name]
                {
                    options.m_command = name;
                }
            );
        }
        auto* uninstall = app.add_subcommand("uninstall", "Remove unchanged files recorded in an installation manifest");
        uninstall->add_option("--prefix", options.m_installation.m_prefix, "Installation prefix")->required();
        uninstall->add_option("-t,--target", options.m_targets, "Installed targets to remove (repeatable)");
        uninstall->add_option("--component", options.m_installation.m_component, "runtime/development/symbols/all")
            ->check(CLI::IsMember({"runtime", "development", "symbols", "all"}));
        uninstall->add_flag("-n,--dry-run", options.m_installation.m_dryRun, "Preview removals");
        uninstall->callback(
            [&options]
            {
                options.m_command = "uninstall";
            }
        );
        auto* doctor = app.add_subcommand("doctor", "Report SDKs/toolchains detected by Rhodium");
        doctor->add_option("--workspace", options.m_sourceDir, "Workspace whose Rhodium profile overrides are loaded")->check(CLI::ExistingDirectory);
        doctor->add_flag("--json", options.m_json, "Machine-readable Rhodium SDK inventory");
        doctor->add_flag("--strict", options.m_strict, "Fail when no supported complete toolchain is detected");
        doctor->callback(
            [&options]
            {
                options.m_command = "doctor";
            }
        );
    }

    std::map<std::string, std::string> Application::readState(const fs::path& dir)
    {
        const fs::path file = dir / "axlum.state";
        if (!fs::is_regular_file(file))
            throw std::runtime_error("not configured: " + file.string() + "; run 'axlum-build configure -B " + dir.string() + "' first");
        std::map<std::string, std::string> state;
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq != std::string::npos)
                state[line.substr(0, eq)] = line.substr(eq + 1);
        }
        if (!state.contains("manifest") || !state.contains("config"))
            throw std::runtime_error("invalid axlum.state, reconfigure");
        return state;
    }

    void Application::printGraph(const ab::BuildGraph& graph)
    {
        for (const auto& action : graph.m_actions)
        {
            std::cout << action.m_name << '\n';
            for (const auto& input : action.m_inputs)
                std::cout << "    <- " << input.string() << '\n';
            for (const auto& output : action.m_outputs)
                std::cout << "    -> " << output.string() << '\n';
        }
    }

    fs::path Application::currentExecutable(const std::string& arg0)
    {
        const auto found = ab::Process::findExecutable(arg0);
        if (!found.empty())
            return fs::absolute(found).lexically_normal();
        if (fs::is_regular_file(arg0))
            return fs::absolute(arg0).lexically_normal();
        throw std::runtime_error("cannot resolve current AxlumBuild executable: " + arg0);
    }

    ab::IDEContext Application::configure(Arguments opt, const fs::path& buildDir, const fs::path& self, bool automatic, bool skipEmpty)
    {
        const auto started = std::chrono::steady_clock::now();
        auto progress = [&](const std::string& stage)
        {
            const auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() / 1000.0;
            std::cout << "[configure " << seconds << "s] " << stage << std::endl;
        };
        progress("Loading configuration: " + opt.m_file);
        if (fs::is_regular_file(buildDir / "axlum.environment.json"))
        {
            const auto previous = ab::FileSystem::readEnvironment(buildDir / "axlum.environment.json");
            for (const auto* key : {"ANDROID_SDK_ROOT", "ANDROID_NDK_ROOT", "RHODIUM_ANDROID_API"})
                if (!std::getenv(key) && previous.contains(key) && !previous.at(key).empty())
                {
#ifdef _WIN32
                    _putenv_s(key, previous.at(key).c_str());
#else
                    setenv(key, previous.at(key).c_str(), 0);
#endif
                }
        }
        auto projectOptions = DslFeatures::readOptions(buildDir);
        const auto savedOptions = projectOptions;
        if (!opt.m_preset.empty())
        {
            const auto metadata = ab::ManifestLoader::loadManifest(opt.m_file, ab::Configuration{"__inspect"}, buildDir, opt.m_architecture, opt.m_platform);
            const auto preset = std::find_if(
                metadata.m_presets.begin(),
                metadata.m_presets.end(),
                [&](const auto& p)
                {
                    return p.m_name == opt.m_preset;
                }
            );
            if (preset == metadata.m_presets.end())
                throw std::runtime_error("unknown preset: " + opt.m_preset);
            opt.m_config = preset->m_configuration;
            opt.m_architecture = preset->m_architecture;
            opt.m_platform = preset->m_platform;
            opt.m_toolchain = preset->m_toolchain;
            opt.m_compiler = preset->m_compiler;
            if (!automatic)
                for (const auto& [key, value] : preset->m_options)
                    projectOptions[key] = value;
        }
        for (const auto& assignment : opt.m_optionValues)
        {
            auto pos = assignment.find('=');
            if (pos == std::string::npos || pos == 0)
                throw std::runtime_error("--option requires NAME=VALUE");
            projectOptions[assignment.substr(0, pos)] = assignment.substr(pos + 1);
        }

        // Restore previous persisted choices if validation/configuration fails.
        struct OptionGuard
        {
            fs::path file;
            nlohmann::json saved;
            bool committed = false;

            ~OptionGuard()
            {
                if (!committed)
                {
                    try
                    {
                        FileSystem::writeIfChanged(file, saved.dump(2));
                    }
                    catch (...)
                    {
                    }
                }
            }
        } guard{buildDir / "axlum.options.json", savedOptions};

        FileSystem::writeIfChanged(guard.file, projectOptions.dump(2));
        opt.m_architecture = ab::BuildModel::normalizeArchitecture(opt.m_architecture);
        const auto config = ab::BuildModel::parseConfiguration(opt.m_config);
        if (fs::absolute(opt.m_file).lexically_normal().parent_path() == buildDir)
            throw std::runtime_error("build directory cannot equal workspace root");
        auto ws = ab::ManifestLoader::loadManifest(opt.m_file, config, buildDir, opt.m_architecture, opt.m_platform);
        for (const auto& [key, value] : projectOptions.items())
            if (!ws.m_features.value("options", nlohmann::json::object()).contains(key))
                throw std::runtime_error("unknown project option: " + key);
        if (ab::FileSystem::isWithin(buildDir, ws.m_root) && buildDir == ws.m_root)
            throw std::runtime_error("build directory cannot equal workspace root");
        if (ws.m_targets.empty())
        {
            if (skipEmpty)
            {
                ab::IDEContext empty;
                empty.m_workspace = std::move(ws);
                empty.m_configuration = config;
                empty.m_buildDirectory = buildDir;
                return empty;
            }
            std::string reasons;
            for (const auto& [name, reason] : ws.m_disabledTargets)
                reasons += "\n  " + name + ": " + reason;
            throw std::runtime_error("no targets support this configuration" + reasons);
        }
        progress("Detecting target toolchain (" + opt.m_compiler + ", " + opt.m_architecture + ")");
        auto tc = ab::ToolchainDiscovery::discoverToolchain(opt.m_compiler, ws.m_root, opt.m_toolchain, opt.m_architecture, opt.m_platform);
        if (!ws.m_architectures.empty() && std::find(ws.m_architectures.begin(), ws.m_architectures.end(), opt.m_architecture) == ws.m_architectures.end())
            throw std::runtime_error("architecture is not declared by workspace: " + opt.m_architecture);
        ToolchainDiscovery::reuseSearchPath(tc, buildDir);
        const auto fingerprint = ab::ToolchainDiscovery::toolchainFingerprint(tc, config);
        if (fs::exists(buildDir / "axlum.state"))
        {
            const auto old = readState(buildDir);
            if (old.at("manifest") != ws.m_manifest.string() || old.at("config") != ab::BuildModel::toString(config) || old.at("target") != tc.m_target ||
                (!automatic && (!old.contains("fingerprint") || old.at("fingerprint") != fingerprint)))
            {
                throw std::runtime_error(
                    "incompatible configuration/toolchain in '" + buildDir.string() +
                    "'; use a separate -B directory for each config, target triple and toolchain"
                );
            }
        }
        tc.m_environment["AXLUM_TOOLCHAIN_FINGERPRINT"] = fingerprint;
        BuildGraph hostGraph;
        std::map<std::string, fs::path> hostArtifacts;
        std::map<std::string, std::vector<fs::path>> hostOutputLists;
        std::vector<std::string> hostRoots;
        for (const auto& target : ws.m_targets)
            if (target.m_hostTool)
                hostRoots.push_back(target.m_name);
        if (!hostRoots.empty())
        {
            const auto hostBuild = buildDir / "host";
            FileSystem::writeIfChanged(hostBuild / "axlum.options.json", projectOptions.dump(2));
            const auto hostPlatform = ToolchainDiscovery::hostTriple().find("windows") != std::string::npos ? "windows"
                                      : ToolchainDiscovery::hostTriple().find("apple") != std::string::npos ? "macos"
                                                                                                            : "linux";
            const auto hostArch =
                ToolchainDiscovery::hostTriple().find("aarch64") != std::string::npos || ToolchainDiscovery::hostTriple().find("arm64") != std::string::npos
                    ? "arm64"
                    : "x64";
            progress("Planning host tools and resolving their dependencies");
            auto hostWs = ManifestLoader::loadManifest(opt.m_file, config, hostBuild, hostArch, hostPlatform);
            DslFeatures::selectClosure(hostWs, hostRoots);
            auto hostTc = ToolchainDiscovery::discoverToolchain("auto", hostWs.m_root, {}, hostArch, hostPlatform);
            ToolchainDiscovery::reuseSearchPath(hostTc, hostBuild);
            DslFeatures::resolveToolPackages(hostWs, hostBuild, config, opt.m_locked);
            const auto hostStorage = tc.m_host != tc.m_target ? hostBuild / "packages" : fs::path{};
            // Isolated cross-build package storage has its own resolver state.
            const auto hostPackageState = hostBuild / "axlum.packages";
            const auto hostFingerprint = PackageManager::packageInputsFingerprint(hostWs, hostTc, config, hostBuild);
            const bool cachedHost =
                hostStorage.empty() && !opt.m_locked && fs::is_regular_file(hostPackageState) && FileSystem::readAll(hostPackageState) == hostFingerprint;
            if (cachedHost)
            {
                auto resolved = hostWs;
                try
                {
                    PackageManager::resolvePackages(resolved, hostTc, config, opt.m_locked, false);
                    hostWs = std::move(resolved);
                }
                catch (const std::exception&)
                {
                    PackageManager::resolvePackages(hostWs, hostTc, config, opt.m_locked, true, hostStorage);
                }
            }
            else
                PackageManager::resolvePackages(hostWs, hostTc, config, opt.m_locked, true, hostStorage);
            FileSystem::writeIfChanged(hostPackageState, PackageManager::packageInputsFingerprint(hostWs, hostTc, config, hostBuild));
            DslFeatures::prepare(hostWs, hostBuild, config, self);
            hostGraph = BuildPlanner::planBuild(hostWs, hostTc, hostBuild, config, self);
            for (auto& action : hostGraph.m_actions)
            {
                action.m_environmentIsolated = true;
                auto environment = hostTc.m_environment;
                for (const auto& [key, value] : action.m_environment)
                    environment[key] = value;
                action.m_environment = std::move(environment);
            }
            // Host and target actions own the same host outputs. Use the same
            // environment paths and Ninja databases in both scheduling views.
            // Every host action already carries its isolated host environment.
            // Do not transiently overwrite the main environment file with host
            // values: its timestamp is an implicit input to target actions.
            hostGraph.m_environment = tc.m_environment;
            NinjaBackend::generateNinja(hostGraph, buildDir, self, false, "host-generators.ninja");
            saveDynamicSources(hostWs, hostGraph, hostBuild);
            ConfigurationCache::save({hostWs, hostTc, hostGraph, hostBuild, config, "host"});
            for (auto& action : hostGraph.m_actions)
            {
                action.m_environmentIsolated = true;
                action.m_owner = "host__" + action.m_owner;
                auto environment = hostTc.m_environment;
                for (const auto& [key, value] : action.m_environment)
                    environment[key] = value;
                action.m_environment = std::move(environment);
            }
            for (auto& name : hostRoots)
            {
                hostArtifacts[name] = hostGraph.m_targetOutputs.at(name).front();
                hostOutputLists[name] = hostGraph.m_targetOutputs.at(name);
            }
            hostGraph.m_targetOutputs.clear();
            hostGraph.m_targetKinds.clear();
            hostGraph.m_targetDependencies.clear();
            hostGraph.m_targetAliases.clear();
            for (auto& target : ws.m_targets)
                if (hostArtifacts.contains(target.m_name))
                {
                    target.m_hostArtifact = hostArtifacts.at(target.m_name);
                    for (const auto& output : hostOutputLists.at(target.m_name))
                        target.m_features["host_outputs"].push_back(output.generic_string());
                    target.m_sources.clear();
                    target.m_publicDependencies.clear();
                    target.m_privateDependencies.clear();
                    target.m_interfaceDependencies.clear();
                    target.m_packages.clear();
                    target.m_publicPackages.clear();
                }
        }
        progress("Resolving project dependencies (reusing cached artifacts when valid)");
        DslFeatures::resolveToolPackages(ws, buildDir, config, opt.m_locked);
        const auto packageFingerprint = ab::PackageManager::packageInputsFingerprint(ws, tc, config, buildDir);
        const auto packageState = buildDir / "axlum.packages";
        const bool restore = opt.m_locked || !fs::is_regular_file(packageState) || ab::FileSystem::readAll(packageState) != packageFingerprint;
        if (restore)
            ab::PackageManager::resolvePackages(ws, tc, config, opt.m_locked);
        else
        {
            // Missing/corrupted cached artifact metadata must be repaired by the package manager.
            auto resolved = ws;
            try
            {
                ab::PackageManager::resolvePackages(resolved, tc, config, opt.m_locked, false);
                ws = std::move(resolved);
            }
            catch (const std::exception&)
            {
                ab::PackageManager::resolvePackages(ws, tc, config, opt.m_locked);
            }
        }
        progress("Planning targets and generated sources");
        DslFeatures::prepare(ws, buildDir, config, self);
        std::vector<std::string> dynamicRoots;
        for (auto& target : ws.m_targets)
            if (target.m_features.contains("outputmanifest"))
                dynamicRoots.push_back(target.m_name);
        ab::ManagedProjectGenerator::prepareManagedProjects(ws, buildDir, self);
        auto graph = ab::BuildPlanner::planBuild(ws, tc, buildDir, config, self, hostGraph.m_actions.empty() ? nullptr : &hostGraph);
        if (!dynamicRoots.empty())
            ab::NinjaBackend::generateNinja(graph, buildDir, self, false, "generators.ninja");
        saveDynamicSources(ws, graph, buildDir, hostRoots);
        progress("Writing build graph and editor metadata");
        ab::NinjaBackend::generateNinja(graph, buildDir, self);
        ab::NinjaBackend::generateCompileCommands(graph, buildDir);
        ab::IDEGenerator::writeArtifactIndex(graph, buildDir);
        std::vector<std::string> runTargets;
        for (const auto& target : ws.m_targets)
            if (!target.m_hostTool && (target.m_kind == TargetKind::Executable || target.m_managedExecutable))
                runTargets.push_back(target.m_name);
        FileSystem::writeIfChanged(buildDir / "axlum.run-targets.json", Json(runTargets).dump());
        saveOutputLedger(graph, buildDir);
        ab::FileSystem::writeIfChanged(packageState, ab::PackageManager::packageInputsFingerprint(ws, tc, config, buildDir));
        ab::FileSystem::writeIfChanged(
            buildDir / "axlum.state",
            "manifest=" + ws.m_manifest.string() + "\nconfig=" + ab::BuildModel::toString(config) + "\ncompiler=" + opt.m_compiler + "\ntarget=" + tc.m_target +
                "\nfingerprint=" + fingerprint + "\ntoolchain=" + tc.m_id + "\nprofile=" + opt.m_toolchain + "\nlocked=" + (opt.m_locked ? "1" : "0") +
                "\ndotnet=" + tc.m_dotnet + "\nninja=" + tc.m_ninja + "\nschema=2\narchitecture=" + opt.m_architecture + "\nplatform=" + opt.m_platform +
                "\npreset=" + opt.m_preset + "\n"
        );
        guard.committed = true;
        FileSystem::writeIfChanged(buildDir / "axlum.option-schema.json", ws.m_features.value("options", nlohmann::json::object()).dump(2));
        saveConfigureInputs(ws, buildDir, self);
        progress("Configuration complete");
        if (!automatic)
            std::cout << "Configured " << ws.m_name << " (" << tc.m_target << ", " << ab::BuildModel::toString(config) << ")\n"
                      << "Build graph: " << (buildDir / "build.ninja") << "\n"
                      << "Actions: " << graph.m_actions.size() << "\n";
        IDEContext context{std::move(ws), std::move(tc), graph, buildDir, config, BuildModel::toString(config) + "-" + opt.m_architecture};
        ConfigurationCache::save(context);
        return context;
    }

    ab::IDEContext Application::refresh(const fs::path& buildDir, const fs::path& self)
    {
        // Build-time discovery updates only the cached graph. Never evaluate Lua,
        // discover SDKs, restore packages, or change configured options here.
        auto context = ConfigurationCache::load(buildDir);
        BuildGraph hostGraph;
        std::vector<std::string> hostRoots;
        for (const auto& target : context.m_workspace.m_targets)
            if (target.m_hostTool)
                hostRoots.push_back(target.m_name);
        if (!hostRoots.empty())
        {
            auto host = ConfigurationCache::load(buildDir / "host");
            updateGeneratedSources(host.m_workspace, host.m_buildDirectory);
            host.m_graph = BuildPlanner::planBuild(host.m_workspace, host.m_toolchain, host.m_buildDirectory, host.m_configuration, self);
            for (auto& action : host.m_graph.m_actions)
            {
                action.m_environmentIsolated = true;
                auto environment = host.m_toolchain.m_environment;
                for (const auto& [key, value] : action.m_environment)
                    environment[key] = value;
                action.m_environment = std::move(environment);
            }
            host.m_graph.m_environment = context.m_toolchain.m_environment;
            NinjaBackend::generateNinja(host.m_graph, buildDir, self, false, "host-generators.ninja");
            saveDynamicSources(host.m_workspace, host.m_graph, host.m_buildDirectory);
            ConfigurationCache::save(host);
            hostGraph = host.m_graph;
            for (auto& action : hostGraph.m_actions)
                action.m_owner = "host__" + action.m_owner;
            hostGraph.m_targetOutputs.clear();
            hostGraph.m_targetKinds.clear();
            hostGraph.m_targetDependencies.clear();
            hostGraph.m_targetAliases.clear();
        }
        updateGeneratedSources(context.m_workspace, buildDir);
        ManagedProjectGenerator::prepareManagedProjects(context.m_workspace, buildDir, self);
        context.m_graph = BuildPlanner::planBuild(
            context.m_workspace, context.m_toolchain, buildDir, context.m_configuration, self, hostGraph.m_actions.empty() ? nullptr : &hostGraph
        );
        NinjaBackend::generateNinja(context.m_graph, buildDir, self, false, "generators.ninja");
        saveDynamicSources(context.m_workspace, context.m_graph, buildDir, hostRoots);
        NinjaBackend::generateNinja(context.m_graph, buildDir, self);
        NinjaBackend::generateCompileCommands(context.m_graph, buildDir);
        IDEGenerator::writeArtifactIndex(context.m_graph, buildDir);
        saveOutputLedger(context.m_graph, buildDir);
        ConfigurationCache::save(context);
        return context;
    }

    int Application::configureEntries(const Arguments& options, const fs::path& self)
    {
        const auto root = fs::absolute(options.m_buildDir).lexically_normal();
        if (!options.m_optionValues.empty() || options.m_locked || !options.m_toolchain.empty() || !options.m_platform.empty())
            throw std::runtime_error("configuration options belong to configure; ide/matrix reuse configured entries");
        std::vector<IDEContext> contexts;
        if (fs::is_regular_file(root / "axlum.context.json"))
            contexts.push_back(ConfigurationCache::load(root));
        else if (fs::is_directory(root))
        {
            std::vector<fs::path> entries;
            for (const auto& entry : fs::directory_iterator(root))
                if (entry.is_directory() && entry.path().filename() != "host" && fs::is_regular_file(entry.path() / "axlum.context.json"))
                    entries.push_back(entry.path());
            std::sort(entries.begin(), entries.end());
            for (const auto& entry : entries)
            {
                if (!options.m_presets.empty() &&
                    std::find(options.m_presets.begin(), options.m_presets.end(), entry.filename().string()) == options.m_presets.end())
                    continue;
                contexts.push_back(ConfigurationCache::load(entry));
            }
        }
        if (contexts.empty())
            throw std::runtime_error("no configured entries; run configure -B " + root.string() + " first");
        for (const auto& preset : options.m_presets)
            if (std::none_of(
                    contexts.begin(),
                    contexts.end(),
                    [&](const auto& c)
                    {
                        return c.m_label == preset || c.m_buildDirectory.filename() == preset;
                    }
                ))
                throw std::runtime_error("preset is not configured: " + preset);
        if (options.m_command == "matrix")
        {
            if (options.m_configureOnly)
                return 0;
            for (const auto& context : contexts)
            {
                const auto rc = BuildExecutor::executeNinja(
                    context.m_buildDirectory,
                    context.m_toolchain.m_ninja,
                    "build",
                    options.m_target.empty() ? std::vector<std::string>{} : std::vector<std::string>{options.m_target},
                    options.m_execution
                );
                if (rc)
                    return rc;
            }
            return 0;
        }
        const auto output = options.m_output.empty() ? root / "ide" : fs::absolute(options.m_output);
        // The editor explicitly configures the selected entry, then regenerates
        // projects separately using the updated cache.
        for (auto& context : contexts)
        {
            const auto state = readState(context.m_buildDirectory);
            context.m_regenerateArguments = {
                "configure",
                "-f",
                context.m_workspace.m_manifest.string(),
                "-B",
                context.m_buildDirectory.string(),
                "--config",
                BuildModel::toString(context.m_configuration),
                "--arch",
                context.m_toolchain.m_architecture,
                "--compiler",
                state.at("compiler")
            };
            for (const auto& [key, flag] :
                 std::vector<std::pair<std::string, std::string>>{{"profile", "--toolchain"}, {"platform", "--platform"}, {"preset", "--preset"}})
                if (state.contains(key) && !state.at(key).empty())
                    context.m_regenerateArguments.insert(context.m_regenerateArguments.end(), {flag, state.at(key)});
            context.m_regenerateDirectory = context.m_workspace.m_root;
        }
        contexts.front().m_regenerateIDEArguments = {"ide", "-B", root.string(), "-o", output.string(), "--format", options.m_format};
        for (const auto& preset : options.m_presets)
            contexts.front().m_regenerateIDEArguments.insert(contexts.front().m_regenerateIDEArguments.end(), {"--preset", preset});
        IDEGenerator::generateIDE(contexts, output, self, options.m_format);
        std::cout << "Generated " << options.m_format << " projects from cached configuration: " << output << '\n';
        return 0;
    }

    int Application::doctor(const Arguments& options)
    {
        const auto report =
            nlohmann::json::parse(ab::ToolchainDiscovery::sdkReport(options.m_sourceDir.empty() ? fs::current_path() : fs::absolute(options.m_sourceDir)));
        bool complete = false;
        for (const auto& profile : report.at("profiles"))
            complete |= profile.at("complete").get<bool>() && profile.at("supported").get<bool>();
        if (options.m_json)
            std::cout << report.dump(2) << '\n';
        else
        {
            std::cout << "SDK provider: Rhodium\nHost: " << report.at("host").get<std::string>() << '\n';
            for (const auto& profile : report.at("profiles"))
            {
                std::cout << "  " << profile.at("id").get<std::string>() << " [" << profile.at("platform").get<std::string>() << '/'
                          << profile.at("architecture").get<std::string>() << "] " << (profile.at("complete").get<bool>() ? "complete" : "incomplete") << '\n';
                for (const auto& key :
                     {"cxxCompiler", "compilerVersion", "sdkKind", "sdkVersion", "sdkRoot", "sysroot", "androidSdkRoot", "androidNdkRoot", "jdkRoot"})
                    if (!profile.at(key).get<std::string>().empty())
                        std::cout << "    " << key << ": " << profile.at(key).get<std::string>() << '\n';
                for (const auto& item : profile.at("missing"))
                    std::cout << "    missing: " << item.get<std::string>() << '\n';
            }
        }
        return options.m_strict && !complete ? 1 : 0;
    }

    int Application::runWrapped(const std::vector<std::string>& args)
    {
        fs::path cwd;
        fs::path msvcJson, depfile, environmentFile;
        std::vector<fs::path> outputs;
        std::size_t i = 0;
        for (; i < args.size(); ++i)
        {
            if (args[i] == "--")
            {
                ++i;
                break;
            }
            if (args[i] == "--cwd")
                cwd = needArg(args, i);
            else if (args[i] == "--out")
                outputs.emplace_back(needArg(args, i));
            else if (args[i] == "--msvc-json")
                msvcJson = needArg(args, i);
            else if (args[i] == "--depfile")
                depfile = needArg(args, i);
            else if (args[i] == "--env-file")
                environmentFile = needArg(args, i);
            else
                throw std::runtime_error("unknown __run parameter: " + args[i]);
        }
        if (i == args.size())
            throw std::runtime_error("__run missing executable after --");
        std::vector<std::string> cmd(args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
        for (const auto& o : outputs)
            fs::create_directories(o.parent_path());
        auto environment = environmentFile.empty() ? std::map<std::string, std::string>{} : ab::FileSystem::readEnvironment(environmentFile);
#ifdef _WIN32
        environment["VSLANG"] = "1033";
#endif
        const int rc = ab::Process::run(cmd, cwd, environment);
        if (rc != 0)
        {
            // A failed command must never leave partially overwritten outputs as successful results.
            for (const auto& o : outputs)
            {
                std::error_code ignored;
                fs::remove(o, ignored);
            }
            return rc;
        }
        for (const auto& output : outputs)
        {
            if (!fs::is_regular_file(output))
            {
                std::cerr << "axlum-build: command succeeded but did not produce declared output '" << output.string() << "'\n";
                return 1;
            }
        }
        if (!msvcJson.empty())
        {
            if (outputs.empty() || depfile.empty())
                throw std::runtime_error("MSVC dependency conversion requires output and depfile");
            try
            {
                ab::FileSystem::writeMsvcDepfile(msvcJson, depfile, outputs.front());
            }
            catch (...)
            {
                for (const auto& output : outputs)
                {
                    std::error_code ignored;
                    fs::remove(output, ignored);
                }
                throw;
            }
        }
        return 0;
    }

    int Application::run(int argc, char** argv)
    {
        try
        {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i)
                args.emplace_back(argv[i]);
            if (args.size() == 2 && args[0] == "__unsupported-target")
                throw std::runtime_error(args[1]);
            if (!args.empty() && args[0] == "__verify")
            {
                for (std::size_t i = 1; i < args.size(); ++i)
                    if (!fs::is_regular_file(args[i]))
                        throw std::runtime_error("bootstrap output missing: " + args[i]);
                return 0;
            }
            if (args.size() == 2 && args[0] == "__feature")
                return DslFeatures::execute(args[1]);
            if (args.size() == 2 && args[0] == "__discover-tests")
                return DslFeatures::discoverTests(args[1]);
            if (args.size() >= 2 && args[0] == "__managed-test")
            {
                std::vector<fs::path> candidates;
                if (fs::is_directory(args[1]))
                    for (auto& entry : fs::recursive_directory_iterator(args[1]))
                        if (entry.path().extension() == ".json" && entry.path().filename().string().ends_with(".runtimeconfig.json"))
                        {
                            auto path = entry.path();
                            auto name = path.filename().string();
                            name.resize(name.size() - std::string(".runtimeconfig.json").size());
                            path = path.parent_path() / (name + ".dll");
                            if (fs::is_regular_file(path))
                                candidates.push_back(path);
                        }
                if (candidates.size() != 1)
                    throw std::runtime_error("managed test requires one runnable assembly; use explicit test command for multi-TFM projects");
                std::vector<std::string> command = {Process::findExecutable("dotnet"), candidates.front().string()};
                command.insert(command.end(), args.begin() + 2, args.end());
                return Process::run(command, fs::current_path());
            }
            if (!args.empty() && args[0] == "__run")
                return runWrapped({args.begin() + 1, args.end()});
            if (!args.empty() && args[0] == "__native-project")
                return ab::NativeProjectBuilder::buildNativeProject({args.begin() + 1, args.end()});
            if (!args.empty() && args[0] == "__dotnet-build")
                return ab::DotNetBuilder::buildManaged({args.begin() + 1, args.end()});
            if (!args.empty() && args[0] == "__copy")
            {
                if (args.size() != 3)
                    throw std::runtime_error("__copy requires source and destination");
                ab::FileSystem::writeIfChanged(args[2], ab::FileSystem::readAll(args[1]));
                return 0;
            }
            if (args.size() == 3 && args[0] == "__refresh" && args[1] == "-B")
            {
                const auto directory = fs::absolute(args[2]).lexically_normal();
                if (configureInputsChanged(directory))
                    throw std::runtime_error("configuration inputs changed; run configure -B " + directory.string());
                while (prepareDynamicSources(directory, readState(directory).at("ninja")))
                    refresh(directory, currentExecutable(argv[0]));
                return 0;
            }
            auto& opt = m_options;
            opt = Arguments{};
            CLI::App app{"AxlumBuild | C/C++ and C# build orchestration with Ninja", "axlum-build"};
            configureCli(app, opt);
            if (args.empty())
            {
                std::cout << app.help();
                return 0;
            }
            // Keep the help command, with optional per-command help.
            if (args[0] == "help")
            {
                if (args.size() == 1)
                {
                    std::cout << app.help();
                    return 0;
                }
                if (args.size() != 2)
                    throw std::runtime_error("usage: axlum-build help [command]");
                auto* command = app.get_subcommand_no_throw(args[1]);
                if (!command)
                    throw std::runtime_error("unknown command '" + args[1] + "'");
                std::cout << command->help();
                return 0;
            }
            try
            {
                if (args.size() >= 2 && (args[0] == "--build" || args[0] == "--install"))
                {
                    const auto command = args[0] == "--build" ? "build" : "install";
                    args[0] = command;
                    args.insert(args.begin() + 1, "-B");
                }
                else if (!args.empty() && (args[0] == "-S" || args[0] == "-B"))
                    args.insert(args.begin(), "configure");
                if (!args.empty() && args[0] == "run")
                {
                    bool hasTarget = false;
                    const std::set<std::string> values = {"-B", "--build-dir", "-t", "--target", "--config", "--cwd", "--env", "-j", "--jobs", "--parallel"};
                    const std::set<std::string> flags = {"--no-build", "-v", "--verbose", "-h", "--help", "--help-all"};
                    for (std::size_t i = 1; i < args.size(); ++i)
                    {
                        if (args[i] == "--")
                        {
                            opt.m_runArguments.assign(args.begin() + i + 1, args.end());
                            args.erase(args.begin() + i, args.end());
                            break;
                        }
                        auto key = args[i].substr(0, args[i].find('='));
                        bool attached = args[i].find('=') != std::string::npos;
                        if (!values.contains(key) && args[i].size() > 2 && values.contains(args[i].substr(0, 2)))
                        {
                            key = args[i].substr(0, 2);
                            attached = true;
                        }
                        if (values.contains(key))
                        {
                            if (key == "-t" || key == "--target")
                                hasTarget = true;
                            if (!attached)
                                ++i;
                            continue;
                        }
                        if (flags.contains(key))
                            continue;
                        if (!hasTarget && !args[i].starts_with("-"))
                        {
                            hasTarget = true;
                            continue;
                        }
                        if (hasTarget)
                        {
                            opt.m_runArguments.assign(args.begin() + i, args.end());
                            args.erase(args.begin() + i, args.end());
                            break;
                        }
                    }
                }
                std::vector<char*> parsed{argv[0]};
                for (auto& arg : args)
                    parsed.push_back(arg.data());
                app.parse(static_cast<int>(parsed.size()), parsed.data());
            }
            catch (const CLI::ParseError& error)
            {
                return app.exit(error);
            }
            if (opt.m_command == "doctor")
                return doctor(opt);
            if (opt.m_command == "ide" || opt.m_command == "matrix")
                return configureEntries(opt, currentExecutable(argv[0]));
            if (opt.m_command == "uninstall")
            {
                for (auto& name : opt.m_targets)
                    for (std::size_t at; (at = name.find("::")) != std::string::npos;)
                        name.replace(at, 2, "__");
                return ab::BuildOperations::uninstallTargets(
                    fs::absolute(opt.m_installation.m_prefix), opt.m_targets, opt.m_installation.m_component, opt.m_installation.m_dryRun
                );
            }
            const fs::path buildDir = fs::absolute(opt.m_buildDir).lexically_normal();
            if (opt.m_command == "options")
            {
                if (!opt.m_sourceDir.empty())
                    opt.m_file = (fs::absolute(opt.m_sourceDir) / opt.m_file).string();
                auto ws = fs::is_regular_file(buildDir / "axlum.context.json") ? ConfigurationCache::load(buildDir).m_workspace
                                                                               : ManifestLoader::loadManifest(opt.m_file, Configuration{"__inspect"}, buildDir);
                auto schema = ws.m_features.value("options", nlohmann::json::object());
                if (opt.m_json)
                    std::cout << schema.dump(2) << '\n';
                else
                    for (auto& [name, spec] : schema.items())
                        std::cout << name << " (" << spec.value("type", std::string("bool")) << ") = " << spec.at("value").dump() << "  "
                                  << spec.value("description", std::string{}) << '\n';
                return 0;
            }
            if (opt.m_command == "test")
            {
                if (!opt.m_noBuild)
                {
                    auto context = ConfigurationCache::load(buildDir);
                    auto rc = BuildExecutor::executeNinja(buildDir, context.m_toolchain.m_ninja, "build", {}, opt.m_execution);
                    if (rc)
                        return rc;
                }
                return DslFeatures::runTests(buildDir, opt.m_testPattern, opt.m_testLabel);
            }
            if (opt.m_command == "configure")
            {
                if (!opt.m_sourceDir.empty())
                    opt.m_file = (fs::absolute(opt.m_sourceDir) / opt.m_file).string();
                configure(opt, buildDir, currentExecutable(argv[0]));
                return 0;
            }
            auto state = readState(buildDir);
            if (!opt.m_requestedConfig.empty() &&
                ab::BuildModel::parseConfiguration(opt.m_requestedConfig) != ab::BuildModel::parseConfiguration(state.at("config")))
                throw std::runtime_error("--config does not match this build directory; configure a separate -B directory");
            if (opt.m_command == "targets" || opt.m_command == "info")
            {
                const auto artifacts = nlohmann::json::parse(ab::FileSystem::readAll(buildDir / "axlum.artifacts.json"));
                const auto selection = nlohmann::json::parse(ab::FileSystem::readAll(buildDir / "axlum.target-selection.json"));
                if (opt.m_json)
                    std::cout << nlohmann::json{{"state", state}, {"targets", artifacts}, {"selection", selection}}.dump(2) << '\n';
                else
                {
                    if (opt.m_command == "info")
                        for (const auto& [key, value] : state)
                            std::cout << key << ": " << value << '\n';
                    for (const auto& [name, data] : artifacts.items())
                        std::cout << name << " (" << data.at("kind").get<std::string>() << ")\n";
                    if (selection.contains("disabled"))
                        for (const auto& [name, reason] : selection.at("disabled").items())
                            std::cout << name << ": " << reason << '\n';
                }
                return 0;
            }
            if (opt.m_command == "run")
            {
                auto index = Json::parse(FileSystem::readAll(buildDir / "axlum.artifacts.json"));
                auto target = opt.m_target;
                if (target.empty() && fs::is_regular_file(buildDir / "axlum.run-targets.json"))
                {
                    const auto candidates = Json::parse(FileSystem::readAll(buildDir / "axlum.run-targets.json"));
                    if (!candidates.empty())
                        target = candidates.front().get<std::string>();
                }
                if (target.empty())
                    for (const auto& [name, data] : index.items())
                        if (data.at("kind") == "executable")
                        {
                            target = name;
                            break;
                        }
                for (std::size_t at; (at = target.find("::")) != std::string::npos;)
                    target.replace(at, 2, "__");
                const auto selection = Json::parse(FileSystem::readAll(buildDir / "axlum.target-selection.json"));
                if (selection.at("aliases").contains(target))
                    target = selection.at("aliases").at(target).get<std::string>();
                if (!target.empty() && index.contains(target) &&
                    (index.at(target).at("kind") == "executable" || index.at(target).at("kind") == "dotnet_project"))
                {
                    if (opt.m_execution.m_dryRun)
                        throw std::runtime_error("run does not support --dry-run");
                    if (!opt.m_noBuild)
                    {
                        const auto rc = BuildExecutor::executeNinja(buildDir, state.at("ninja"), "build", {target}, opt.m_execution);
                        if (rc)
                            return rc;
                        index = Json::parse(FileSystem::readAll(buildDir / "axlum.artifacts.json"));
                        state = readState(buildDir);
                    }
                    Workspace ws;
                    Toolchain tc;
                    tc.m_target = state.at("target");
                    tc.m_host = ToolchainDiscovery::hostTriple();
                    tc.m_architecture = state.at("architecture");
                    tc.m_platform = state.at("platform");
                    tc.m_dotnet = state.contains("dotnet") ? state.at("dotnet") : Process::findExecutable("dotnet");
                    BuildGraph graph;
                    for (const auto& [name, data] : index.items())
                    {
                        graph.m_targetKinds[name] = BuildModel::parseTargetKind(data.at("kind").get<std::string>());
                        for (const auto& output : data.at("outputs"))
                            graph.m_targetOutputs[name].emplace_back(output.get<std::string>());
                    }
                    const CommandContext context{ws, tc, graph, buildDir, currentExecutable(argv[0]), BuildModel::parseConfiguration(state.at("config"))};
                    return BuildOperations::runTarget(context, target, opt.m_runArguments, opt.m_workingDirectory, opt.m_environment, true, opt.m_execution);
                }
            }
            std::optional<ab::IDEContext> prepared;
            if (opt.m_command == "graph" || opt.m_command == "publish" || opt.m_command == "install" || opt.m_command == "run")
            {
                prepared = ConfigurationCache::load(buildDir);
                state = readState(buildDir);
            }
            else if (
                (opt.m_command == "build" || opt.m_command == "rebuild") &&
                (!state.contains("schema") || state.at("schema") != "2" || !state.contains("ninja") || !fs::is_regular_file(state.at("ninja")))
            )
            {
                throw std::runtime_error("build configuration is incompatible; run configure first");
            }
            (void)state;
            const auto ninja = state.contains("ninja") ? state.at("ninja") : ab::Process::findExecutable("ninja");
            if (ninja.empty())
                throw std::runtime_error("ninja not on PATH");
            if (opt.m_command == "graph" || opt.m_command == "publish" || opt.m_command == "install" || opt.m_command == "run")
            {
                const auto& ws = prepared->m_workspace;
                const auto& tc = prepared->m_toolchain;
                const auto& graph = prepared->m_graph;
                const auto config = prepared->m_configuration;
                if (opt.m_command == "graph")
                {
                    printGraph(graph);
                    return 0;
                }
                const ab::CommandContext context{ws, tc, graph, buildDir, currentExecutable(argv[0]), config};
                if (opt.m_command == "run")
                    return ab::BuildOperations::runTarget(
                        context, opt.m_target, opt.m_runArguments, opt.m_workingDirectory, opt.m_environment, opt.m_noBuild, opt.m_execution
                    );
                return ab::BuildOperations::installTargets(context, opt.m_targets, opt.m_installation, opt.m_publication, opt.m_execution);
            }
            for (auto& name : opt.m_targets)
                for (std::size_t at; (at = name.find("::")) != std::string::npos;)
                    name.replace(at, 2, "__");
            if (opt.m_command == "clean")
            {
                if (opt.m_cleanAll)
                    opt.m_cleaning.m_mode = "all";
                return ab::BuildOperations::cleanBuild(buildDir, ninja, opt.m_targets, opt.m_cleaning, opt.m_execution);
            }
            if (opt.m_cleanFirst && opt.m_execution.m_dryRun)
                throw std::runtime_error("--clean-first cannot be combined with --dry-run");
            return ab::BuildExecutor::executeNinja(buildDir, ninja, opt.m_cleanFirst ? "rebuild" : opt.m_command, opt.m_targets, opt.m_execution);
        }
        catch (const std::exception& e)
        {
            std::cerr << "axlum-build: error: " << e.what() << '\n';
            return 2;
        }
    }
} // namespace axlum
