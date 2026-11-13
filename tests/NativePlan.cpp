/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "backend/Ide.hpp"
#include "core/Process.hpp"
#include "graph/Graph.hpp"
#include "graph/NativeOptions.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <set>

namespace ab = axlum;
namespace fs = std::filesystem;

namespace
{
    int s_failures = 0;

    void check(bool value, const std::string& label)
    {
        if (!value)
        {
            ++s_failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    }

    bool has(const std::vector<std::string>& args, const std::string& value)
    {
        return std::find(args.begin(), args.end(), value) != args.end();
    }

    void throws(const std::function<void()>& action, const std::string& text)
    {
        try
        {
            action();
            check(false, "expected " + text);
        }
        catch (const std::exception& error)
        {
            check(std::string(error.what()).find(text) != std::string::npos, error.what());
        }
    }
} // namespace

int main(int argc, char** argv)
{
    (void)argc;
    const auto root = fs::temp_directory_path() / ("axlum-native-plan-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto executable = fs::absolute(argv[0]);
    ab::FileSystem::writeIfChanged(root / "source.cpp", "int main() { return 0; }\n");
    ab::FileSystem::writeIfChanged(root / "module.cpp", "int module_function() { return 1; }\n");
    ab::FileSystem::writeIfChanged(root / "source.c", "int c_function(void) { return 1; }\n");
    ab::FileSystem::writeIfChanged(root / "Pch.hpp", "#pragma once\n#define PCH_MARKER 1\n");
    ab::FileSystem::writeIfChanged(root / "creator.cpp", "#include \"Pch.hpp\"\nint creator_symbol() { return 1; }\n");
    ab::FileSystem::writeIfChanged(root / "runtime.bin", "runtime data\n");
    ab::Workspace ws;
    ws.m_name = "NativePlan";
    ws.m_root = root;
    ab::Target target;
    target.m_name = "Native";
    target.m_kind = ab::TargetKind::Executable;
    target.m_sources = {"source.cpp", "source.c"};
    target.m_compile.m_optimization = "full";
    target.m_compile.m_symbols = "on";
    target.m_compile.m_warnings = "extra";
    target.m_compile.m_warningsAsErrors = true;
    target.m_compile.m_exceptions = false;
    target.m_compile.m_rtti = false;
    target.m_compile.m_runtime = "static";
    target.m_compile.m_lto = true;
    target.m_compile.m_pchHeader = "Pch.hpp";
    target.m_compile.m_pchSource = "creator.cpp";
    target.m_outputName = "renamed";
    target.m_outputDirectory = "products/${config}/${target}";
    target.m_objectDirectory = "objects/${target}";
    target.m_runtimeFiles = {"runtime.bin"};
    ws.m_targets = {target};
    ab::Module module;
    module.m_name = "Separate";
    module.m_targetName = "Native";
    module.m_sources = {"module.cpp"};
    module.m_compile.m_optimization = "size";
    module.m_compile.m_privateDefines = {"MODULE_LOCAL=1"};
    ws.m_modules = {module};
    try
    {
        for (const auto kind : {ab::CompilerKind::MSVC, ab::CompilerKind::GCC, ab::CompilerKind::Clang})
        {
            ab::Toolchain tc;
            tc.m_kind = kind;
            tc.m_cCompiler = tc.m_cppCompiler = tc.m_linker = tc.m_archiver = executable.string();
            tc.m_extensionObject = kind == ab::CompilerKind::MSVC ? ".obj" : ".o";
            tc.m_extensionExecutable = kind == ab::CompilerKind::MSVC ? ".exe" : "";
            const auto build = root / std::to_string(static_cast<int>(kind));
            const auto graph = ab::BuildPlanner::planBuild(ws, tc, build, ab::Configuration::s_release, executable);
            std::set<fs::path> pchs;
            for (const auto& action : graph.m_actions)
            {
                if (action.m_name.starts_with("PCH "))
                {
                    pchs.insert(action.m_outputs.front());
                    if (kind == ab::CompilerKind::MSVC)
                        check(has(action.m_argv, "/GL"), "MSVC PCH inherits LTO");
                    else
                        check(has(action.m_argv, "-flto"), "GNU/Clang PCH inherits LTO");
                    if (action.m_name == "PCH Native:Separate")
                        check(
                            std::find(action.m_inputs.begin(), action.m_inputs.end(), root / "creator.cpp") == action.m_inputs.end(),
                            "target PCH creator is not duplicated into another module"
                        );
                }
                if (action.m_compile && action.m_source.filename() == "source.c")
                    check(
                        std::none_of(
                            action.m_argv.begin(),
                            action.m_argv.end(),
                            [](const auto& arg)
                            {
                                return arg.starts_with("/Yu") || arg.starts_with("-include");
                            }
                        ),
                        "C action excludes C++ PCH"
                    );
                if (action.m_compile && action.m_source.filename() == "source.cpp")
                {
                    check(has(action.m_argv, kind == ab::CompilerKind::MSVC ? "/Ox" : "-O3"), "target optimization");
                    check(!has(action.m_argv, kind == ab::CompilerKind::MSVC ? "/DMODULE_LOCAL=1" : "-DMODULE_LOCAL=1"), "module definitions isolated");
                    if (kind == ab::CompilerKind::Clang)
                        check(has(action.m_argv, "-include-pch"), "Clang uses explicit PCH");
                    if (kind == ab::CompilerKind::GCC)
                        check(has(action.m_argv, "-Werror=invalid-pch"), "GCC rejects ignored PCH");
                }
                if (action.m_name == "Link Native")
                {
                    check(has(action.m_argv, kind == ab::CompilerKind::MSVC ? "/LTCG" : "-flto"), "LTO reaches linker");
                    if (kind != ab::CompilerKind::MSVC)
                    {
                        check(has(action.m_argv, "-Wl,-rpath,$ORIGIN"), "copied shared libraries are discoverable");
                        check(has(action.m_argv, "-static-libstdc++"), "static GNU runtime reaches linker");
                    }
                }
            }
            check(pchs.size() == 2, "PCH isolated per module and option set");
            const auto expected = build / "products" / "Release" / "Native" / ("renamed" + tc.m_extensionExecutable);
            check(graph.m_targetOutputs.at("Native").front() == expected, "custom target name/directory");
            auto grouped = ws;
            grouped.m_targets.front().m_linkType = "modular";
            grouped.m_modules.front().m_binaryModuleName = "Core";
            grouped.m_modules.front().m_apiDefine = "CORE_API";
            tc.m_extensionShared = kind == ab::CompilerKind::MSVC ? ".dll" : ".so";
            const auto modular = ab::BuildPlanner::planBuild(grouped, tc, build / "modular", ab::Configuration::s_release, executable);
            check(modular.m_targetKinds.at("Native__Core") == ab::TargetKind::SharedLibrary, "module becomes a shared binary group");
            bool producer = false, consumer = false;
            for (const auto& action : modular.m_actions)
                if (action.m_compile)
                {
                    if (action.m_owner == "Native__Core")
                    {
                        producer |=
                            has(action.m_argv,
                                kind == ab::CompilerKind::MSVC ? "/DCORE_API=__declspec(dllexport)" : "-DCORE_API=__attribute__((visibility(\"default\")))");
                        check(!has(action.m_argv, "/DCORE_API=__declspec(dllimport)"), "producer never imports its own API");
                    }
                    else
                        consumer |= has(action.m_argv, kind == ab::CompilerKind::MSVC ? "/DCORE_API=__declspec(dllimport)" : "-DCORE_API=");
                }
            check(producer && consumer, "API annotations distinguish producer and consumer");
        }

        // Focused supported-platform parity checks: no compiler/SDK invocation.
        {
            ab::Toolchain compiler;
            compiler.m_kind = ab::CompilerKind::MSVC;
            ab::CompileOptions options;
            options.m_optimization = "off";
            options.m_inlining = "aggressive";
            options.m_functionLevelLinking = true;
            options.m_runtimeChecks = true;
            options.m_stringPooling = true;
            options.m_intrinsicFunctions = false;
            options.m_bufferSecurityCheck = true;
            auto flags = ab::NativeOptions::nativeCompileFlags(options, compiler, ab::Configuration::s_debug, true, false);
            check(
                has(flags, "/Ob3") && has(flags, "/Gy") && has(flags, "/RTC1") && has(flags, "/GF") && has(flags, "/Oi-") && has(flags, "/GS"),
                "typed MSVC policies"
            );
            compiler.m_kind = ab::CompilerKind::Clang;
            compiler.m_platform = "linux";
            options.m_runtimeChecks = false;
            options.m_sanitizers = {"address", "undefined"};
            flags = ab::NativeOptions::nativeCompileFlags(options, compiler, ab::Configuration::s_debug, true, false);
            check(
                has(flags, "-fsanitize=address") && has(flags, "-fsanitize=undefined") && has(flags, "-fstack-protector-strong"),
                "typed Clang sanitizer/security policies"
            );
            compiler.m_cCompiler = compiler.m_cppCompiler = compiler.m_linker = compiler.m_archiver = executable.string();
            compiler.m_extensionObject = ".o";
            compiler.m_extensionExecutable = "";
            compiler.m_extensionStatic = ".a";
            compiler.m_extensionShared = ".so";
            ab::Workspace shared;
            shared.m_name = "SharedPch";
            shared.m_root = root;
            ab::Target app;
            app.m_name = "App";
            app.m_sources = {"source.cpp"};
            app.m_compile.m_pchHeader = "Pch.hpp";
            app.m_compile.m_sharedPch = true;
            shared.m_targets = {app};
            ab::Module part;
            part.m_name = "Part";
            part.m_targetName = "App";
            part.m_sources = {"module.cpp"};
            shared.m_modules = {part};
            auto plan = ab::BuildPlanner::planBuild(shared, compiler, root / "shared-pch", ab::Configuration::s_debug, executable);
            check(
                std::count_if(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [](const auto& a)
                    {
                        return a.m_name.starts_with("PCH ");
                    }
                ) == 1,
                "compatible modules share one PCH action"
            );
            shared.m_targets.front().m_compile.m_pchSource = "creator.cpp";
            shared.m_modules.front().m_compile.m_pchSource = "creator.cpp";
            plan = ab::BuildPlanner::planBuild(shared, compiler, root / "manual-shared", ab::Configuration::s_debug, executable);
            check(
                std::count_if(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [](const auto& a)
                    {
                        return a.m_name.starts_with("PCH ");
                    }
                ) == 1,
                "manual PCH creator reused safely"
            );
            ab::Target library;
            library.m_name = "Library";
            library.m_kind = ab::TargetKind::StaticLibrary;
            shared.m_targets = {library, app};
            shared.m_targets.back().m_compile = {};
            shared.m_targets.back().m_privateModules = {"Library/Core"};
            part.m_name = "Core";
            part.m_targetName = "Library";
            part.m_apiDefine = "CORE_API";
            part.m_compile.m_publicIncludes = {"."};
            shared.m_modules = {part};
            plan = ab::BuildPlanner::planBuild(shared, compiler, root / "qualified-modules", ab::Configuration::s_debug, executable);
            bool imported = false, linked = false;
            for (const auto& action : plan.m_actions)
                if (action.m_owner == "App")
                {
                    imported |= action.m_compile && has(action.m_argv, "-DCORE_API=");
                    linked |= action.m_name.starts_with("Link ") && has(action.m_argv, plan.m_targetOutputs.at("Library").front().string());
                }
            check(imported && linked, "qualified module reference imports interface and binary");
            shared.m_targets.front().m_kind = ab::TargetKind::SharedLibrary;
            shared.m_targets.front().m_linkType = "modular";
            shared.m_targets.back().m_privateModules.clear();
            shared.m_targets.back().m_privateDependencies = {"Library"};
            compiler.m_platform = "macos";
            compiler.m_extensionShared = ".dylib";
            shared.m_targets.front().m_link.m_frameworks = {"Foundation"};
            plan = ab::BuildPlanner::planBuild(shared, compiler, root / "frameworks", ab::Configuration::s_debug, executable);
            check(plan.m_targetKinds.contains("Library__Core"), "expanded binary modules retained");
            check(
                std::any_of(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [](const auto& a)
                    {
                        return has(a.m_argv, "-framework") && has(a.m_argv, "Foundation");
                    }
                ),
                "Apple framework reaches linker"
            );
            compiler.m_platform = "ios-simulator";
            plan = ab::BuildPlanner::planBuild(shared, compiler, root / "ios", ab::Configuration::s_debug, executable);
            check(
                std::any_of(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [](const auto& a)
                    {
                        return has(a.m_argv, "-dynamiclib");
                    }
                ),
                "iOS uses Apple dylib backend"
            );
        }

        {
            ab::Toolchain compiler;
            compiler.m_kind = ab::CompilerKind::MSVC;
            compiler.m_cCompiler = compiler.m_cppCompiler = compiler.m_linker = compiler.m_archiver = executable.string();
            compiler.m_documentationTool = executable.string();
            compiler.m_extensionObject = ".obj";
            compiler.m_extensionExecutable = ".exe";
            compiler.m_extensionStatic = ".lib";
            compiler.m_extensionShared = ".dll";
            ab::CompileOptions options;
            options.m_cppStandard = "c++14";
            check(has(ab::NativeOptions::nativeCompileFlags(options, compiler, ab::Configuration::s_release, true, false), "/std:c++14"), "C++14 exposed");
            options.m_cppStandard = "latest";
            check(
                has(ab::NativeOptions::nativeCompileFlags(options, compiler, ab::Configuration::s_release, true, false), "/std:c++latest"),
                "Latest independent mode exposed"
            );
            ab::FileSystem::writeIfChanged(root / "external.pch", "PCH");
            ab::FileSystem::writeIfChanged(root / "external.obj", "OBJECT");
            ab::Workspace external;
            external.m_root = root;
            external.m_name = "External";
            ab::Target app;
            app.m_name = "App";
            app.m_sources = {"source.cpp"};
            app.m_compile.m_pchHeader = "Pch.hpp";
            app.m_compile.m_pchFile = "external.pch";
            app.m_compile.m_pchObject = "external.obj";
            app.m_compile.m_documentation = true;
            external.m_targets = {app};
            auto plan = ab::BuildPlanner::planBuild(external, compiler, root / "external", ab::Configuration::s_release, executable);
            check(
                std::none_of(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [](const auto& a)
                    {
                        return a.m_name.starts_with("PCH ");
                    }
                ),
                "external PCH not regenerated"
            );
            check(
                std::any_of(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [&](const auto& a)
                    {
                        return a.m_compile && has(a.m_argv, "/Fp" + (root / "external.pch").string()) &&
                               std::find(a.m_inputs.begin(), a.m_inputs.end(), root / "external.pch") != a.m_inputs.end();
                    }
                ),
                "external PCH tracked and consumed"
            );
            check(
                std::any_of(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [](const auto& a)
                    {
                        return a.m_name == "Documentation App" && has(a.m_argv, "/old");
                    }
                ),
                "documentation merge planned"
            );
            ab::IDEContext context{external, compiler, plan, root / "external", ab::Configuration::s_release, "Release-x64"};
            ab::IDEGenerator::generateIDE(
                {context},
                root / "custom-ide",
                executable,
                "vs",
                {[](const auto&, const auto& targetName, const auto&, ab::IDECustomization& custom)
                 {
                     if (targetName == "App")
                     {
                         custom.m_properties["CallbackProperty"] = "callback<&>";
                         custom.m_debugger["LocalDebuggerCommandArguments"] = "--callback";
                     }
                 }}
            );
            check(
                ab::FileSystem::readAll(root / "custom-ide/vs/App.vcxproj").find("callback&lt;&amp;&gt;") != std::string::npos,
                "C++ customizer writes escaped project properties"
            );
            check(
                ab::FileSystem::readAll(root / "custom-ide/vs/App.vcxproj.user").find("--callback") != std::string::npos,
                "C++ customizer writes debugger settings"
            );
            compiler.m_kind = ab::CompilerKind::Clang;
            compiler.m_platform = "macos";
            compiler.m_extensionObject = ".o";
            app.m_compile = {};
            app.m_link.m_frameworkDirectories = {"Vendor"};
            external.m_targets = {app};
            ab::Target package;
            package.m_name = "Package";
            package.m_kind = ab::TargetKind::HeaderOnly;
            package.m_link.m_frameworkDirectories = {"SDK"};
            external.m_targets.push_back(package);
            external.m_targets.front().m_privateDependencies = {"Package"};
            plan = ab::BuildPlanner::planBuild(external, compiler, root / "framework-compile", ab::Configuration::s_release, executable);
            check(
                std::any_of(
                    plan.m_actions.begin(),
                    plan.m_actions.end(),
                    [&](const auto& a)
                    {
                        return a.m_compile && has(a.m_argv, "-F" + (root / "Vendor").string()) && has(a.m_argv, "-F" + (root / "SDK").string());
                    }
                ),
                "framework search directories propagate to compilation"
            );
        }
        ab::Toolchain tc;
        tc.m_kind = ab::CompilerKind::GCC;
        tc.m_cCompiler = tc.m_cppCompiler = tc.m_linker = tc.m_archiver = executable.string();
        ws.m_targets.front().m_outputDirectory = "../escape";
        throws(
            [&]
            {
                (void)ab::BuildPlanner::planBuild(ws, tc, root / "bad", ab::Configuration::s_debug, executable);
            },
            "stay under"
        );
        ws.m_targets.front() = target;
        ws.m_targets.front().m_link.m_incremental = true;
        throws(
            [&]
            {
                (void)ab::BuildPlanner::planBuild(ws, tc, root / "bad", ab::Configuration::s_debug, executable);
            },
            "LTO"
        );
        ws.m_targets.front() = target;
        ab::FileSystem::writeIfChanged(root / "one" / "same.dll", "one");
        ab::FileSystem::writeIfChanged(root / "two" / "same.dll", "two");
        ws.m_targets.front().m_runtimeFiles = {"one/same.dll", "two/same.dll"};
        throws(
            [&]
            {
                (void)ab::BuildPlanner::planBuild(ws, tc, root / "bad", ab::Configuration::s_debug, executable);
            },
            "collision"
        );
        const auto staged = root / "bad" / "products" / "Debug" / "Native" / "same.dll";
        ab::FileSystem::writeIfChanged(staged, "already staged");
        ws.m_targets.front().m_runtimeFiles = {staged.string(), "one/same.dll"};
        throws(
            [&]
            {
                (void)ab::BuildPlanner::planBuild(ws, tc, root / "bad", ab::Configuration::s_debug, executable);
            },
            "collision"
        );
    }
    catch (const std::exception& error)
    {
        check(false, error.what());
    }
    fs::remove_all(root);
    std::cout << (s_failures ? "FAILED" : "PASS") << ": native plans for MSVC/GCC/Clang (planning only)\n";
    return s_failures ? 1 : 0;
}
