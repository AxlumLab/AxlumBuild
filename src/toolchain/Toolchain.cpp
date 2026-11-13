/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "toolchain/Toolchain.hpp"
#include "core/Process.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <rhodium/core/Environment.hpp>
#include <rhodium/diagnostics/DiagnosticSink.hpp>
#include <rhodium/toolchain/ToolchainDetector.hpp>
#include <rhodium/toolchain/ToolchainRegistry.hpp>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        std::string getEnv(const char* key)
        {
            const char* value = std::getenv(key);
            return value ? std::string(value) : std::string{};
        }

        std::string needed(const std::string& tool)
        {
            const auto path = Process::findExecutable(tool);
            if (path.empty())
                throw std::runtime_error("required tool '" + tool + "' not found on PATH");
            return path;
        }
    } // namespace

    std::string ToolchainDiscovery::hostTriple()
    {
#if defined(_WIN32) && defined(_M_X64)
        return "x86_64-pc-windows-msvc";
#elif defined(_WIN32) && defined(_M_ARM64)
        return "aarch64-pc-windows-msvc";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
        return "arm64-apple-macosx";
#elif defined(__APPLE__) && defined(__x86_64__)
        return "x86_64-apple-macosx";
#elif defined(__linux__) && defined(__aarch64__)
        return "aarch64-unknown-linux-gnu";
#elif defined(__linux__) && defined(__x86_64__)
        return "x86_64-unknown-linux-gnu";
#else
        throw std::runtime_error("AxlumBuild host platform/architecture is unsupported");
#endif
    }

    namespace
    {
        class Diagnostics final : public rhodium::DiagnosticSink
        {
          public:
            bool m_failed = false;

            void report(const rhodium::Diagnostic& diagnostic) override
            {
                if (diagnostic.level == rhodium::DiagnosticLevel::Error)
                    m_failed = true;
                if (diagnostic.level == rhodium::DiagnosticLevel::Error || diagnostic.level == rhodium::DiagnosticLevel::Warning)
                    std::cerr << "[toolchain] " << diagnostic.message << '\n';
            }
        };

        std::vector<rhodium::ToolchainProfile> discoverProfiles(const fs::path& workspace)
        {
            Diagnostics diagnostics;
            auto profiles = rhodium::ToolchainDetector::host(workspace, diagnostics);
            // VsDevCmd emits the entire parent environment. Persist toolchain variables, not unrelated session state.
            for (auto& p : profiles)
            {
                std::map<std::string, std::string> relevant;
                for (const auto& [key, value] : p.environment)
                {
                    auto upper = key;
                    std::transform(
                        upper.begin(),
                        upper.end(),
                        upper.begin(),
                        [](unsigned char c)
                        {
                            return static_cast<char>(std::toupper(c));
                        }
                    );
                    const std::vector<std::string> allowed = {
                        "PATH",
                        "INCLUDE",
                        "LIB",
                        "LIBPATH",
                        "VCINSTALLDIR",
                        "VCTOOLSINSTALLDIR",
                        "VCTOOLSVERSION",
                        "VSINSTALLDIR",
                        "WINDOWSSDKDIR",
                        "WINDOWSSDKVERSION",
                        "WINDOWSSDKBINPATH",
                        "WINDOWSSDKVERBINPATH",
                        "UNIVERSALCRTSDKDIR",
                        "UCRTVERSION",
                        "CC",
                        "CXX",
                        "CL",
                        "_CL_",
                        "CFLAGS",
                        "CXXFLAGS",
                        "LDFLAGS",
                        "RCFLAGS"
                    };
                    if (std::find(allowed.begin(), allowed.end(), upper) != allowed.end())
                        relevant[p.platform == "windows" ? upper : key] = value;
                }
                p.environment = std::move(relevant);
            }
            for (const auto& loaded : rhodium::ToolchainRegistry::load(workspace, diagnostics))
            {
                if (loaded.source == rhodium::ToolchainSource::AutoDetected)
                    continue;
                auto old = std::find_if(
                    profiles.begin(),
                    profiles.end(),
                    [&](const auto& p)
                    {
                        return p.id == loaded.profile.id;
                    }
                );
                if (old == profiles.end())
                    profiles.push_back(loaded.profile);
                else
                    *old = loaded.profile;
            }
            if (diagnostics.m_failed)
                throw std::runtime_error("Rhodium toolchain discovery/configuration failed");
            return profiles;
        }

        bool supported(const rhodium::ToolchainProfile& p)
        {
            return (p.targetArch == "x64" || p.targetArch == "x86_64" || p.targetArch == "arm64" || p.targetArch == "aarch64") &&
                   ((p.platform == "windows" && p.compilerKind == "msvc") ||
                    ((p.platform == "linux" || p.platform == "macos" || p.platform == "android" || p.platform == "ios") &&
                     (p.compilerKind == "gcc" || p.compilerKind == "clang")));
        }
    } // namespace

    std::vector<ToolchainInfo> ToolchainDiscovery::listToolchains(const fs::path& workspace)
    {
        std::vector<ToolchainInfo> result;
        for (const auto& profile : discoverProfiles(workspace))
        {
            std::string missing;
            for (const auto& item : profile.missing)
            {
                if (!missing.empty())
                    missing += ", ";
                missing += item;
            }
            result.push_back(
                {profile.id,
                 profile.cxxCompiler.string(),
                 profile.compilerVersion,
                 profile.sdkVersion,
                 profile.platform,
                 profile.targetArch,
                 missing,
                 profile.complete}
            );
        }
        return result;
    }

    std::string ToolchainDiscovery::sdkReport(const fs::path& workspace)
    {
        using Json = nlohmann::json;
        auto profiles = Json::array();
        for (const auto& p : discoverProfiles(workspace))
            profiles.push_back(
                {{"id", p.id},
                 {"platform", p.platform},
                 {"architecture", p.targetArch},
                 {"compilerKind", p.compilerKind},
                 {"compiler", p.compiler.generic_string()},
                 {"cxxCompiler", p.cxxCompiler.generic_string()},
                 {"compilerVersion", p.compilerVersion},
                 {"linker", p.linker.generic_string()},
                 {"archiver", p.archiver.generic_string()},
                 {"sdkKind", p.sdkKind},
                 {"sdkVersion", p.sdkVersion},
                 {"sdkRoot", p.sdkRoot.generic_string()},
                 {"sysroot", p.sysroot.generic_string()},
                 {"targetTriple", p.targetTriple},
                 {"androidSdkRoot", p.androidSdkRoot.generic_string()},
                 {"androidNdkRoot", p.androidNdkRoot.generic_string()},
                 {"androidApi", p.androidApi},
                 {"androidAbi", p.androidAbi},
                 {"jdkRoot", p.jdkRoot.generic_string()},
                 {"includeDirectories", p.systemIncludeDirs},
                 {"libraryDirectories", p.systemLibDirs},
                 {"complete", p.complete},
                 {"missing", p.missing},
                 {"supported", supported(p)}}
            );
        return Json{{"provider", "Rhodium"}, {"host", rhodium::Environment::hostPlatformName()}, {"profiles", profiles}}.dump(2) + "\n";
    }

    Toolchain ToolchainDiscovery::discoverToolchain(
        const std::string& requested,
        const fs::path& workspace,
        const std::string& profileId,
        const std::string& architecture,
        const std::string& platform
    )
    {
        auto profiles = discoverProfiles(workspace);
        const auto arch = BuildModel::normalizeArchitecture(architecture);
        const auto hostPlatform = ToolchainDiscovery::hostTriple().find("windows") != std::string::npos ? "windows"
                                  : ToolchainDiscovery::hostTriple().find("apple") != std::string::npos ? "macos"
                                                                                                        : "linux";
        const auto wantedPlatform = platform.empty() ? hostPlatform : platform;
        if (wantedPlatform != "windows" && wantedPlatform != "linux" && wantedPlatform != "macos" && wantedPlatform != "android" && wantedPlatform != "ios" &&
            wantedPlatform != "ios-simulator")
            throw std::runtime_error("unsupported platform: " + wantedPlatform);
        if (wantedPlatform == "ios" && arch != "arm64")
            throw std::runtime_error("iOS devices require arm64; use ios-simulator for x64");
        auto matches = [&](const auto& p)
        {
            const auto actual = p.platform == "ios" && p.sdkKind == "iphonesimulator" ? "ios-simulator" : p.platform;
            return supported(p) && BuildModel::normalizeArchitecture(p.targetArch) == arch && actual == wantedPlatform;
        };
        auto selectedId = profileId;
        Diagnostics diagnostics;
        rhodium::LocalToolchainConfig config;
        if (!rhodium::ToolchainRegistry::readConfig(workspace, config, diagnostics))
            throw std::runtime_error("cannot read Rhodium toolchain preference");
        if (selectedId.empty() && requested == "auto")
            selectedId = config.preferredToolchain;
        // Preserve explicit compiler-family selection; do not silently pick an unrelated compiler.
        if (selectedId.empty() && requested == "auto")
        {
            const auto cxx = getEnv("CXX");
            if (!cxx.empty())
            {
                auto found = std::find_if(
                    profiles.begin(),
                    profiles.end(),
                    [&](const auto& p)
                    {
                        return matches(p) && p.complete && p.cxxCompiler.filename() == fs::path(cxx).filename();
                    }
                );
                if (found != profiles.end())
                    selectedId = found->id;
            }
        }
        const rhodium::ToolchainProfile* selected = nullptr;
        for (const auto& p : profiles)
        {
            if (!selectedId.empty() && p.id != selectedId)
                continue;
            if (requested != "auto" && requested != p.compilerKind)
                continue;
            if (!matches(p) || !p.complete)
                continue;
            if (!fs::is_regular_file(p.compiler) || !fs::is_regular_file(p.cxxCompiler) || !fs::is_regular_file(p.linker) || !fs::is_regular_file(p.archiver))
                continue;
            selected = &p;
            break;
        }
        if (!selected)
        {
            std::string available;
            for (const auto& p : profiles)
                available += "\n  " + p.id + " (" + (p.complete ? "complete" : "incomplete") + ")";
            throw std::runtime_error(
                "no supported complete toolchain matches platform=" + wantedPlatform + " architecture=" + arch + " compiler='" + requested + "' profile='" +
                selectedId + "'. Run axlum-build doctor. Detected:" + available
            );
        }
        Toolchain tool;
        tool.m_kind = selected->compilerKind == "msvc" ? CompilerKind::MSVC : selected->compilerKind == "clang" ? CompilerKind::Clang : CompilerKind::GCC;
        tool.m_id = selected->id;
        tool.m_compilerVersion = selected->compilerVersion;
        tool.m_sdkVersion = selected->sdkVersion;
        tool.m_host = ToolchainDiscovery::hostTriple();
        tool.m_platform = wantedPlatform;
        tool.m_architecture = arch;
        tool.m_androidAbi = selected->androidAbi;
        tool.m_target = selected->targetTriple.empty()
                            ? ((arch == "arm64" ? "aarch64" : "x86_64") + std::string(tool.m_platform == "windows" ? "-pc-windows-msvc" : "-unknown-linux-gnu"))
                            : selected->targetTriple;
        tool.m_cCompiler = selected->compiler.string();
        tool.m_cppCompiler = selected->cxxCompiler.string();
        tool.m_linker = selected->linker.string();
        tool.m_archiver = selected->archiver.string();
        tool.m_resourceCompiler = selected->resourceCompiler.string();
        tool.m_manifestTool = selected->manifestTool.string();
        if (tool.m_kind == CompilerKind::MSVC)
        {
            for (auto directory = fs::path(tool.m_cppCompiler).parent_path(); !directory.empty() && directory != directory.parent_path();
                 directory = directory.parent_path())
                if (fs::is_regular_file(directory / "MSBuild/Current/Bin/MSBuild.exe"))
                {
                    tool.m_msbuild = (directory / "MSBuild/Current/Bin/MSBuild.exe").string();
                    break;
                }
            if (tool.m_msbuild.empty())
                tool.m_msbuild = Process::findExecutable("MSBuild.exe");
            const auto compilerDir = fs::path(tool.m_cppCompiler).parent_path();
            for (const auto& directory : {compilerDir, compilerDir.parent_path() / "x64", compilerDir.parent_path() / "arm64"})
                if (fs::is_regular_file(directory / "xdcmake.exe"))
                {
                    tool.m_documentationTool = (directory / "xdcmake.exe").string();
                    break;
                }
            if (tool.m_documentationTool.empty())
                tool.m_documentationTool = Process::findExecutable("xdcmake");
        }
        if (tool.m_platform == "android")
        {
            // NDK Windows launchers are .cmd files; execute clang directly with its target.
            if (fs::is_regular_file(fs::path(tool.m_cCompiler).parent_path() / "clang.exe"))
            {
                tool.m_cCompiler = (fs::path(tool.m_cCompiler).parent_path() / "clang.exe").string();
                tool.m_cppCompiler = (fs::path(tool.m_cppCompiler).parent_path() / "clang++.exe").string();
                tool.m_linker = tool.m_cppCompiler;
            }
            tool.m_targetFlags = {"--target=" + tool.m_target};
        }
        if (tool.m_platform == "macos" || tool.m_platform.starts_with("ios"))
            tool.m_targetFlags = {"-target", tool.m_target};
        if ((tool.m_platform == "macos" || tool.m_platform.starts_with("ios")) && (!selected->sysroot.empty() || !selected->sdkRoot.empty()))
            tool.m_targetFlags.insert(
                tool.m_targetFlags.end(), {"-isysroot", (selected->sysroot.empty() ? selected->sdkRoot : selected->sysroot).generic_string()}
            );
        else if (!selected->sysroot.empty() && tool.m_kind != CompilerKind::MSVC)
            tool.m_targetFlags.push_back("--sysroot=" + selected->sysroot.generic_string());
        if (tool.m_platform == "macos" && !selected->deploymentTarget.empty())
            tool.m_targetFlags.push_back("-mmacosx-version-min=" + selected->deploymentTarget);
        if (tool.m_platform.starts_with("ios") && !selected->deploymentTarget.empty())
            tool.m_targetFlags.push_back(
                std::string(tool.m_platform == "ios-simulator" ? "-mios-simulator-version-min=" : "-miphoneos-version-min=") + selected->deploymentTarget
            );
        tool.m_ninja = selected->ninja.empty() ? needed("ninja") : selected->ninja.string();
        if (!fs::is_regular_file(tool.m_ninja))
            throw std::runtime_error("selected toolchain has no usable Ninja: " + tool.m_id);
        tool.m_cmake = selected->cmake.empty() ? Process::findExecutable("cmake") : selected->cmake.string();
        tool.m_dotnet = Process::findExecutable("dotnet");
        tool.m_systemIncludes = selected->systemIncludeDirs;
        tool.m_systemLibraries = selected->systemLibDirs;
        tool.m_environment = selected->environment;
        if (tool.m_platform == "android")
        {
            tool.m_environment["ANDROID_SDK_ROOT"] = selected->androidSdkRoot.generic_string();
            tool.m_environment["ANDROID_NDK_ROOT"] = selected->androidNdkRoot.generic_string();
            tool.m_environment["RHODIUM_ANDROID_API"] = selected->androidApi;
        }
        if (tool.m_kind == CompilerKind::MSVC)
        {
            auto has = [&](std::string wanted)
            {
                return std::any_of(
                    tool.m_environment.begin(),
                    tool.m_environment.end(),
                    [&](const auto& item)
                    {
                        auto key = item.first;
                        std::transform(
                            key.begin(),
                            key.end(),
                            key.begin(),
                            [](unsigned char c)
                            {
                                return static_cast<char>(std::toupper(c));
                            }
                        );
                        return key == wanted;
                    }
                );
            };
            auto join = [](const auto& paths)
            {
                std::string result;
                for (const auto& path : paths)
                {
                    if (!result.empty())
                        result += ';';
                    result += path.string();
                }
                return result;
            };
            if (!has("INCLUDE"))
                tool.m_environment["INCLUDE"] = join(tool.m_systemIncludes);
            if (!has("LIB"))
                tool.m_environment["LIB"] = join(tool.m_systemLibraries);
            if (!has("LIBPATH"))
                tool.m_environment["LIBPATH"] = join(tool.m_systemLibraries);
            if (!has("PATH"))
            {
                auto dirs = selected->binaryDirs;
                dirs.push_back(fs::path(tool.m_cppCompiler).parent_path());
                if (!tool.m_resourceCompiler.empty())
                    dirs.push_back(fs::path(tool.m_resourceCompiler).parent_path());
                tool.m_environment["PATH"] = join(dirs) + ";" + getEnv("PATH");
            }
        }
        for (auto* path :
             {&tool.m_cCompiler,
              &tool.m_cppCompiler,
              &tool.m_linker,
              &tool.m_archiver,
              &tool.m_resourceCompiler,
              &tool.m_manifestTool,
              &tool.m_documentationTool,
              &tool.m_msbuild,
              &tool.m_ninja,
              &tool.m_cmake,
              &tool.m_dotnet})
            if (!path->empty())
                *path = fs::path(*path).lexically_normal().generic_string();
        if (tool.m_platform == "android")
            for (auto& [key, value] : tool.m_environment)
                if (key == "PATH" || key == "Path")
                    std::replace(value.begin(), value.end(), '\\', '/');
        tool.m_environment["CC"] = tool.m_cCompiler;
        tool.m_environment["CXX"] = tool.m_cppCompiler;
        // Capture inherited compiler switches/search paths even for hand-written profiles.
        // Persist empty values too, so an old value cannot survive in a child environment.
        for (const auto* key : {"CL", "_CL_", "CFLAGS", "CXXFLAGS", "LDFLAGS", "RCFLAGS", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"})
        {
            const bool supplied = std::any_of(
                tool.m_environment.begin(),
                tool.m_environment.end(),
                [&](const auto& entry)
                {
                    auto name = entry.first;
                    std::transform(
                        name.begin(),
                        name.end(),
                        name.begin(),
                        [](unsigned char c)
                        {
                            return static_cast<char>(std::toupper(c));
                        }
                    );
                    return name == key;
                }
            );
            if (!supplied)
                tool.m_environment[key] = getEnv(key);
        }
        tool.m_profile = std::make_shared<rhodium::ToolchainProfile>(*selected);
        tool.m_profile->environment = tool.m_environment;
        const bool msvc = tool.m_kind == CompilerKind::MSVC;
        tool.m_extensionObject = msvc ? ".obj" : ".o";
        tool.m_extensionExecutable = msvc ? ".exe" : "";
        tool.m_extensionStatic = msvc ? ".lib" : ".a";
        tool.m_extensionShared = msvc ? ".dll" : (tool.m_platform == "macos" || tool.m_platform.starts_with("ios")) ? ".dylib" : ".so";
        return tool;
    }

    std::string ToolchainDiscovery::latestCppDialect(const Toolchain& tc)
    {
        if (tc.m_kind == CompilerKind::MSVC)
            return "latest";
        // Constructed planner models have no executable profile; real toolchains are probed.
        if (!tc.m_profile)
            return "c++2b";
        static std::mutex s_mutex;
        static std::map<std::string, std::string> s_cache;
        std::lock_guard lock(s_mutex);
        const auto key = tc.m_cppCompiler + tc.m_compilerVersion + std::to_string(fs::last_write_time(tc.m_cppCompiler).time_since_epoch().count());
        if (s_cache.contains(key))
            return s_cache.at(key);
        const auto base = fs::temp_directory_path();
        fs::path directory;
        for (unsigned i = 0;; ++i)
        {
            directory = base / ("axlum-cpp-dialect-" +
                                FileSystem::shortHash(key + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + std::to_string(i)));
            if (fs::create_directory(directory))
                break;
        }

        struct Cleanup
        {
            fs::path m_directory;

            ~Cleanup()
            {
                std::error_code ec;
                fs::remove(m_directory / "empty.cpp", ec);
                fs::remove(m_directory, ec);
            }
        } cleanup{directory};

        const auto source = directory / "empty.cpp";
        FileSystem::writeIfChanged(source, "// Compiler dialect capability check.\n");
        for (const auto* dialect : {"c++2d", "c++2c", "c++2b", "c++2a", "c++17", "c++14"})
        {
            if (Process::run(
                    {tc.m_cppCompiler, "-std=" + std::string(dialect), "-x", "c++", "-fsyntax-only", source.string()},
                    directory,
                    tc.m_environment,
                    [](std::string_view) {}
                ) == 0)
                return s_cache[key] = dialect;
        }
        throw std::runtime_error("compiler has no supported C++ dialect for Latest: " + tc.m_cppCompiler);
    }

    std::string ToolchainDiscovery::toolchainFingerprint(const Toolchain& tc, Configuration config)
    {
        std::string data = tc.m_profile ? rhodium::Toolchain::contentHash(*tc.m_profile, BuildModel::toString(config)) : tc.m_id;
        for (const auto& path :
             {tc.m_cCompiler, tc.m_cppCompiler, tc.m_linker, tc.m_archiver, tc.m_resourceCompiler, tc.m_manifestTool, tc.m_documentationTool})
        {
            data += "\n" + path;
            if (fs::is_regular_file(path))
                data += std::to_string(fs::file_size(path)) + std::to_string(fs::last_write_time(path).time_since_epoch().count());
        }
        // Detect servicing updates which keep compiler/SDK installation paths and version IDs.
        for (const auto& directories : {tc.m_systemIncludes, tc.m_systemLibraries})
            for (const auto& directory : directories)
            {
                data += "\n" + directory.generic_string();
                for (const auto* name :
                     {"vcruntime.h",
                      "stddef.h",
                      "Windows.h",
                      "sdkddkver.h",
                      "corecrt.h",
                      "kernel32.lib",
                      "ucrt.lib",
                      "libucrt.lib",
                      "libcmt.lib",
                      "msvcrt.lib"})
                {
                    const auto file = directory / name;
                    if (fs::is_regular_file(file))
                        data +=
                            file.generic_string() + std::to_string(fs::file_size(file)) + std::to_string(fs::last_write_time(file).time_since_epoch().count());
                }
            }
        for (const auto& [key, value] : tc.m_environment)
        {
            // PATH may differ between a Developer shell and a normal shell without changing the selected toolchain.
            std::string lower = key;
            std::transform(
                lower.begin(),
                lower.end(),
                lower.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
            if (lower != "path" && lower != "axlum_toolchain_fingerprint")
                data += "\n" + lower + "=" + value;
        }
        return FileSystem::shortHash(data);
    }
} // namespace axlum
