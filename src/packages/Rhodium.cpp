/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "packages/Rhodium.hpp"
#include "core/Process.hpp"
#include "graph/NativeOptions.hpp"
#include "packages/PackageManifest.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <rhodium/config/DependencyOverrideFile.hpp>
#include <rhodium/config/JsonModelWriter.hpp>
#include <rhodium/config/LockFile.hpp>
#include <rhodium/core/Environment.hpp>
#include <rhodium/core/WorkspaceLock.hpp>
#include <rhodium/diagnostics/DiagnosticSink.hpp>
#include <rhodium/package/GitFetcher.hpp>
#include <rhodium/package/ToolchainContextResolver.hpp>
#include <rhodium/resolve/DependencyResolver.hpp>
#include <rhodium/resolve/PackageArtifactResolver.hpp>
#include <set>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        class Diagnostics final : public rhodium::DiagnosticSink
        {
          public:
            void report(const rhodium::Diagnostic& d) override
            {
                std::cerr << "[rhodium] " << d.message << '\n';
            }
        };

        std::map<std::string, std::string> packageRuntimes(const Workspace& ws, const Toolchain& tc)
        {
            std::map<std::string, std::string> policies;
            const auto& recipes = ws.m_packageManifest->m_model.rootPackage.dependencies;
            for (const auto& target : ws.m_targets)
            {
                if (target.m_kind != TargetKind::Executable && target.m_kind != TargetKind::StaticLibrary && target.m_kind != TargetKind::SharedLibrary)
                    continue;
                std::set<std::string> visited;
                std::function<void(const std::string&)> attach = [&](const std::string& name)
                {
                    if (!visited.insert(name).second)
                        return;
                    const auto runtime = tc.m_kind == CompilerKind::MSVC ? NativeOptions::runtimePolicy(target.m_compile) : "dynamic";
                    const auto old = policies.find(name);
                    if (old != policies.end() && old->second != runtime)
                        throw std::runtime_error("package has consumers with incompatible CRT policies: " + name);
                    policies[name] = runtime;
                    const auto dep = std::find_if(
                        recipes.begin(),
                        recipes.end(),
                        [&](const auto& d)
                        {
                            return d.name == name;
                        }
                    );
                    if (dep != recipes.end())
                        for (const auto& sub : dep->dependencies)
                            attach(sub);
                };
                for (const auto* group : {&target.m_packages, &target.m_publicPackages})
                    for (const auto& name : *group)
                        attach(name);
            }
            return policies;
        }

        bool runtimeBinary(const fs::path& path)
        {
            const auto name = path.filename().string();
            return path.extension() == ".dll" || path.extension() == ".dylib" || name.find(".so") != std::string::npos;
        }

        std::string packageTarget(const std::string& name)
        {
            return "_rhodium_" + FileSystem::shortHash(name);
        }
    } // namespace

    std::string PackageManager::packageInputsFingerprint(const Workspace& ws, const Toolchain& tc, Configuration config, const fs::path& build)
    {
        if (!ws.m_packageManifest || ws.m_packageManifest->m_model.rootPackage.dependencies.empty())
            return "none";
        const auto& model = ws.m_packageManifest->m_model;
        std::string data = rhodium::JsonModel::write(model) + ToolchainDiscovery::toolchainFingerprint(tc, config) + BuildModel::toString(config);
        for (const auto& [name, runtime] : packageRuntimes(ws, tc))
            data += "\n" + name + "=" + runtime;
        auto addFile = [&](const fs::path& path)
        {
            data += "\n" + fs::absolute(path).lexically_normal().generic_string() + "=";
            data += fs::is_regular_file(path) ? FileSystem::shortHash(FileSystem::readAll(path)) : "missing";
        };
        addFile(ws.m_root / "tpkg.lock.toml");
        Diagnostics diagnostics;
        rhodium::DependencyOverrideSet overrides;
        if (!rhodium::DependencyOverrides::make(model, ws.m_root, {}, overrides, diagnostics))
            throw std::runtime_error("invalid Rhodium dependency overrides");
        std::set<fs::path> trees;
        for (const auto& dep : model.rootPackage.dependencies)
        {
            const auto overridden = overrides.overrides.find(dep.name);
            if (overridden != overrides.overrides.end())
                trees.insert(overridden->second.path);
            else if (dep.local || dep.sourceType == "local")
                trees.insert(ws.m_root / dep.source);
            for (const auto& patch : dep.patches)
                addFile(ws.m_root / patch);
            if (!dep.cmake.toolchainFile.empty())
                addFile(ws.m_root / dep.cmake.toolchainFile);
        }
        for (const auto& tree : trees)
        {
            data += "\nsource=" + fs::absolute(tree).lexically_normal().generic_string();
            if (!fs::is_directory(tree))
            {
                data += " missing";
                continue;
            }
            std::vector<fs::path> files;
            for (fs::recursive_directory_iterator it(tree), end; it != end; ++it)
            {
                if (it->is_directory())
                {
                    const auto name = it->path().filename().string();
                    if (name == ".git" || name == ".rhodium" || name == ".tpkg" || name == ".axlum" || name == ".cache" || name == "build" || name == "bin" ||
                        name == "obj" || name.starts_with("build-") || FileSystem::isWithin(it->path(), build) || fs::exists(it->path() / "CMakeCache.txt") ||
                        fs::exists(it->path() / "axlum.state"))
                        it.disable_recursion_pending();
                }
                else if (it->is_regular_file())
                    files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files)
                addFile(file);
        }
        return FileSystem::shortHash(data);
    }

    void PackageManager::resolvePackages(Workspace& ws, const Toolchain& tc, Configuration config, bool locked, bool restore)
    {
        if (!ws.m_packageManifest || ws.m_packageManifest->m_model.rootPackage.dependencies.empty())
            return;
        auto model = ws.m_packageManifest->m_model;
        if (!tc.m_profile)
            throw std::runtime_error("package restore requires a discovered Rhodium toolchain profile");
        const auto profile = *tc.m_profile;
        const auto runtimes = packageRuntimes(ws, tc);
        const auto configName = config == Configuration::s_debug ? "debug" : "release";
        model.workspace.defaultConfig = configName;
        // Package builds must use the same CRT/PIC policy as native consumer actions.
        for (auto& dep : model.rootPackage.dependencies)
        {
            const auto consumer = runtimes.find(dep.name);
            const auto requiredRuntime =
                consumer == runtimes.end() || tc.m_kind != CompilerKind::MSVC ? (dep.runtime == "default" ? "dynamic" : dep.runtime) : consumer->second;
            if (dep.runtime != "default" && dep.runtime != requiredRuntime)
                throw std::runtime_error("package CRT policy conflicts with its consumer: " + dep.name);
            dep.runtime = requiredRuntime;
            if (tc.m_kind == CompilerKind::MSVC)
            {
                const auto expected = requiredRuntime == "static" ? "MultiThreaded$<$<CONFIG:Debug>:Debug>" : "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL";
                const std::string concrete =
                    std::string(config == Configuration::s_debug ? "MultiThreadedDebug" : "MultiThreaded") + (requiredRuntime == "static" ? "" : "DLL");
                for (const auto* options : {&dep.cmake.options, &dep.buildOptions})
                {
                    const auto runtime = options->find("CMAKE_MSVC_RUNTIME_LIBRARY");
                    if (runtime != options->end() && runtime->second != expected && runtime->second != concrete)
                        throw std::runtime_error("package CMAKE_MSVC_RUNTIME_LIBRARY conflicts with its consumer: " + dep.name);
                }
                dep.cmake.options["CMAKE_MSVC_RUNTIME_LIBRARY"] = expected;
                dep.buildOptions["CMAKE_MSVC_RUNTIME_LIBRARY"] = expected;
            }
            if (dep.pic == "default" && tc.m_kind != CompilerKind::MSVC)
                dep.pic = "true";
            if (!dep.cmake.buildType.empty() && rhodium::BuildConfig::normalize(dep.cmake.buildType) != configName)
                throw std::runtime_error("package build_type must match the selected configuration: " + dep.name);
            dep.cmake.buildType = BuildModel::toString(config);
            if (tc.m_platform == "android")
            {
                dep.cmake.toolchainFile = (profile.androidNdkRoot / "build/cmake/android.toolchain.cmake").generic_string();
                dep.cmake.options["ANDROID_ABI"] = profile.androidAbi;
                dep.cmake.options["ANDROID_PLATFORM"] = "android-" + profile.androidApi;
            }
            else if (tc.m_platform == "macos")
            {
                dep.cmake.options["CMAKE_OSX_ARCHITECTURES"] = tc.m_architecture == "arm64" ? "arm64" : "x86_64";
                if (!profile.sdkRoot.empty())
                    dep.cmake.options["CMAKE_OSX_SYSROOT"] = profile.sdkRoot.generic_string();
                if (!profile.deploymentTarget.empty())
                    dep.cmake.options["CMAKE_OSX_DEPLOYMENT_TARGET"] = profile.deploymentTarget;
            }
            for (const auto& [key, value] : std::map<std::string, std::string>{
                     {"CMAKE_C_COMPILER", tc.m_cCompiler}, {"CMAKE_CXX_COMPILER", tc.m_cppCompiler}, {"CMAKE_AR", tc.m_archiver}
                 })
            {
                dep.cmake.options[key] = fs::path(value).generic_string();
                dep.buildOptions[key] = fs::path(value).generic_string();
            }
            if (tc.m_kind == CompilerKind::MSVC)
            {
                for (const auto& [key, value] : std::map<std::string, std::string>{
                         {"CMAKE_LINKER", tc.m_linker}, {"CMAKE_RC_COMPILER", tc.m_resourceCompiler}, {"CMAKE_MT", tc.m_manifestTool}
                     })
                    if (!value.empty())
                    {
                        dep.cmake.options[key] = fs::path(value).generic_string();
                        dep.buildOptions[key] = fs::path(value).generic_string();
                    }
            }
            if (tc.m_kind == CompilerKind::Clang && tc.m_host != tc.m_target)
            {
                dep.cmake.options["CMAKE_C_COMPILER_TARGET"] = tc.m_target;
                dep.cmake.options["CMAKE_CXX_COMPILER_TARGET"] = tc.m_target;
            }
        }
        Diagnostics diagnostics;
        auto lock = rhodium::WorkspaceLock::acquire(ws.m_root, diagnostics);
        if (!lock)
            throw std::runtime_error("cannot lock Rhodium workspace");
        rhodium::DependencyOverrideSet overrides;
        if (!rhodium::DependencyOverrides::make(model, ws.m_root, {}, overrides, diagnostics))
            throw std::runtime_error("invalid Rhodium dependency overrides");
        rhodium::ToolchainSnapshot snapshot(ws.m_root, {profile});
        if (restore)
        {
            rhodium::GitFetcher git;
            if (!git.initialize(diagnostics))
                throw std::runtime_error("cannot initialize Rhodium Git support");
            rhodium::DependencyRestoreRequest request;
            request.preferredToolchain = profile.id;
            request.config = configName;
            request.overrides = overrides;
            request.options.locked = locked;
            request.options.toolchains = &snapshot;
            request.options.toolchainContentHash = ToolchainDiscovery::toolchainFingerprint(tc, config);
            if (!rhodium::DependencyResolver{}.restore(model, ws.m_root, diagnostics, request))
                throw std::runtime_error("Rhodium package restore failed");
        }
        rhodium::LockFile lockfile;
        rhodium::PackageArtifactResolver artifacts;
        if (!rhodium::LockFile::read(ws.m_root / "tpkg.lock.toml", lockfile, diagnostics) ||
            !artifacts.load(lockfile, ws.m_root, {configName, profile.id}, overrides, diagnostics))
            throw std::runtime_error("cannot read Rhodium package artifacts; run configure first");
        const auto originalCount = ws.m_targets.size();
        std::map<std::string, std::string> imported;
        for (const auto& dep : model.rootPackage.dependencies)
        {
            const auto* artifact = artifacts.find(dep.name);
            if (!artifact)
                throw std::runtime_error("missing package artifact for " + dep.name + "; run configure first");
            if (!artifact->frameworks.empty() && tc.m_platform != "macos" && !tc.m_platform.starts_with("ios"))
                throw std::runtime_error("package frameworks require an Apple toolchain: " + dep.name);
            Target target;
            target.m_name = packageTarget(dep.name);
            target.m_kind = TargetKind::HeaderOnly;
            target.m_compile.m_runtime = dep.runtime;
            target.m_link.m_frameworks = artifact->frameworks;
            for (const auto& framework : artifact->frameworks)
                for (const auto& directory : artifact->libDirs)
                    if (fs::is_directory(directory / (fs::path(framework).extension() == ".framework" ? framework : framework + ".framework")))
                        target.m_link.m_frameworkDirectories.push_back(directory.string());
            for (const auto& path : artifact->binFiles)
                target.m_runtimeFiles.push_back(path.string());
            for (const auto& dir : artifact->binDirs)
                if (fs::is_directory(dir))
                {
                    for (const auto& entry : fs::directory_iterator(dir))
                        if (entry.is_regular_file() && runtimeBinary(entry.path()))
                            target.m_runtimeFiles.push_back(entry.path().string());
                }
            for (const auto& path : artifact->includeDirs)
                target.m_compile.m_publicIncludes.push_back(path.string());
            target.m_compile.m_publicDefines = artifact->defines;
            for (const auto& path : artifact->libDirs)
                target.m_link.m_libraryDirectories.push_back(path.string());
            for (const auto& path : artifact->libFiles)
            {
                target.m_link.m_libraries.push_back(path.string());
                if (runtimeBinary(path))
                    target.m_runtimeFiles.push_back(path.string());
            }
            for (auto lib : artifact->libs)
            {
                if (tc.m_kind == CompilerKind::MSVC && fs::path(lib).extension().empty())
                    lib += ".lib";
                std::string resolved = lib;
                for (const auto& dir : artifact->libDirs)
                {
                    const auto candidates =
                        tc.m_kind == CompilerKind::MSVC ? std::vector<std::string>{lib}
                        : dep.linkage == "shared"
                            ? std::vector<
                                  std::
                                      string>{"lib" + lib + (tc.m_platform == "macos" || tc.m_platform.starts_with("ios") ? ".dylib" : ".so"), "lib" + lib + ".a", lib}
                            : std::vector<std::string>{
                                  "lib" + lib + ".a", "lib" + lib + (tc.m_platform == "macos" || tc.m_platform.starts_with("ios") ? ".dylib" : ".so"), lib
                              };
                    bool found = false;
                    for (const auto& candidate : candidates)
                        if (fs::is_regular_file(dir / candidate))
                        {
                            resolved = (dir / candidate).string();
                            found = true;
                            break;
                        }
                    if (found)
                        break;
                }
                target.m_link.m_libraries.push_back(resolved);
                if (fs::is_regular_file(resolved) && runtimeBinary(resolved))
                    target.m_runtimeFiles.push_back(resolved);
            }
            for (auto lib : artifact->systemLibs)
            {
                if (tc.m_kind == CompilerKind::MSVC && fs::path(lib).extension().empty())
                    lib += ".lib";
                target.m_link.m_libraries.push_back(lib);
            }
            for (const auto& sub : dep.dependencies)
                target.m_publicDependencies.push_back(packageTarget(sub));
            if (std::any_of(
                    ws.m_targets.begin(),
                    ws.m_targets.end(),
                    [&](const Target& t)
                    {
                        return t.m_name == target.m_name;
                    }
                ))
                throw std::runtime_error("target name is reserved for an imported Rhodium package: " + target.m_name);
            imported.emplace(dep.name, target.m_name);
            ws.m_targets.push_back(std::move(target));
        }
        for (std::size_t i = 0; i < originalCount; ++i)
        {
            auto& target = ws.m_targets[i];
            for (const auto* names : {&target.m_packages, &target.m_publicPackages})
            {
                auto& deps = names == &target.m_packages ? target.m_privateDependencies : target.m_publicDependencies;
                for (const auto& name : *names)
                {
                    const auto found = imported.find(name);
                    if (found == imported.end())
                        throw std::runtime_error("target " + target.m_name + " uses undeclared package: " + name);
                    deps.push_back(found->second);
                }
            }
        }
        BuildModel::validateWorkspace(ws);
    }
} // namespace axlum
