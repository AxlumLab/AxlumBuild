/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "backend/Ide.hpp"
#include "core/Process.hpp"
#include "managed/NativeProject.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        using Json = nlohmann::json;
        thread_local std::set<fs::path>* s_generatedFiles = nullptr;
        thread_local const std::vector<IDEProjectCustomizer>* s_activeCustomizers = nullptr;

        IDECustomization customize(const IDEContext& c, const std::string& name, const std::string& format)
        {
            IDECustomization custom;
            if (c.m_workspace.m_ide.contains(format))
                BuildModel::mergeIDECustomization(custom, c.m_workspace.m_ide.at(format));
            const Target* selected = nullptr;
            for (const auto& t : c.m_workspace.m_targets)
                if (t.m_name == name || name.starts_with(t.m_name + "__"))
                    if (!selected || selected->m_name.size() < t.m_name.size())
                        selected = &t;
            if (selected && selected->m_ide.contains(format))
                BuildModel::mergeIDECustomization(custom, selected->m_ide.at(format));
            if (s_activeCustomizers)
                for (const auto& callback : *s_activeCustomizers)
                    callback(c, name, format, custom);
            for (const auto* properties : {&custom.m_properties, &custom.m_debugger})
                for (const auto& [key, value] : *properties)
                {
                    (void)value;
                    if (!std::regex_match(key, std::regex("[A-Za-z_][A-Za-z0-9_.-]*")))
                        throw std::runtime_error("invalid IDE property name: " + key);
                }
            return custom;
        }

        void patch(Json& value, const IDECustomization& custom, const char* section)
        {
            if (custom.m_jsonPatch.empty())
                return;
            const auto patch = Json::parse(custom.m_jsonPatch);
            if (patch.contains(section))
                value.merge_patch(patch.at(section));
        }

        void writeGenerated(const fs::path& file, const std::string& data)
        {
            FileSystem::writeIfChanged(file, data);
            if (s_generatedFiles)
                s_generatedFiles->insert(fs::absolute(file).lexically_normal());
        }

        std::string fileKey(const fs::path& file)
        {
            auto key = fs::absolute(file).lexically_normal().generic_string();
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
        }

        std::string escapeXml(std::string s)
        {
            std::string out;
            for (const char c : s)
            {
                if (c == '&')
                    out += "&amp;";
                else if (c == '<')
                    out += "&lt;";
                else if (c == '>')
                    out += "&gt;";
                else if (c == '"')
                    out += "&quot;";
                else if (c == '\'')
                    out += "&apos;";
                else
                    out += c;
            }
            return out;
        }

        std::string q(const std::string& s)
        {
            return Json(s).dump();
        }

        std::string shellCommand(const std::vector<std::string>& args)
        {
            std::string out;
            for (const auto& arg : args)
            {
                if (!out.empty())
                    out += ' ';
                out += Process::quoteShellArg(arg);
            }
            return out;
        }

        std::string makeId(const std::string& value)
        {
            auto s = FileSystem::shortHash(value) + FileSystem::shortHash("axlum:" + value);
            std::transform(
                s.begin(),
                s.end(),
                s.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::toupper(c));
                }
            );
            return s;
        }

        std::string makeGuid(const std::string& value)
        {
            const auto s = makeId(value) + "00000000";
            return "{" + s.substr(0, 8) + "-" + s.substr(8, 4) + "-" + s.substr(12, 4) + "-" + s.substr(16, 4) + "-" + s.substr(20, 12) + "}";
        }

        std::vector<std::string> collectTargets(const std::vector<IDEContext>& contexts)
        {
            std::set<std::string> names;
            for (const auto& c : contexts)
                for (const auto& [name, kind] : c.m_graph.m_targetKinds)
                {
                    (void)kind;
                    if (!name.starts_with("_rhodium_"))
                        names.insert(name);
                }
            return {names.begin(), names.end()};
        }

        std::set<std::string> collectSources(const IDEContext& c, const std::string& name)
        {
            std::set<std::string> files;
            for (const auto& t : c.m_workspace.m_targets)
                if (t.m_name == name)
                {
                    for (const auto& f : t.m_ideFiles)
                        files.insert(f.starts_with("@build/") ? (c.m_buildDirectory / f.substr(7)).generic_string() : f);
                    for (const auto& f : t.m_sources)
                        if (!f.starts_with("@build/"))
                            files.insert(f);
                }
            for (const auto& m : c.m_workspace.m_modules)
                if (m.m_targetName == name || m.m_targetName + "__" + (m.m_binaryModuleName.empty() ? m.m_name : m.m_binaryModuleName) == name)
                {
                    for (const auto& f : m.m_sources)
                        if (!f.starts_with("@build/"))
                            files.insert(f);
                    for (const auto& f : m.m_ideFiles)
                        files.insert(f.starts_with("@build/") ? (c.m_buildDirectory / f.substr(7)).generic_string() : f);
                }
            for (const auto& a : c.m_graph.m_actions)
                if (a.m_owner == name && a.m_compile)
                    files.insert(a.m_source.generic_string());
            return files;
        }

        struct Interface
        {
            std::set<std::string> m_includes, m_defines;
        };

        Interface compileInterface(const IDEContext& c, const std::string& name)
        {
            Interface result;
            for (const auto& a : c.m_graph.m_actions)
                if (a.m_owner == name && a.m_compile)
                    for (const auto& arg : a.m_argv)
                    {
                        if (arg.starts_with("/I") || arg.starts_with("-I"))
                            result.m_includes.insert(arg.substr(2));
                        else if (arg.starts_with("/external:I"))
                            result.m_includes.insert(arg.substr(11));
                        else if (arg.starts_with("/D") || arg.starts_with("-D"))
                            result.m_defines.insert(arg.substr(2));
                    }
            return result;
        }

        std::string join(const std::set<std::string>& values)
        {
            std::string out;
            for (const auto& s : values)
            {
                if (!out.empty())
                    out += ';';
                out += s;
            }
            return out;
        }

        std::vector<std::string> buildCommand(const IDEContext& c, const fs::path& self, const std::string& target, bool clean = false)
        {
            return {self.string(), clean ? "clean" : "build", "-B", c.m_buildDirectory.string(), "-t", target};
        }

        void vsCode(const std::vector<IDEContext>& contexts, const fs::path& dir, const fs::path& self)
        {
            Json tasks = Json::array(), launches = Json::array(), configs = Json::array();
            for (const auto& c : contexts)
            {
                for (const auto& name : collectTargets({c}))
                {
                    const auto label = c.m_label + ":" + name;
                    const auto custom = customize(c, name, "vscode");
                    for (const bool clean : {false, true})
                    {
                        auto args = buildCommand(c, self, name, clean);
                        args.erase(args.begin());
                        tasks.push_back(
                            {{"label", (clean ? "clean:" : "build:") + label},
                             {"type", "process"},
                             {"command", self.string()},
                             {"args", args},
                             {"options", {{"cwd", c.m_workspace.m_root.string()}}},
                             {"problemMatcher", c.m_toolchain.m_kind == CompilerKind::MSVC ? "$msCompile" : "$gcc"},
                             {"group", clean ? "none" : "build"}}
                        );
                        if (clean)
                            tasks.back().erase("group");
                        patch(tasks.back(), custom, "task");
                    }
                    const auto managed = std::find_if(
                        c.m_workspace.m_targets.begin(),
                        c.m_workspace.m_targets.end(),
                        [&](const auto& t)
                        {
                            return t.m_name == name && t.m_kind == TargetKind::DotNetProject;
                        }
                    );
                    if (managed != c.m_workspace.m_targets.end())
                    {
                        const auto project = fs::path(managed->m_project);
                        const auto text = FileSystem::readAll(project);
                        const bool runnable =
                            managed->m_managedExecutable || std::regex_search(text, std::regex(R"(<OutputType>\s*(Exe|WinExe)\s*</OutputType>)"));
                        if (runnable)
                        {
                            Json launch = {
                                {"name", label}, {"request", "launch"}, {"preLaunchTask", "build:" + label}, {"cwd", managed->m_sourceRoot.generic_string()}
                            };
                            if (managed->m_generatedProject)
                            {
                                auto config = BuildModel::toString(c.m_configuration);
                                std::transform(
                                    config.begin(),
                                    config.end(),
                                    config.begin(),
                                    [](unsigned char ch)
                                    {
                                        return static_cast<char>(std::tolower(ch));
                                    }
                                );
                                const auto assembly = managed->m_managed.m_properties.contains("AssemblyName")
                                                          ? managed->m_managed.m_properties.at("AssemblyName")
                                                          : project.stem().string();
                                launch["type"] = "coreclr";
                                launch["program"] =
                                    (c.m_buildDirectory / "dotnet" / name / "artifacts/bin" / project.stem() / config / (assembly + ".dll")).generic_string();
                            }
                            else
                            {
                                launch["type"] = "dotnet";
                                launch["projectPath"] = project.generic_string();
                            }
                            for (const auto& [key, value] : custom.m_debugger)
                                launch[key] = value;
                            patch(launch, custom, "launch");
                            launches.push_back(launch);
                        }
                    }
                    const auto& outputs = c.m_graph.m_targetOutputs.at(name);
                    if (c.m_graph.m_targetKinds.at(name) == TargetKind::Executable && !outputs.empty() && c.m_toolchain.m_target == c.m_toolchain.m_host)
                    {
                        Json launch = {
                            {"name", label},
                            {"type", c.m_toolchain.m_kind == CompilerKind::MSVC ? "cppvsdbg" : "cppdbg"},
                            {"request", "launch"},
                            {"program", outputs.front().string()},
                            {"cwd", outputs.front().parent_path().string()},
                            {"args", Json::array()},
                            {"preLaunchTask", "build:" + label},
                            {"stopAtEntry", false}
                        };
                        if (c.m_toolchain.m_kind != CompilerKind::MSVC)
                            launch["MIMode"] = c.m_toolchain.m_platform == "macos" ? "lldb" : "gdb";
                        for (const auto& [key, value] : custom.m_debugger)
                            launch[key] = value;
                        patch(launch, custom, "launch");
                        launches.push_back(launch);
                    }
                }
                configs.push_back(
                    {{"name", c.m_label},
                     {"compilerPath", c.m_toolchain.m_cppCompiler},
                     {"compileCommands", (c.m_buildDirectory / "compile_commands.json").string()},
                     {"cppStandard", "c++20"},
                     {"cStandard", "c11"}}
                );
                const auto custom = customize(c, "", "vscode");
                for (const auto& [key, value] : custom.m_properties)
                    configs.back()[key] = value;
                patch(configs.back(), custom, "configuration");
            }
            const Json taskFile = {{"version", "2.0.0"}, {"tasks", tasks}};
            const Json launchFile = {{"version", "0.2.0"}, {"configurations", launches}};
            writeGenerated(dir / ".vscode/tasks.json", taskFile.dump(2) + "\n");
            writeGenerated(dir / ".vscode/launch.json", launchFile.dump(2) + "\n");
            writeGenerated(dir / ".vscode/c_cpp_properties.json", Json{{"version", 4}, {"configurations", configs}}.dump(2) + "\n");
            Json workspace = {
                {"folders", Json::array({{{"name", "Sources"}, {"path", contexts.front().m_workspace.m_root.generic_string()}}})},
                {"tasks", taskFile},
                {"launch", launchFile},
                {"settings", {{"C_Cpp.default.compileCommands", (contexts.front().m_buildDirectory / "compile_commands.json").string()}}}
            };
            std::set<std::string> managedFolders;
            for (const auto& c : contexts)
                for (const auto& t : c.m_workspace.m_targets)
                    if (t.m_kind == TargetKind::DotNetProject)
                    {
                        const auto folder = fs::path(t.m_project).parent_path().generic_string();
                        if (managedFolders.insert(folder).second)
                            workspace["folders"].push_back({{"name", t.m_name + " [" + c.m_label + "]"}, {"path", folder}});
                    }
            patch(workspace, customize(contexts.front(), "", "vscode"), "workspace");
            writeGenerated(dir / (contexts.front().m_workspace.m_name + ".code-workspace"), workspace.dump(2) + "\n");
        }

        void visualStudio(const std::vector<IDEContext>& contexts, const fs::path& dir, const fs::path& self)
        {
            auto names = collectTargets(contexts);
            std::erase_if(
                names,
                [&](const auto& name)
                {
                    for (const auto& c : contexts)
                        if (c.m_graph.m_targetKinds.contains(name) && c.m_graph.m_targetKinds.at(name) == TargetKind::DotNetProject)
                            return true;
                    return false;
                }
            );
            names.insert(names.begin(), "all");
            std::map<std::string, const Target*> external;
            std::map<std::string, std::string> nativeGuids, externalPaths;
            std::map<std::string, std::string> pathOwners;
            for (const auto& c : contexts)
                for (const auto& t : c.m_workspace.m_targets)
                    if (BuildModel::isNativeProject(t))
                    {
                        const auto key = fileKey(t.m_project);
                        if (pathOwners.contains(key) && pathOwners.at(key) != t.m_name)
                            throw std::runtime_error("declare an external native project once and use configuration mappings: " + t.m_project);
                        pathOwners[key] = t.m_name;
                        if (externalPaths.contains(t.m_name) && fileKey(externalPaths.at(t.m_name)) != key)
                            throw std::runtime_error("external project path must be stable across IDE configurations: " + t.m_name);
                        external[t.m_name] = &t;
                        externalPaths[t.m_name] = t.m_project;
                        const auto guid = NativeProjectBuilder::nativeProjectGuid(t);
                        nativeGuids[t.m_name] = guid.empty() ? makeGuid(t.m_name) : guid;
                    }
            auto projectGuid = [&](const std::string& name)
            {
                return nativeGuids.contains(name) ? nativeGuids.at(name) : makeGuid(name);
            };
            std::ostringstream sln;
            sln << "Microsoft Visual Studio Solution File, Format Version 12.00\n# Visual Studio Version 17\n";
            for (const auto& c : contexts)
                for (const auto& target : c.m_workspace.m_targets)
                    if (target.m_kind == TargetKind::DotNetProject)
                    {
                        const auto key = target.m_name + "@" + c.m_label;
                        sln << "Project(\"{9A19103F-16F7-4668-BE54-9A1E7A4F7556}\") = \"" << target.m_name << " [" << c.m_label << "]\", \""
                            << fs::path(target.m_project).generic_string() << "\", \"" << makeGuid(key) << "\"\nEndProject\n";
                    }
            for (const auto& name : names)
            {
                if (external.contains(name))
                {
                    sln << "Project(\"{BC8A1FFA-BEE3-4634-8014-F334798102B3}\") = \"" << name << "\", \"" << externalPaths.at(name) << "\", \""
                        << projectGuid(name) << "\"\nEndProject\n";
                    continue;
                }
                sln << "Project(\"{BC8A1FFA-BEE3-4634-8014-F334798102B3}\") = \"" << name << "\", \"" << name << ".vcxproj\", \"" << makeGuid(name)
                    << "\"\nEndProject\n";
                std::ostringstream project;
                const auto customization = customize(contexts.front(), name, "vs");
                project << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Project DefaultTargets=\"Build\" "
                           "ToolsVersion=\"Current\" "
                           "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n<ItemGroup "
                           "Label=\"ProjectConfigurations\">\n";
                for (const auto& c : contexts)
                    if (name == "all" || c.m_graph.m_targetKinds.contains(name))
                        project << "<ProjectConfiguration Include=\"" << escapeXml(c.m_label) << '|'
                                << (c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64") << "\"><Configuration>" << escapeXml(c.m_label)
                                << "</Configuration><Platform>" << (c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64")
                                << "</Platform></ProjectConfiguration>\n";
                project << "</ItemGroup><PropertyGroup Label=\"Globals\"><ProjectGuid>" << makeGuid(name)
                        << "</ProjectGuid><Keyword>MakeFileProj</Keyword></PropertyGroup>\n<Import "
                           "Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\"/>\n";
                project << customization.m_projectBegin << "\n";
                for (const auto& c : contexts)
                    if (name == "all" || c.m_graph.m_targetKinds.contains(name))
                        project << "<PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << escapeXml(c.m_label) << '|'
                                << (c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64")
                                << "'\" "
                                   "Label=\"Configuration\"><ConfigurationType>Makefile</"
                                   "ConfigurationType><PlatformToolset>v143</PlatformToolset></PropertyGroup>\n";
                project << "<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\"/>\n";
                std::set<std::string> files;
                for (const auto& c : contexts)
                {
                    if (name != "all" && !c.m_graph.m_targetKinds.contains(name))
                        continue;
                    const auto iface = compileInterface(c, name);
                    const auto build = shellCommand(buildCommand(c, self, name));
                    const auto clean = shellCommand(buildCommand(c, self, name, true));
                    auto rebuildArgs = buildCommand(c, self, name);
                    rebuildArgs[1] = "rebuild";
                    const auto rebuild = shellCommand(rebuildArgs);
                    const auto outputs = name == "all" ? std::vector<fs::path>{} : c.m_graph.m_targetOutputs.at(name);
                    std::string languageStandard = "stdcpp20";
                    for (const auto& t : c.m_workspace.m_targets)
                        if (t.m_name == name || name.starts_with(t.m_name + "__"))
                        {
                            const auto dialect = t.m_compile.m_cppStandard;
                            languageStandard = dialect == "latest" || dialect == "c++23" ? "stdcpplatest"
                                               : dialect == "c++14"                      ? "stdcpp14"
                                               : dialect == "c++17"                      ? "stdcpp17"
                                                                                         : "stdcpp20";
                        }
                    project << "<PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << escapeXml(c.m_label) << '|'
                            << (c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64") << "'\">\n"
                            << "<NMakeBuildCommandLine>" << escapeXml(build) << "</NMakeBuildCommandLine>\n<NMakeCleanCommandLine>" << escapeXml(clean)
                            << "</NMakeCleanCommandLine>\n<NMakeReBuildCommandLine>" << escapeXml(rebuild) << "</NMakeReBuildCommandLine>\n"
                            << "<NMakeOutput>" << (outputs.empty() ? "" : escapeXml(outputs.front().string())) << "</NMakeOutput>\n"
                            << "<OutDir>" << escapeXml(c.m_buildDirectory.string()) << "\\</OutDir><IntDir>"
                            << escapeXml((dir / "obj" / name / c.m_label).string()) << "\\</IntDir>\n"
                            << "<NMakeIncludeSearchPath>" << escapeXml(join(iface.m_includes))
                            << ";$(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>\n<NMakePreprocessorDefinitions>" << escapeXml(join(iface.m_defines))
                            << ";$(NMakePreprocessorDefinitions)</NMakePreprocessorDefinitions>\n<NMakeLanguageStandard>" << languageStandard
                            << "</NMakeLanguageStandard>\n";
                    if (!outputs.empty() && c.m_graph.m_targetKinds.at(name) == TargetKind::Executable && c.m_toolchain.m_target == c.m_toolchain.m_host)
                        project << "<LocalDebuggerCommand>" << escapeXml(outputs.front().string()) << "</LocalDebuggerCommand><LocalDebuggerWorkingDirectory>"
                                << escapeXml(outputs.front().parent_path().string())
                                << "</LocalDebuggerWorkingDirectory><DebuggerFlavor>WindowsLocalDebugger</"
                                   "DebuggerFlavor>\n";
                    for (const auto& [key, value] : customize(c, name, "vs").m_properties)
                        project << '<' << key << '>' << escapeXml(value) << "</" << key << ">\n";
                    project << "</PropertyGroup>\n";
                    const auto source = collectSources(c, name);
                    files.insert(source.begin(), source.end());
                }
                project << "<ItemGroup>\n";
                for (const auto& file : files)
                {
                    const auto ext = fs::path(file).extension().string();
                    const auto tag = (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") ? "ClCompile"
                                     : (ext == ".h" || ext == ".hpp" || ext == ".inl")               ? "ClInclude"
                                                                                                     : "None";
                    project << '<' << tag << " Include=\"" << escapeXml(file) << "\"/>\n";
                }
                project << "</ItemGroup><Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>\n" << customization.m_projectEnd << "\n</Project>\n";
                writeGenerated(dir / (name + ".vcxproj"), project.str());
                const auto xmlRoot = "<?xml version=\"1.0\" encoding=\"utf-8\"?><Project ToolsVersion=\"4.0\" "
                                     "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
                if (!customization.m_filtersBegin.empty() || !customization.m_filtersEnd.empty())
                    writeGenerated(
                        dir / (name + ".vcxproj.filters"), xmlRoot + customization.m_filtersBegin + "\n" + customization.m_filtersEnd + "\n</Project>\n"
                    );
                std::string user = xmlRoot + customization.m_userBegin + "\n";
                bool writeUser = !customization.m_userBegin.empty() || !customization.m_userEnd.empty();
                for (const auto& c : contexts)
                {
                    user += "<PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" + escapeXml(c.m_label) + "|" +
                            (c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64") + "'\">\n";
                    const auto debug = customize(c, name, "vs").m_debugger;
                    writeUser |= !debug.empty();
                    for (const auto& [key, value] : debug)
                        user += '<' + key + '>' + escapeXml(value) + "</" + key + ">\n";
                    user += "</PropertyGroup>\n";
                }
                if (writeUser)
                    writeGenerated(dir / (name + ".vcxproj.user"), user + customization.m_userEnd + "\n</Project>\n");
            }
            sln << "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
            for (const auto& c : contexts)
            {
                const auto p = c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64";
                sln << "\t\t" << c.m_label << '|' << p << " = " << c.m_label << '|' << p << '\n';
            }
            sln << "\tEndGlobalSection\n\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
            for (const auto& name : names)
                for (const auto& c : contexts)
                    if (name == "all" || c.m_graph.m_targetKinds.contains(name))
                    {
                        const auto p = c.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64";
                        const auto item = std::find_if(
                            c.m_workspace.m_targets.begin(),
                            c.m_workspace.m_targets.end(),
                            [&](const auto& t)
                            {
                                return t.m_name == name;
                            }
                        );
                        const auto configuration =
                            external.contains(name) ? NativeProjectBuilder::nativeProjectConfiguration(*item, c.m_configuration) : c.m_label;
                        const auto architecture =
                            external.contains(name) ? NativeProjectBuilder::nativeProjectArchitecture(*item, c.m_toolchain.m_architecture) : p;
                        sln << "\t\t" << projectGuid(name) << '.' << c.m_label << '|' << p << ".ActiveCfg = " << configuration << '|' << architecture << '\n';
                        // One aggregate build avoids concurrent Ninja processes for the same build directory.
                        if (name == "all")
                            sln << "\t\t" << makeGuid(name) << '.' << c.m_label << '|' << p << ".Build.0 = " << c.m_label << '|' << p << '\n';
                    }
            for (const auto& owner : contexts)
                for (const auto& target : owner.m_workspace.m_targets)
                    if (target.m_kind == TargetKind::DotNetProject)
                    {
                        for (const auto& entry : contexts)
                        {
                            const auto platform = entry.m_toolchain.m_architecture == "arm64" ? "ARM64" : "x64";
                            sln << "\t\t" << makeGuid(target.m_name + "@" + owner.m_label) << '.' << entry.m_label << '|' << platform
                                << ".ActiveCfg = " << BuildModel::toString(owner.m_configuration) << "|Any CPU\n";
                        }
                    }
            sln << "\tEndGlobalSection\nEndGlobal\n";
            writeGenerated(dir / (contexts.front().m_workspace.m_name + ".sln"), sln.str());
        }

        void driver(const std::vector<IDEContext>& contexts, const fs::path& dir, const fs::path& self)
        {
            Json settings = {{"executable", self.string()}, {"manifest", contexts.front().m_workspace.m_manifest.string()}, {"entries", Json::object()}};
            for (const auto& c : contexts)
                settings["entries"][c.m_label] = c.m_buildDirectory.string();
            writeGenerated(dir / "axlum-ide.json", settings.dump(2) + "\n");
            writeGenerated(dir / "axlum_driver.py", R"py(import argparse, json, os, pathlib, shutil, subprocess
p = argparse.ArgumentParser()
p.add_argument('--config', required=True)
p.add_argument('--target', default='all')
p.add_argument('--action', default='build')
p.add_argument('--android', action='store_true')
p.add_argument('--abi', default='arm64-v8a')
p.add_argument('--ndk', default='')
p.add_argument('--api', default='21')
p.add_argument('--directory', default='')
p.add_argument('--stage', default='')
a = p.parse_args()
s = json.loads(pathlib.Path(__file__).with_name('axlum-ide.json').read_text())
cli = s['executable']
if not a.android:
    if a.config not in s['entries']: p.error('unknown IDE configuration: ' + a.config)
    subprocess.run([cli, 'clean' if a.action == 'clean' else 'build', '-B', s['entries'][a.config], '-t', a.target], check=True)
else:
    if a.abi not in ('arm64-v8a', 'x86_64'): p.error('supported Android ABIs: arm64-v8a, x86_64')
    env = os.environ.copy()
    env['ANDROID_NDK_HOME'] = a.ndk
    env['ANDROID_NDK_ROOT'] = a.ndk
    env.setdefault('ANDROID_SDK_ROOT', str(pathlib.Path(a.ndk).parent.parent))
    env['RHODIUM_ANDROID_API'] = a.api.removeprefix('android-')
    build = pathlib.Path(a.directory).resolve() / 'axlum'
    subprocess.run([cli, 'configure', '-f', s['manifest'], '-B', str(build), '--config', a.config, '--platform', 'android', '--arch', 'arm64' if a.abi == 'arm64-v8a' else 'x64'], env=env, check=True)
    artifacts = json.loads((build / 'axlum.artifacts.json').read_text())
    targets = [k for k, v in artifacts.items() if v['kind'] == 'shared_library']
    if not targets: p.error('Android integration requires a SharedLibrary target')
    staged = pathlib.Path(a.stage).resolve() if a.stage else pathlib.Path(a.directory).resolve() / 'jniLibs' / a.abi
    staged.mkdir(parents=True, exist_ok=True)
    expected = set()
    for target in targets:
        subprocess.run([cli, 'build', '-B', str(build), '-t', target], env=env, check=True)
        for value in artifacts[target]['outputs']:
            source = pathlib.Path(value)
            if source.suffix == '.so':
                dest = staged / source.name
                expected.add(dest.m_name)
                if not dest.exists() or source.read_bytes() != dest.read_bytes(): shutil.copy2(source, dest)
    for old in staged.glob('*.so'):
        if old.name not in expected: old.unlink()
)py");
        }

        void xcode(const std::vector<IDEContext>& contexts, const fs::path& dir)
        {
            const auto& first = contexts.front();
            const auto project = first.m_workspace.m_name;
            const auto names = collectTargets(contexts);
            std::ostringstream pbx;
            auto ref = [&](const std::string& value)
            {
                return makeId(value);
            };
            pbx << "// !$*UTF8*$!\n{ archiveVersion = 1; classes = {}; objectVersion = 56; objects = {\n";
            std::set<std::string> files;
            for (const auto& c : contexts)
                for (const auto& name : names)
                {
                    const auto sources = collectSources(c, name);
                    files.insert(sources.begin(), sources.end());
                }
            for (const auto& file : files)
                pbx << ref(file) << " = {isa = PBXFileReference; path = " << q(file) << "; sourceTree = \"<absolute>\"; };\n";
            pbx << ref("group") << " = {isa = PBXGroup; sourceTree = \"<group>\"; children = (";
            for (const auto& file : files)
                pbx << ref(file) << ',';
            pbx << "); };\n";
            auto configurations = [&](const std::string& owner)
            {
                for (const auto& c : contexts)
                {
                    pbx << ref(owner + c.m_label) << " = {isa = XCBuildConfiguration; name = " << q(c.m_label)
                        << "; buildSettings = { AXLUM_BUILD_DIR = " << q(c.m_buildDirectory.string());
                    if (c.m_toolchain.m_platform == "macos" || c.m_toolchain.m_platform.starts_with("ios"))
                    {
                        pbx << "; SDKROOT = "
                            << (c.m_toolchain.m_platform == "macos"           ? "macosx"
                                : c.m_toolchain.m_platform == "ios-simulator" ? "iphonesimulator"
                                                                              : "iphoneos")
                            << "; ARCHS = " << (c.m_toolchain.m_architecture == "arm64" ? "arm64" : "x86_64");
                    }
                    for (const auto& [key, value] : customize(c, owner == "project" ? "" : owner, "xcode").m_properties)
                        pbx << "; " << key << " = " << q(value);
                    pbx << "; }; };\n";
                }
                pbx << ref(owner + "list") << " = {isa = XCConfigurationList; buildConfigurations = (";
                for (const auto& c : contexts)
                    pbx << ref(owner + c.m_label) << ',';
                pbx << "); defaultConfigurationIsVisible = 0; defaultConfigurationName = " << q(first.m_label) << "; };\n";
            };
            configurations("project");
            for (const auto& name : names)
            {
                const auto custom = customize(first, name, "xcode");
                pbx << custom.m_projectBegin << "\n";
                configurations(name);
                pbx << ref(name) << " = {isa = PBXLegacyTarget; name = " << q(name) << "; productName = " << q(name)
                    << "; buildConfigurationList = " << ref(name + "list")
                    << "; buildPhases = (); dependencies = (); passBuildSettingsInEnvironment = 1; buildToolPath = "
                       "/usr/bin/env; buildWorkingDirectory = "
                    << q(first.m_workspace.m_root.string()) << "; buildArgumentsString = "
                    << q("python3 \"" + (dir / "axlum_driver.py").generic_string() + "\" --config \"$(CONFIGURATION)\" --target " + name +
                         " --action \"$(ACTION)\"")
                    << "; };\n";
                pbx << custom.m_projectEnd << "\n";
                for (const auto& c : contexts)
                    if (c.m_graph.m_targetKinds.contains(name))
                    {
                        const auto& outputs = c.m_graph.m_targetOutputs.at(name);
                        const auto reference = "<BuildableReference BuildableIdentifier=\"primary\" BlueprintIdentifier=\"" + ref(name) +
                                               "\" BuildableName=\"" + escapeXml(name) + "\" BlueprintName=\"" + escapeXml(name) +
                                               "\" ReferencedContainer=\"container:" + escapeXml(project) + ".xcodeproj\"/>";
                        std::string scheme = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Scheme LastUpgradeVersion=\"1500\" "
                                             "version=\"1.3\"><BuildAction parallelizeBuildables=\"NO\" "
                                             "buildImplicitDependencies=\"YES\"><BuildActionEntries><BuildActionEntry "
                                             "buildForRunning=\"YES\" buildForProfiling=\"YES\" buildForArchiving=\"YES\" "
                                             "buildForAnalyzing=\"YES\">" +
                                             reference + "</BuildActionEntry></BuildActionEntries></BuildAction>";
                        if (c.m_graph.m_targetKinds.at(name) == TargetKind::Executable && !outputs.empty() && c.m_toolchain.m_target == c.m_toolchain.m_host)
                        {
                            std::map<std::string, std::string> attributes = {
                                {"buildConfiguration", c.m_label},
                                {"selectedDebuggerIdentifier", "Xcode.DebuggerFoundation.Debugger.LLDB"},
                                {"selectedLauncherIdentifier", "Xcode.IDEFoundation.Launcher.LLDB"},
                                {"useCustomWorkingDirectory", "YES"},
                                {"customWorkingDirectory", outputs.front().parent_path().string()}
                            };
                            for (const auto& [key, value] : customize(c, name, "xcode").m_debugger)
                                attributes[key] = value;
                            scheme += "<LaunchAction";
                            for (const auto& [key, value] : attributes)
                                scheme += ' ' + key + "=\"" + escapeXml(value) + "\"";
                            scheme += "><PathRunnable runnableDebuggingMode=\"0\" FilePath=\"" + escapeXml(outputs.front().string()) + "\"/></LaunchAction>";
                        }
                        scheme += "<AnalyzeAction buildConfiguration=\"" + escapeXml(c.m_label) + "\"/><ArchiveAction buildConfiguration=\"" +
                                  escapeXml(c.m_label) + "\" revealArchiveInOrganizer=\"YES\"/></Scheme>\n";
                        writeGenerated(dir / (project + ".xcodeproj") / "xcshareddata/xcschemes" / (name + "-" + c.m_label + ".xcscheme"), scheme);
                    }
            }
            pbx << ref("project") << " = {isa = PBXProject; attributes = {LastUpgradeCheck = 1500; }; buildConfigurationList = " << ref("projectlist")
                << "; compatibilityVersion = \"Xcode 14.0\"; developmentRegion = en; hasScannedForEncodings = 0; "
                   "knownRegions = (en,Base); mainGroup = "
                << ref("group") << "; projectDirPath = \"\"; projectRoot = \"\"; targets = (";
            for (const auto& name : names)
                pbx << ref(name) << ',';
            pbx << "); }; }; rootObject = " << ref("project") << "; }\n";
            writeGenerated(dir / (project + ".xcodeproj") / "project.pbxproj", pbx.str());
            writeGenerated(
                dir / (project + ".xcworkspace") / "contents.xcworkspacedata",
                "<?xml version=\"1.0\"?><Workspace version=\"1.0\"><FileRef location=\"group:" + escapeXml(project) + ".xcodeproj\"/></Workspace>\n"
            );
        }

        void androidStudio(const std::vector<IDEContext>& contexts, const fs::path& dir, const fs::path& self)
        {
            std::ostringstream gradle;
            gradle << "plugins { id 'base' }\n";
            const auto custom = customize(contexts.front(), "", "android-studio");
            gradle << custom.m_projectBegin << "\n";
            for (const auto& [key, value] : custom.m_properties)
                gradle << "ext.set(" << q(key) << ", " << q(value) << ")\n";
            for (const auto& c : contexts)
                for (const auto& target : collectTargets({c}))
                {
                    const auto name = "axlum_" + c.m_label + "_" + target;
                    auto args = buildCommand(c, self, target);
                    gradle << "tasks.register(" << q(name) << ", Exec) { group = 'axlum'; commandLine ";
                    for (std::size_t i = 0; i < args.size(); ++i)
                        gradle << (i ? ", " : "") << q(args[i]);
                    gradle << " }\n";
                    writeGenerated(
                        dir / ".idea/runConfigurations" / (name + ".xml"),
                        "<component name=\"ProjectRunConfigurationManager\"><configuration default=\"false\" name=\"" + escapeXml(name) +
                            "\" type=\"GradleRunConfiguration\" factoryName=\"Gradle\"><ExternalSystemSettings><option "
                            "name=\"externalProjectPath\" value=\"$PROJECT_DIR$\"/><option "
                            "name=\"externalSystemIdString\" value=\"GRADLE\"/><option "
                            "name=\"taskNames\"><list><option value=\"" +
                            escapeXml(name) +
                            "\"/></list></option></ExternalSystemSettings><method "
                            "v=\"2\"/></configuration></component>\n"
                    );
                }
            gradle << "tasks.register('axlumBuild', Exec) { group = 'axlum'; commandLine " << q(self.string()) << ", 'build', '-B', "
                   << q(contexts.front().m_buildDirectory.string()) << " }\ntasks.named('assemble') { dependsOn 'axlumBuild' }\n";
            gradle << custom.m_projectEnd << "\n";
            writeGenerated(dir / "build.gradle", gradle.str());
            writeGenerated(
                dir / "settings.gradle",
                "pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } "
                "}\ndependencyResolutionManagement { repositories { google(); mavenCentral() } }\nrootProject.m_name "
                "= " +
                    q(contexts.front().m_workspace.m_name) + "\nif (providers.gradleProperty('axlumAndroid').orNull == 'true') { include ':native' }\n"
            );
            std::string applications;
            for (const auto& target : contexts.front().m_workspace.m_targets)
                if (!target.m_android.m_applicationId.empty())
                {
                    const auto module = "app_" + target.m_name;
                    applications += "if (providers.gradleProperty('axlumAndroid').orNull == 'true') { include ':" + module + "' }\n";
                    const auto& options = target.m_android;
                    std::ostringstream app;
                    app << "plugins { id 'com.android.application' version '8.6.1' }\nandroid {\nnamespace " << q(options.m_applicationId) << "\ncompileSdk "
                        << options.m_targetSdk << "\ndefaultConfig { applicationId " << q(options.m_applicationId) << "; minSdk " << options.m_minSdk
                        << "; targetSdk " << options.m_targetSdk << "; versionCode 1; versionName '1.0' }\n"
                        << "buildTypes { debug { debuggable true; jniDebuggable true } }\n}\ndependencies { "
                           "implementation project(':native') }\n"
                        << "tasks.register('axlumRunDebug', Exec) { dependsOn 'installDebug'; doFirst { def args = "
                           "[androidComponents.sdkComponents.adb.get().asFile.absolutePath]; "
                        << "if (project.hasProperty('axlumDevice')) args += ['-s', project.property('axlumDevice')]; "
                           "args += ['shell', 'am', 'start', '-n', "
                        << q(options.m_applicationId + "/" + options.m_activity) << "]; commandLine args } }\n";
                    const auto appCustom = customize(contexts.front(), target.m_name, "android-studio");
                    auto appText = app.str();
                    const auto firstLine = appText.find('\n');
                    appText.insert(firstLine + 1, appCustom.m_projectBegin + "\n");
                    for (const auto& [key, value] : appCustom.m_properties)
                        appText += "\next.set(" + q(key) + ", " + q(value) + ")\n";
                    appText += "\n" + appCustom.m_projectEnd + "\n";
                    writeGenerated(dir / module / "build.gradle", appText);
                    std::string manifest;
                    if (!options.m_manifest.empty())
                        manifest = FileSystem::readAll(options.m_manifest);
                    else
                    {
                        const auto library = target.m_outputName.empty() ? target.m_name : target.m_outputName;
                        manifest = "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"><application "
                                   "android:label=\"" +
                                   escapeXml(target.m_name) + "\" android:hasCode=\"false\"><activity android:name=\"" + escapeXml(options.m_activity) +
                                   "\" android:exported=\"true\"><meta-data android:name=\"android.app.lib_name\" "
                                   "android:value=\"" +
                                   escapeXml(library) +
                                   "\"/><intent-filter><action android:name=\"android.intent.action.MAIN\"/><category "
                                   "android:name=\"android.intent.category.LAUNCHER\"/></intent-filter></activity></"
                                   "application></manifest>\n";
                    }
                    writeGenerated(dir / module / "src/main/AndroidManifest.xml", manifest);
                    auto runConfiguration = std::string(
                        "<component name=\"ProjectRunConfigurationManager\"><configuration default=\"false\" name=\"" + escapeXml(target.m_name) +
                        " Android\" type=\"AndroidRunConfigurationType\" factoryName=\"Android App\"><module name=\"" +
                        escapeXml(contexts.front().m_workspace.m_name + "." + module) +
                        "\"/><option name=\"DEPLOY\" value=\"true\"/><option name=\"MODE\" "
                        "value=\"default_activity\"/><method v=\"2\"><option name=\"Android.Gradle.BeforeRunTask\" "
                        "enabled=\"true\"/></method></configuration></component>\n"
                    );
                    std::string debuggerOptions;
                    for (const auto& [key, value] : appCustom.m_debugger)
                        debuggerOptions += "<option name=\"" + escapeXml(key) + "\" value=\"" + escapeXml(value) + "\"/>";
                    runConfiguration.insert(runConfiguration.find("<method"), debuggerOptions);
                    writeGenerated(dir / ".idea/runConfigurations" / (module + ".xml"), runConfiguration);
                }
            if (!applications.empty())
                writeGenerated(dir / "settings.gradle", FileSystem::readAll(dir / "settings.gradle") + applications);
            writeGenerated(dir / "native/build.gradle", R"gr(plugins { id 'com.android.library' version '8.6.1' }
android {
    namespace 'org.axlum.generated'
    compileSdk 35
    defaultConfig {
        minSdk 21
        ndk { abiFilters 'arm64-v8a', 'x86_64' }
        externalNativeBuild { cmake { targets 'axlum_android' } }
    }
    externalNativeBuild { cmake { path file('CMakeLists.txt'); version '3.22.1' } }
    sourceSets {
        debug { jniLibs.srcDirs = [file('.cxx/axlum-jni/Debug')] }
        release { jniLibs.srcDirs = [file('.cxx/axlum-jni/Release')] }
    }
}
tasks.configureEach { task ->
    if (task.name.startsWith('merge') && task.name.endsWith('JniLibFolders')) {
        def variant = task.name.substring(5, task.name.length() - 'JniLibFolders'.length())
        task.dependsOn tasks.matching { it.m_name == 'externalNativeBuild' + variant }
    }
}
)gr");
            writeGenerated(dir / "native/src/main/AndroidManifest.xml", "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"/>\n");
            std::ostringstream cmake;
            cmake << "cmake_minimum_required(VERSION 3.22)\nproject(AxlumAndroid LANGUAGES C "
                     "CXX)\nfind_package(Python3 REQUIRED COMPONENTS Interpreter)\n"
                  << "add_custom_target(axlum_android ALL COMMAND ${Python3_EXECUTABLE} "
                     "\"${CMAKE_CURRENT_LIST_DIR}/../axlum_driver.py\" --android --config \"${CMAKE_BUILD_TYPE}\" "
                     "--abi \"${ANDROID_ABI}\" --ndk \"${CMAKE_ANDROID_NDK}\" --api \"${ANDROID_PLATFORM}\" "
                     "--directory \"${CMAKE_BINARY_DIR}\" --stage "
                     "\"${CMAKE_CURRENT_LIST_DIR}/.cxx/axlum-jni/${CMAKE_BUILD_TYPE}/${ANDROID_ABI}\" VERBATIM)\n";
            std::size_t index = 0;
            for (const auto& target : collectTargets(contexts))
            {
                auto files = collectSources(contexts.front(), target);
                std::erase_if(
                    files,
                    [](const auto& f)
                    {
                        const auto e = fs::path(f).extension().string();
                        return e != ".cpp" && e != ".c" && e != ".cc" && e != ".cxx";
                    }
                );
                if (files.empty())
                    continue;
                const auto name = "axlum_index_" + std::to_string(index++);
                cmake << "add_library(" << name << " OBJECT EXCLUDE_FROM_ALL\n";
                for (const auto& f : files)
                    cmake << q(fs::path(f).generic_string()) << '\n';
                cmake << ")\ntarget_compile_features(" << name << " PRIVATE cxx_std_20)\n";
                const auto iface = compileInterface(contexts.front(), target);
                for (const auto& include : iface.m_includes)
                    if (FileSystem::isWithin(include, contexts.front().m_workspace.m_root))
                        cmake << "target_include_directories(" << name << " PRIVATE " << q(fs::path(include).generic_string()) << ")\n";
            }
            writeGenerated(dir / "native/CMakeLists.txt", cmake.str());
            writeGenerated(
                dir / "gradle.properties",
                "org.gradle.jvmargs=-Xmx2048m\nandroid.useAndroidX=true\n# Pass "
                "-PaxlumAndroid=true to enable the NDK library module.\n"
            );
            writeGenerated(
                dir / "README.md",
                "# Android Studio integration\n\nOpen this Gradle project. Host build tasks are in the axlum group and "
                ".idea/runConfigurations.\nUse installed Gradle or Android Studio's Gradle installation (no wrapper "
                "JAR is vendored).\nFor NDK library builds, enable -PaxlumAndroid=true; install JDK 17, Gradle 8.7, "
                "AGP 8.6.1, SDK 35, CMake 3.22.1 and an NDK. Set local.properties sdk.dir.\nThe native module "
                "delegates shared-library builds to AxlumBuild, stages JNI libraries and provides CMake source-index "
                "targets. It builds an AAR; targets declaring android_applicationid also generate an APK application "
                "module with installDebug/axlumRunDebug and Android Studio launch/debug configuration.\n"
            );
        }
    } // namespace

    void IDEGenerator::writeArtifactIndex(const BuildGraph& graph, const fs::path& directory)
    {
        Json model = Json::object();
        for (const auto& [name, outputs] : graph.m_targetOutputs)
        {
            std::vector<std::string> files;
            for (const auto& f : outputs)
                files.push_back(f.generic_string());
            model[name] = {{"kind", BuildModel::toString(graph.m_targetKinds.at(name))}, {"outputs", files}};
            std::set<std::string> intermediates;
            for (const auto& action : graph.m_actions)
                if (action.m_owner == name)
                {
                    for (const auto& file : action.m_outputs)
                        if (std::find(files.begin(), files.end(), file.generic_string()) == files.end())
                            intermediates.insert(file.generic_string());
                    if (!action.m_depfile.empty())
                        intermediates.insert(action.m_depfile.generic_string());
                    if (action.m_depsStyle == "msvc-json" && !action.m_outputs.empty())
                        intermediates.insert(action.m_outputs.front().generic_string() + ".json");
                }
            model[name]["intermediates"] = intermediates;
            if (graph.m_targetDependencies.contains(name))
                model[name]["dependencies"] = graph.m_targetDependencies.at(name);
            if (graph.m_nativeProjectPlans.contains(name))
                model[name]["nativeProjectPlan"] = graph.m_nativeProjectPlans.at(name);
        }
        writeGenerated(directory / "axlum.artifacts.json", model.dump(2) + "\n");
    }

    void IDEGenerator::generateIDE(
        const std::vector<IDEContext>& contexts,
        const fs::path& output,
        const fs::path& executable,
        const std::string& format,
        const std::vector<IDEProjectCustomizer>& customizers
    )
    {
        struct CustomizerRegistration
        {
            const std::vector<IDEProjectCustomizer>* m_previous;

            explicit CustomizerRegistration(const std::vector<IDEProjectCustomizer>& list) : m_previous(s_activeCustomizers)
            {
                s_activeCustomizers = &list;
            }

            ~CustomizerRegistration()
            {
                s_activeCustomizers = m_previous;
            }
        } customizerRegistration(customizers);

        std::set<fs::path> written;

        struct Registration
        {
            std::set<fs::path>* m_previous;

            explicit Registration(std::set<fs::path>& files) : m_previous(s_generatedFiles)
            {
                s_generatedFiles = &files;
            }

            ~Registration()
            {
                s_generatedFiles = m_previous;
            }
        } registration(written);

        if (contexts.empty())
            throw std::runtime_error("IDE generation has no supported target/configuration combinations");
        if (format == "vscode" || format == "all")
            vsCode(contexts, output / "vscode", executable);
        if (format == "vs" || format == "all")
            visualStudio(contexts, output / "vs", executable);
        if (format == "xcode" || format == "all")
        {
            driver(contexts, output / "xcode", executable);
            xcode(contexts, output / "xcode");
        }
        if (format == "android-studio" || format == "all")
        {
            driver(contexts, output / "android-studio", executable);
            androidStudio(contexts, output / "android-studio", executable);
        }
        const auto ledger = output / "axlum.ide-files.json";
        std::set<std::string> keys;
        std::vector<std::string> paths;
        for (const auto& file : written)
        {
            keys.insert(fileKey(file));
            paths.push_back(file.generic_string());
        }
        if (fs::is_regular_file(ledger))
            for (const auto& name : Json::parse(FileSystem::readAll(ledger)).get<std::vector<std::string>>())
            {
                const fs::path file(name);
                if (!FileSystem::isWithin(fs::weakly_canonical(file), fs::weakly_canonical(output)))
                    continue;
                const auto relative = file.lexically_relative(fs::absolute(output));
                if (format != "all" && !relative.empty() && relative.begin()->string() != format)
                {
                    if (fs::is_regular_file(file) && keys.insert(fileKey(file)).second)
                        paths.push_back(file.generic_string());
                }
                else if (!keys.contains(fileKey(file)) && fs::is_regular_file(file))
                    fs::remove(file);
            }
        FileSystem::writeIfChanged(ledger, Json(paths).dump(2) + "\n");
    }
} // namespace axlum
