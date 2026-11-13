/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "graph/NativeOptions.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace axlum
{
    CompileOptions NativeOptions::effectiveCompile(const CompileOptions& target, const CompileOptions& module)
    {
        auto result = target;
        if (!module.m_cppStandard.empty())
            result.m_cppStandard = module.m_cppStandard;
        if (!module.m_cStandard.empty())
            result.m_cStandard = module.m_cStandard;
        for (auto member :
             {&CompileOptions::m_optimization,
              &CompileOptions::m_symbols,
              &CompileOptions::m_warnings,
              &CompileOptions::m_runtime,
              &CompileOptions::m_inlining,
              &CompileOptions::m_pchHeader,
              &CompileOptions::m_pchSource,
              &CompileOptions::m_pchFile,
              &CompileOptions::m_pchObject})
            if (!(module.*member).empty())
                result.*member = module.*member;
        for (auto member :
             {&CompileOptions::m_warningsAsErrors,
              &CompileOptions::m_exceptions,
              &CompileOptions::m_rtti,
              &CompileOptions::m_lto,
              &CompileOptions::m_pic,
              &CompileOptions::m_pchEnabled,
              &CompileOptions::m_functionLevelLinking,
              &CompileOptions::m_runtimeChecks,
              &CompileOptions::m_stringPooling,
              &CompileOptions::m_intrinsicFunctions,
              &CompileOptions::m_bufferSecurityCheck,
              &CompileOptions::m_debugCRT,
              &CompileOptions::m_sharedPch,
              &CompileOptions::m_documentation})
            if ((module.*member).has_value())
                result.*member = module.*member;
        if (!module.m_sanitizers.empty())
            result.m_sanitizers = module.m_sanitizers;
        result.m_cFlags.insert(result.m_cFlags.end(), module.m_cFlags.begin(), module.m_cFlags.end());
        result.m_cxxFlags.insert(result.m_cxxFlags.end(), module.m_cxxFlags.begin(), module.m_cxxFlags.end());
        return result;
    }

    bool NativeOptions::symbolsEnabled(const CompileOptions& options, Configuration config)
    {
        return options.m_symbols.empty() ? (BuildModel::isDebug(config) || config.m_name == "Development") : options.m_symbols != "off";
    }

    std::string NativeOptions::runtimePolicy(const CompileOptions& options)
    {
        return options.m_runtime.empty() ? "dynamic" : options.m_runtime;
    }

    namespace
    {
        std::string optimizationMode(const CompileOptions& c, Configuration config)
        {
            return c.m_optimization.empty() ? (config == Configuration::s_debug ? "off" : "speed") : c.m_optimization;
        }

        void oneOf(const std::string& value, std::initializer_list<const char*> choices, const std::string& label)
        {
            if (value.empty())
                return;
            if (std::none_of(
                    choices.begin(),
                    choices.end(),
                    [&](const char* choice)
                    {
                        return value == choice;
                    }
                ))
                throw std::runtime_error("invalid " + label + ": " + value);
        }
    } // namespace

    void NativeOptions::validateNativeOptions(const Target& t, const Toolchain& tc)
    {
        oneOf(t.m_compile.m_optimization, {"off", "on", "speed", "size", "full"}, "optimization");
        oneOf(t.m_compile.m_symbols, {"on", "off", "full"}, "symbols");
        oneOf(t.m_compile.m_warnings, {"off", "default", "extra", "all"}, "warnings");
        oneOf(t.m_compile.m_runtime, {"static", "dynamic"}, "runtime");
        oneOf(t.m_compile.m_inlining, {"off", "default", "on", "aggressive"}, "inlining");
        const auto& c = t.m_compile;
        for (const auto& name : c.m_sanitizers)
            oneOf(name, {"address", "thread", "memory", "undefined"}, "sanitizer");
        auto has = [&](const char* value)
        {
            return std::find(c.m_sanitizers.begin(), c.m_sanitizers.end(), value) != c.m_sanitizers.end();
        };
        if ((has("address") && (has("thread") || has("memory"))) || (has("thread") && has("memory")))
            throw std::runtime_error("incompatible sanitizer combination: " + t.m_name);
        if (tc.m_kind == CompilerKind::MSVC)
        {
            if (std::any_of(
                    c.m_sanitizers.begin(),
                    c.m_sanitizers.end(),
                    [](const auto& n)
                    {
                        return n != "address";
                    }
                ))
                throw std::runtime_error("MSVC supports only the address sanitizer: " + t.m_name);
            if (has("address") && (c.m_runtimeChecks.value_or(false) || t.m_link.m_incremental.value_or(false)))
                throw std::runtime_error("MSVC address sanitizer conflicts with runtime checks/incremental link: " + t.m_name);
            if (c.m_runtimeChecks.value_or(false) && !c.m_optimization.empty() && c.m_optimization != "off")
                throw std::runtime_error("MSVC runtime checks require optimize Off: " + t.m_name);
        }
        else
        {
            if (has("memory") && (tc.m_kind != CompilerKind::Clang || tc.m_platform != "linux"))
                throw std::runtime_error("MemorySanitizer requires a Linux Clang toolchain: " + t.m_name);
            if (c.m_runtimeChecks.value_or(false) || c.m_debugCRT.has_value())
                throw std::runtime_error("runtimechecks/debugcrt require MSVC: " + t.m_name);
        }
        if ((!t.m_link.m_frameworks.empty() || !t.m_link.m_frameworkDirectories.empty()) && tc.m_platform != "macos" && !tc.m_platform.starts_with("ios"))
            throw std::runtime_error("frameworks require an Apple toolchain: " + t.m_name);
        oneOf(t.m_link.m_debugLink, {"default", "full", "fastlink"}, "debug link policy");
        if (!t.m_link.m_debugLink.empty() && tc.m_kind != CompilerKind::MSVC)
            throw std::runtime_error("debuglink requires MSVC");
        oneOf(t.m_link.m_subsystem, {"console", "windows"}, "subsystem");
        if ((!t.m_link.m_importLibrary || !t.m_link.m_exports.empty()) && t.m_kind != TargetKind::SharedLibrary)
            throw std::runtime_error("exports/importlibrary require a shared library: " + t.m_name);
        if (t.m_compile.m_lto.value_or(false) && t.m_link.m_incremental.value_or(false))
            throw std::runtime_error("LTO and incremental linking cannot both be enabled: " + t.m_name);
        if (t.m_kind == TargetKind::StaticLibrary &&
            (t.m_link.m_incremental.value_or(false) || !t.m_link.m_subsystem.empty() || !t.m_link.m_entryPoint.empty() ||
             !t.m_link.m_moduleDefinition.empty() || !t.m_link.m_manifest.empty() || !t.m_link.m_delayLoadLibraries.empty()))
            throw std::runtime_error("executable/shared-library linker settings cannot be used on a static library: " + t.m_name);
        if (tc.m_kind != CompilerKind::MSVC &&
            (t.m_link.m_incremental.value_or(false) || !t.m_link.m_delayLoadLibraries.empty() || !t.m_link.m_moduleDefinition.empty() ||
             !t.m_link.m_manifest.empty() || !t.m_link.m_entryPoint.empty() || t.m_link.m_subsystem == "windows" || !t.m_link.m_exports.empty() ||
             !t.m_link.m_importLibrary || !t.m_icon.empty()))
            throw std::runtime_error("Windows linker settings require the MSVC backend: " + t.m_name);
        if (t.m_compile.m_documentation.value_or(false) && tc.m_kind != CompilerKind::MSVC)
            throw std::runtime_error("cppdocumentation requires MSVC XDC documentation tools: " + t.m_name);
        if (!t.m_compile.m_pchFile.empty() && !t.m_compile.m_pchSource.empty())
            throw std::runtime_error("external pchfile cannot be combined with pchsource");
        if (!t.m_compile.m_pchObject.empty() && t.m_compile.m_pchFile.empty())
            throw std::runtime_error("pchobject requires pchfile");
        if ((!t.m_compile.m_pchFile.empty() || !t.m_compile.m_pchObject.empty()) && t.m_compile.m_pchHeader.empty())
            throw std::runtime_error("external PCH requires pchheader");
        if (!t.m_compile.m_pchObject.empty() && tc.m_kind != CompilerKind::MSVC)
            throw std::runtime_error("pchobject is an MSVC creator object");
        if (t.m_compile.m_pchEnabled.value_or(!t.m_compile.m_pchHeader.empty()) && t.m_compile.m_pchHeader.empty())
            throw std::runtime_error("enabled PCH requires pchheader: " + t.m_name);
        if (!t.m_compile.m_pchSource.empty() && t.m_compile.m_pchHeader.empty())
            throw std::runtime_error("pchsource requires pchheader: " + t.m_name);
        if (!t.m_compile.m_pchSource.empty())
        {
            const auto ext = fs::path(t.m_compile.m_pchSource).extension().string();
            if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".C")
                throw std::runtime_error("pchsource must be a C++ source file: " + t.m_name);
        }
    }

    std::vector<std::string> NativeOptions::nativeCompileFlags(const CompileOptions& c, const Toolchain& tc, Configuration config, bool cpp, bool shared)
    {
        std::vector<std::string> flags;
        const bool msvc = tc.m_kind == CompilerKind::MSVC;
        const auto optimization = optimizationMode(c, config);
        if (msvc && c.m_runtimeChecks.value_or(false) && optimization != "off")
            throw std::runtime_error("runtimechecks requires optimize Off for this configuration");
        const auto warning = c.m_warnings.empty() ? "default" : c.m_warnings;
        if (msvc)
        {
            flags = {"/nologo", "/utf-8"};
            flags.push_back(optimization == "off" ? "/Od" : optimization == "size" ? "/O1" : optimization == "full" ? "/Ox" : "/O2");
            flags.push_back(warning == "off" ? "/W0" : warning == "extra" ? "/W4" : warning == "all" ? "/Wall" : "/W3");
            flags.push_back(
                NativeOptions::runtimePolicy(c) == "static" ? (c.m_debugCRT.value_or(BuildModel::isDebug(config)) ? "/MTd" : "/MT")
                                                            : (c.m_debugCRT.value_or(BuildModel::isDebug(config)) ? "/MDd" : "/MD")
            );
            if (NativeOptions::symbolsEnabled(c, config))
                flags.push_back("/Z7");
            if (c.m_warningsAsErrors.has_value())
                flags.push_back(*c.m_warningsAsErrors ? "/WX" : "/WX-");
            if (cpp)
            {
                flags.push_back("/Zc:__cplusplus");
                if (c.m_exceptions.value_or(true))
                    flags.push_back("/EHsc");
                else
                {
                    flags.push_back("/EHs-c-");
                    flags.push_back("/D_HAS_EXCEPTIONS=0");
                }
                flags.push_back(c.m_rtti.value_or(true) ? "/GR" : "/GR-");
            }
            else
                flags.push_back("/TC");
            if (c.m_lto.value_or(false))
                flags.push_back("/GL");
            if (!c.m_inlining.empty() && c.m_inlining != "default")
                flags.push_back(c.m_inlining == "off" ? "/Ob0" : c.m_inlining == "aggressive" ? "/Ob3" : "/Ob2");
            for (const auto& [option, on, off] : std::vector<std::tuple<std::optional<bool>, std::string, std::string>>{
                     {c.m_functionLevelLinking, "/Gy", "/Gy-"},
                     {c.m_stringPooling, "/GF", "/GF-"},
                     {c.m_intrinsicFunctions, "/Oi", "/Oi-"},
                     {c.m_bufferSecurityCheck, "/GS", "/GS-"}
                 })
                if (option.has_value())
                    flags.push_back(*option ? on : off);
            if (c.m_runtimeChecks.value_or(false))
                flags.push_back("/RTC1");
            if (!c.m_sanitizers.empty())
                flags.push_back("/fsanitize=address");
        }
        else
        {
            flags.push_back(optimization == "off" ? "-O0" : optimization == "size" ? "-Os" : optimization == "full" ? "-O3" : "-O2");
            if (warning == "off")
                flags.push_back("-w");
            else
            {
                flags.push_back("-Wall");
                if (warning == "extra" || warning == "all")
                    flags.push_back("-Wextra");
                if (warning == "all")
                    flags.push_back("-Wpedantic");
            }
            if (NativeOptions::symbolsEnabled(c, config))
                flags.push_back("-g");
            if (c.m_warningsAsErrors.has_value())
                flags.push_back(*c.m_warningsAsErrors ? "-Werror" : "-Wno-error");
            if (cpp)
            {
                flags.push_back(c.m_exceptions.value_or(true) ? "-fexceptions" : "-fno-exceptions");
                flags.push_back(c.m_rtti.value_or(true) ? "-frtti" : "-fno-rtti");
            }
            if (c.m_lto.value_or(false))
                flags.push_back("-flto");
            if (c.m_pic.value_or(shared))
                flags.push_back("-fPIC");
            if (!c.m_inlining.empty() && c.m_inlining != "default")
                flags.push_back(c.m_inlining == "off" ? "-fno-inline" : "-finline-functions");
            if (c.m_functionLevelLinking.has_value())
                flags.push_back(*c.m_functionLevelLinking ? "-ffunction-sections" : "-fno-function-sections");
            if (c.m_stringPooling.has_value())
                flags.push_back(
                    tc.m_kind == CompilerKind::Clang ? (*c.m_stringPooling ? "-fmerge-all-constants" : "-fno-merge-all-constants")
                                                     : (*c.m_stringPooling ? "-fmerge-constants" : "-fno-merge-constants")
                );
            if (c.m_intrinsicFunctions.has_value())
                flags.push_back(*c.m_intrinsicFunctions ? "-fbuiltin" : "-fno-builtin");
            if (c.m_bufferSecurityCheck.has_value())
                flags.push_back(*c.m_bufferSecurityCheck ? "-fstack-protector-strong" : "-fno-stack-protector");
            for (const auto& name : c.m_sanitizers)
                flags.push_back("-fsanitize=" + name);
            if (!c.m_sanitizers.empty())
                flags.push_back("-fno-omit-frame-pointer");
        }
        if (!BuildModel::isDebug(config))
            flags.push_back(msvc ? "/DNDEBUG" : "-DNDEBUG");
        const auto standard = cpp ? c.m_cppStandard : c.m_cStandard;
        oneOf(
            standard,
            cpp ? std::initializer_list<const char*>{"c++14", "c++17", "c++20", "c++23", "latest"} : std::initializer_list<const char*>{"c11", "c17"},
            "language standard"
        );
        const auto dialect = cpp && standard == "latest" ? ToolchainDiscovery::latestCppDialect(tc) : standard;
        flags.push_back(msvc ? (cpp && (standard == "c++23" || standard == "latest") ? "/std:c++latest" : "/std:" + dialect) : "-std=" + dialect);
        flags.insert(flags.end(), tc.m_targetFlags.begin(), tc.m_targetFlags.end());
        const auto& custom = cpp ? c.m_cxxFlags : c.m_cFlags;
        flags.insert(flags.end(), custom.begin(), custom.end());
        return flags;
    }

    std::vector<std::string> NativeOptions::nativeLinkFlags(const Target& t, const Toolchain& tc, Configuration config)
    {
        std::vector<std::string> flags;
        const auto& c = t.m_compile;
        if (tc.m_kind == CompilerKind::MSVC)
        {
            flags.push_back(t.m_link.m_incremental.value_or(false) ? "/INCREMENTAL" : "/INCREMENTAL:NO");
            if (!t.m_link.m_incremental.value_or(false))
            {
                const bool optimize = optimizationMode(c, config) != "off";
                flags.push_back(optimize ? "/OPT:REF" : "/OPT:NOREF");
                flags.push_back(optimize ? "/OPT:ICF" : "/OPT:NOICF");
            }
            if (NativeOptions::symbolsEnabled(c, config))
                flags.push_back(
                    t.m_link.m_debugLink == "fastlink"                        ? "/DEBUG:FASTLINK"
                    : t.m_link.m_debugLink == "full" || c.m_symbols == "full" ? "/DEBUG:FULL"
                                                                              : "/DEBUG"
                );
            if (c.m_lto.value_or(false))
                flags.push_back("/LTCG");
            if (!t.m_link.m_subsystem.empty())
                flags.push_back(t.m_link.m_subsystem == "windows" ? "/SUBSYSTEM:WINDOWS" : "/SUBSYSTEM:CONSOLE");
            if (!t.m_link.m_entryPoint.empty())
                flags.push_back("/ENTRY:" + t.m_link.m_entryPoint);
            for (const auto& dll : t.m_link.m_delayLoadLibraries)
                flags.push_back("/DELAYLOAD:" + dll);
            if (!t.m_link.m_delayLoadLibraries.empty())
                flags.push_back("delayimp.lib");
        }
        else
        {
            if (NativeOptions::symbolsEnabled(c, config))
                flags.push_back("-g");
            for (const auto& name : c.m_sanitizers)
                flags.push_back("-fsanitize=" + name);
            if (c.m_lto.value_or(false))
            {
                flags.push_back("-flto");
                const auto optimization = optimizationMode(c, config);
                flags.push_back(optimization == "off" ? "-O0" : optimization == "size" ? "-Os" : optimization == "full" ? "-O3" : "-O2");
            }
            flags.insert(flags.end(), tc.m_targetFlags.begin(), tc.m_targetFlags.end());
            if (NativeOptions::runtimePolicy(c) == "static")
                flags.insert(flags.end(), {"-static-libgcc", "-static-libstdc++"});
        }
        return flags;
    }
} // namespace axlum
