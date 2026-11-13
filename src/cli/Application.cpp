/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "cli/Application.hpp"
#include "backend/Ninja.hpp"
#include "core/BuildOperations.hpp"
#include "core/Diagnostics.hpp"
#include "core/Process.hpp"
#include "graph/Graph.hpp"
#include "managed/DotNet.hpp"
#include "managed/NativeProject.hpp"
#include "manifest/Manifest.hpp"
#include "packages/Rhodium.hpp"
#include "toolchain/Toolchain.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
            auto* command = app.add_subcommand(
                name,
                std::string(name) == "ide" ? "Generate IDE projects from the native target model"
                                           : "Configure/build isolated configuration and architecture entries"
            );
            command->add_option("-f,--file", options.m_file, "Workspace manifest")->check(CLI::ExistingFile);
            command->add_option("-B,--build-dir", options.m_buildDir, "Root of isolated entry build directories");
            command->add_option("--arch", options.m_architecture, "Architecture (IDE default)");
            command->add_option("--platform", options.m_platform, "Target platform");
            command->add_option("--toolchain", options.m_toolchain, "Toolchain profile ID");
            command->add_option("--preset", options.m_presets, "Preset names (repeatable); matrix defaults to all");
            command->add_flag("--locked", options.m_locked, "Validate package lock identity");
            if (std::string(name) == "ide")
            {
                command->add_option("--format", options.m_format, "vscode/vs/xcode/android-studio/all")
                    ->check(CLI::IsMember({"vscode", "vs", "xcode", "android-studio", "all"}));
                command->add_option("-o,--output", options.m_output, "Generated IDE project directory (default: <B>/ide)");
            }
            else
            {
                command->add_flag("--configure-only", options.m_configureOnly, "Generate entries without building");
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
        }
        opt.m_architecture = ab::BuildModel::normalizeArchitecture(opt.m_architecture);
        const auto config = ab::BuildModel::parseConfiguration(opt.m_config);
        if (fs::absolute(opt.m_file).lexically_normal().parent_path() == buildDir)
            throw std::runtime_error("build directory cannot equal workspace root");
        auto ws = ab::ManifestLoader::loadManifest(opt.m_file, config, buildDir, opt.m_architecture, opt.m_platform);
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
        auto tc = ab::ToolchainDiscovery::discoverToolchain(opt.m_compiler, ws.m_root, opt.m_toolchain, opt.m_architecture, opt.m_platform);
        if (!ws.m_architectures.empty() && std::find(ws.m_architectures.begin(), ws.m_architectures.end(), opt.m_architecture) == ws.m_architectures.end())
            throw std::runtime_error("architecture is not declared by workspace: " + opt.m_architecture);
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
        const auto packageFingerprint = ab::PackageManager::packageInputsFingerprint(ws, tc, config, buildDir);
        const auto packageState = buildDir / "axlum.packages";
        const bool restore = !automatic || !fs::is_regular_file(packageState) || ab::FileSystem::readAll(packageState) != packageFingerprint;
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
        ab::ManagedProjectGenerator::prepareManagedProjects(ws, buildDir, self);
        const auto graph = ab::BuildPlanner::planBuild(ws, tc, buildDir, config, self);
        ab::NinjaBackend::generateNinja(graph, buildDir, self);
        ab::NinjaBackend::generateCompileCommands(graph, buildDir);
        ab::IDEGenerator::writeArtifactIndex(graph, buildDir);
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
                if (!outputs.contains(pathKey(file)) && !inputs.contains(pathKey(file)) && ab::FileSystem::isWithin(resolved, fs::weakly_canonical(buildDir)) &&
                    fs::is_regular_file(resolved))
                    fs::remove(fs::path(file));
            }
        }
        std::string outputList;
        for (const auto& file : outputPaths)
            outputList += file + "\n";
        ab::FileSystem::writeIfChanged(ledger, outputList);
        ab::FileSystem::writeIfChanged(packageState, ab::PackageManager::packageInputsFingerprint(ws, tc, config, buildDir));
        ab::FileSystem::writeIfChanged(
            buildDir / "axlum.state",
            "manifest=" + ws.m_manifest.string() + "\nconfig=" + ab::BuildModel::toString(config) + "\ncompiler=" + opt.m_compiler + "\ntarget=" + tc.m_target +
                "\nfingerprint=" + fingerprint + "\ntoolchain=" + tc.m_id + "\nprofile=" + opt.m_toolchain + "\nlocked=" + (opt.m_locked ? "1" : "0") +
                "\nninja=" + tc.m_ninja + "\nschema=2\narchitecture=" + opt.m_architecture + "\nplatform=" + opt.m_platform + "\npreset=" + opt.m_preset + "\n"
        );
        if (!automatic)
            std::cout << "Configured " << ws.m_name << " (" << tc.m_target << ", " << ab::BuildModel::toString(config) << ")\n"
                      << "Build graph: " << (buildDir / "build.ninja") << "\n"
                      << "Actions: " << graph.m_actions.size() << "\n";
        return {std::move(ws), std::move(tc), graph, buildDir, config, BuildModel::toString(config) + "-" + opt.m_architecture};
    }

    ab::IDEContext Application::refresh(const fs::path& buildDir, const fs::path& self)
    {
        const auto state = readState(buildDir);
        Arguments opt;
        opt.m_file = state.at("manifest");
        opt.m_config = state.at("config");
        opt.m_compiler = state.at("compiler");
        opt.m_toolchain = state.contains("profile") ? state.at("profile") : state.at("toolchain");
        opt.m_locked = state.contains("locked") && state.at("locked") == "1";
        opt.m_architecture = state.contains("architecture") ? state.at("architecture") : "x64";
        opt.m_platform = state.contains("platform") ? state.at("platform") : "";
        opt.m_preset = state.contains("preset") ? state.at("preset") : "";
        return configure(opt, buildDir, self, true);
    }

    int Application::configureEntries(const Arguments& options, const fs::path& self)
    {
        const auto root = fs::absolute(options.m_buildDir).lexically_normal();
        const auto metadata =
            ab::ManifestLoader::loadManifest(options.m_file, ab::Configuration{"__inspect"}, root, options.m_architecture, options.m_platform);
        std::vector<ab::BuildPreset> entries;
        if (!options.m_presets.empty() || (options.m_command == "matrix" && !metadata.m_presets.empty()))
        {
            for (const auto& preset : metadata.m_presets)
                if (options.m_presets.empty() || std::find(options.m_presets.begin(), options.m_presets.end(), preset.m_name) != options.m_presets.end())
                    entries.push_back(preset);
            for (const auto& name : options.m_presets)
                if (std::none_of(
                        entries.begin(),
                        entries.end(),
                        [&](const auto& p)
                        {
                            return p.m_name == name;
                        }
                    ))
                    throw std::runtime_error("unknown preset: " + name);
        }
        else
        {
            const auto configs = metadata.m_configurations.empty() ? std::vector<ab::Configuration>{ab::Configuration::s_debug, ab::Configuration::s_release}
                                                                   : metadata.m_configurations;
            const auto architectures = options.m_command == "matrix" && !metadata.m_architectures.empty()
                                           ? metadata.m_architectures
                                           : std::vector<std::string>{ab::BuildModel::normalizeArchitecture(options.m_architecture)};
            for (const auto& config : configs)
                for (const auto& arch : architectures)
                    entries.push_back(
                        {ab::BuildModel::toString(config) + "-" + arch, ab::BuildModel::toString(config), arch, options.m_platform, options.m_toolchain, "auto"}
                    );
        }
        std::vector<ab::IDEContext> contexts;
        for (const auto& entry : entries)
        {
            Arguments opt = options;
            opt.m_preset.clear();
            opt.m_config = entry.m_configuration;
            opt.m_architecture = entry.m_architecture;
            opt.m_platform = entry.m_platform;
            opt.m_toolchain = entry.m_toolchain;
            opt.m_compiler = entry.m_compiler;
            const auto directory = root / entry.m_name;
            auto context = configure(opt, directory, self, false, true);
            context.m_label = entry.m_name;
            if (context.m_workspace.m_targets.empty())
            {
                std::cout << "Skipped " << entry.m_name << ": no supported targets\n";
                continue;
            }
            contexts.push_back(std::move(context));
            if (options.m_command == "matrix" && !options.m_configureOnly)
            {
                std::vector<std::string> command = {self.string(), "build", "-B", directory.string()};
                if (!options.m_target.empty())
                    command.insert(command.end(), {"-t", options.m_target});
                if (options.m_execution.m_jobs)
                    command.insert(command.end(), {"-j", std::to_string(options.m_execution.m_jobs)});
                if (options.m_execution.m_verbose)
                    command.push_back("--verbose");
                if (options.m_execution.m_explain)
                    command.push_back("--why-rebuild");
                if (options.m_execution.m_timing)
                    command.push_back("--timing");
                const auto rc = ab::Process::run(command);
                if (rc != 0)
                    return rc;
            }
        }
        if (options.m_command == "ide")
        {
            const auto output = options.m_output.empty() ? root / "ide" : fs::absolute(options.m_output);
            ab::IDEGenerator::generateIDE(contexts, output, self, options.m_format);
            std::cout << "Generated " << options.m_format << " projects: " << output << '\n';
        }
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
                refresh(fs::absolute(args[2]).lexically_normal(), currentExecutable(argv[0]));
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
                    const auto separator = std::find(args.begin(), args.end(), "--");
                    if (separator != args.end())
                    {
                        opt.m_runArguments.assign(separator + 1, args.end());
                        args.erase(separator, args.end());
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
            std::optional<ab::IDEContext> prepared;
            if (opt.m_command == "graph" || opt.m_command == "publish" || opt.m_command == "install" || opt.m_command == "run")
            {
                prepared = refresh(buildDir, currentExecutable(argv[0]));
                state = readState(buildDir);
            }
            else if (
                (opt.m_command == "build" || opt.m_command == "rebuild") &&
                (!state.contains("schema") || state.at("schema") != "2" || !state.contains("ninja") || !fs::is_regular_file(state.at("ninja")))
            )
            {
                refresh(buildDir, currentExecutable(argv[0]));
                state = readState(buildDir);
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
