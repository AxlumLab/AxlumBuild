/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "managed/NativeProject.hpp"
#include "core/Process.hpp"
#include "graph/NativeOptions.hpp"
#include "graph/NativePaths.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace axlum
{
    namespace
    {
        fs::path sdkPath(fs::path path)
        {
            path = fs::absolute(path).lexically_normal();
#ifdef _WIN32
            std::vector<fs::path> tail;
            auto existing = path;
            while (!fs::exists(existing) && existing != existing.parent_path())
            {
                tail.push_back(existing.filename());
                existing = existing.parent_path();
            }
            const auto length = GetLongPathNameW(existing.c_str(), nullptr, 0);
            if (length)
            {
                std::wstring full(length, L'\0');
                const auto written = GetLongPathNameW(existing.c_str(), full.data(), length);
                if (written)
                {
                    full.resize(written);
                    existing = full;
                    for (auto i = tail.rbegin(); i != tail.rend(); ++i)
                        existing /= *i;
                    path = existing;
                }
            }
#endif
            return path.make_preferred();
        }

        std::string escapeXml(const std::string& value)
        {
            std::string out;
            for (char c : value)
            {
                if (c == '&')
                    out += "&amp;";
                else if (c == '<')
                    out += "&lt;";
                else if (c == '>')
                    out += "&gt;";
                else if (c == '\"')
                    out += "&quot;";
                else if (c == '\'')
                    out += "&apos;";
                else
                    out += c;
            }
            return out;
        }

        std::string propertyValue(const std::string& value)
        {
            std::string out;
            for (char c : value)
            {
                if (c == '%')
                    out += "%25";
                else if (c == ';')
                    out += "%3B";
                else if (c == ',')
                    out += "%2C";
                else
                    out += c;
            }
            return out;
        }

        std::string join(const std::vector<std::string>& values)
        {
            std::string out;
            for (const auto& value : values)
            {
                if (!out.empty())
                    out += ';';
                out += value;
            }
            return out;
        }

        void validateProjectField(const std::string& text)
        {
            if (text.empty() || text.find_first_of("|\"\r\n") != std::string::npos)
                throw std::runtime_error("invalid external project configuration/platform: " + text);
        }
    } // namespace

    std::string NativeProjectBuilder::nativeProjectConfiguration(const Target& t, Configuration config)
    {
        const auto key = BuildModel::toString(config);
        return t.m_nativeProject.m_configurations.contains(key) ? t.m_nativeProject.m_configurations.at(key) : key;
    }

    std::string NativeProjectBuilder::nativeProjectArchitecture(const Target& t, const std::string& arch)
    {
        return t.m_nativeProject.m_architectures.contains(arch) ? t.m_nativeProject.m_architectures.at(arch) : arch == "arm64" ? "ARM64" : "x64";
    }

    std::string NativeProjectBuilder::nativeProjectGuid(const Target& t)
    {
        std::smatch match;
        const auto xml = FileSystem::readAll(t.m_project);
        if (std::regex_search(xml, match, std::regex(R"(<(?:\w+:)?ProjectGuid>\s*(\{[0-9A-Fa-f-]{36}\})\s*</)", std::regex::icase)))
            return match[1].str();
        return {};
    }

    void NativeProjectBuilder::validateNativeProject(const Workspace& ws, const Target& t)
    {
        if (!BuildModel::isNativeProject(t))
            return;
        const auto path = fs::absolute(ws.m_root / t.m_project);
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            }
        );
        if (extension != ".vcxproj" || !fs::is_regular_file(path))
            throw std::runtime_error("native project requires an existing .vcxproj: " + path.string());
        if (path.string().find_first_of("\"\r\n") != std::string::npos)
            throw std::runtime_error("native project path cannot contain quotes/newlines");
        if (t.m_linkType != "monolithic")
            throw std::runtime_error("external native project owns its binary graph; linktype must be Monolithic: " + t.m_name);
        if (!t.m_sources.empty() || !t.m_compile.m_pchHeader.empty() || t.m_compile.m_documentation.value_or(false))
            throw std::runtime_error("external native project owns sources/PCH/documentation; use inputs and public compile interfaces: " + t.m_name);
        for (const auto& [key, value] : t.m_nativeProject.m_configurations)
        {
            (void)BuildModel::parseConfiguration(key);
            validateProjectField(value);
        }
        for (const auto& [key, value] : t.m_nativeProject.m_architectures)
        {
            const auto arch = BuildModel::normalizeArchitecture(key);
            validateProjectField(value);
            auto lower = value;
            std::transform(
                lower.begin(),
                lower.end(),
                lower.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );
            if (lower == "win32" || lower == "x86" || lower == "arm" || lower == "arm32" || lower == "anycpu" || lower == "any cpu")
                throw std::runtime_error("external native project must use a 64-bit platform");
            if ((lower == "x64" || lower == "arm64") && lower != arch)
                throw std::runtime_error("external architecture mapping changes the target ABI");
        }
        static const std::set<std::string> s_reserved = {
            "Configuration", "Platform", "OutDir", "IntDir", "TargetName", "TargetExt", "TargetPath", "ForceImportBeforeCppTargets"
        };
        for (const auto& [key, value] : t.m_nativeProject.m_properties)
        {
            (void)value;
            if (!std::regex_match(key, std::regex("[A-Za-z_][A-Za-z0-9_.]*")) || s_reserved.contains(key))
                throw std::runtime_error("reserved/invalid external property: " + key);
        }
    }

    void NativeProjectBuilder::planNativeProject(
        BuildGraph& graph,
        const Workspace& ws,
        const Toolchain& tc,
        const fs::path& build,
        Configuration config,
        const fs::path& self,
        const Target& t,
        const std::vector<std::string>& includes,
        const std::vector<std::string>& defines
    )
    {
        if (tc.m_kind != CompilerKind::MSVC || tc.m_platform != "windows")
            throw std::runtime_error("external vcxproj builds require Windows MSVC: " + t.m_name);
        if (!fs::is_regular_file(tc.m_msbuild))
            throw std::runtime_error("MSBuild.exe not found for native project: " + t.m_name);
        const auto selectedConfig = NativeProjectBuilder::nativeProjectConfiguration(t, config),
                   selectedArch = NativeProjectBuilder::nativeProjectArchitecture(t, tc.m_architecture);
        validateProjectField(selectedConfig);
        validateProjectField(selectedArch);
        auto project = sdkPath(ws.m_root / t.m_project);
        const auto projectText = FileSystem::readAll(project);
        const std::regex declaration(R"re(<(?:\w+:)?ProjectConfiguration\b[^>]*Include\s*=\s*["']([^"']+)["'])re", std::regex::icase);
        bool declared = false, matched = false;
        for (auto i = std::sregex_iterator(projectText.begin(), projectText.end(), declaration); i != std::sregex_iterator(); ++i)
        {
            declared = true;
            matched |= (*i)[1].str() == selectedConfig + "|" + selectedArch;
        }
        if (declared && !matched)
            throw std::runtime_error("external project '" + t.m_name + "' has no mapped configuration " + selectedConfig + "|" + selectedArch);
        const auto folder = sdkPath(build / "native-projects" / t.m_name);
        const auto artifact = sdkPath(graph.m_targetOutputs.at(t.m_name).front());
        const auto out = artifact.parent_path().generic_string() + "/";
        const auto intermediate = (folder / "obj").generic_string() + "/";
        const std::string runtime = NativeOptions::runtimePolicy(t.m_compile) == "static"
                                        ? (t.m_compile.m_debugCRT.value_or(BuildModel::isDebug(config)) ? "MultiThreadedDebug" : "MultiThreaded")
                                        : (t.m_compile.m_debugCRT.value_or(BuildModel::isDebug(config)) ? "MultiThreadedDebugDLL" : "MultiThreadedDLL");
        auto guidTarget = t;
        guidTarget.m_project = project.string();
        const auto guid = NativeProjectBuilder::nativeProjectGuid(guidTarget);
        const auto rootCondition =
            guid.empty() ? "'$(MSBuildProjectFullPath)' == '" + escapeXml(project.string()) + "'" : "'$(ProjectGuid)' == '" + escapeXml(guid) + "'";
        std::string xml = "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
        xml += "<Import Project=\"$(AxlumNativeBeforeTargets)\" Condition=\"Exists('$(AxlumNativeBeforeTargets)')\"/>\n";
        xml += "<PropertyGroup><OutDir>" + escapeXml(out) + "</OutDir><IntDir>" + escapeXml(intermediate) +
               "$(MSBuildProjectName)/$(ProjectGuid)/</IntDir><LinkIncremental>" + (t.m_link.m_incremental.value_or(false) ? "true" : "false") +
               "</LinkIncremental></PropertyGroup>\n";
        xml += "<PropertyGroup Condition=\"" + rootCondition + "\"><TargetName>" + escapeXml(artifact.stem().string()) + "</TargetName><TargetExt>" +
               escapeXml(artifact.extension().string()) + "</TargetExt><TargetPath>" + escapeXml(artifact.string()) + "</TargetPath></PropertyGroup>\n";
        xml += "<ItemDefinitionGroup><ClCompile><RuntimeLibrary>" + runtime + "</RuntimeLibrary></ClCompile></ItemDefinitionGroup>\n";
        xml += "<ItemDefinitionGroup Condition=\"" + rootCondition + "\"><ClCompile>";
        std::vector<std::string> resolvedIncludes;
        for (const auto& include : includes)
            resolvedIncludes.push_back(sdkPath(NativePaths::resolveBuildInput(ws, build, include)).generic_string());
        xml += "<AdditionalIncludeDirectories>" + escapeXml(join(resolvedIncludes)) +
               ";%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories><PreprocessorDefinitions>" + escapeXml(join(defines)) +
               ";%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile>";
        std::vector<std::string> libraries = t.m_link.m_libraries, directories = t.m_link.m_libraryDirectories;
        std::map<std::string, const Target*> targets;
        for (const auto& candidate : ws.m_targets)
            targets[candidate.m_name] = &candidate;
        std::set<std::string> visited;
        std::function<void(const std::string&)> dependency = [&](const std::string& name)
        {
            if (!visited.insert(name).second)
                return;
            const auto& dep = *targets.at(name);
            if (dep.m_kind == TargetKind::SharedLibrary && !dep.m_link.m_importLibrary)
                throw std::runtime_error("cannot link a DLL with importlibrary(false): " + name);
            if (dep.m_kind == TargetKind::StaticLibrary || dep.m_kind == TargetKind::SharedLibrary)
                libraries.push_back(graph.m_targetOutputs.at(name).at(dep.m_kind == TargetKind::SharedLibrary ? 1 : 0).string());
            if (dep.m_kind == TargetKind::StaticLibrary || dep.m_kind == TargetKind::HeaderOnly)
            {
                libraries.insert(libraries.end(), dep.m_link.m_libraries.begin(), dep.m_link.m_libraries.end());
                directories.insert(directories.end(), dep.m_link.m_libraryDirectories.begin(), dep.m_link.m_libraryDirectories.end());
                for (const auto* scope : {&dep.m_publicDependencies, &dep.m_privateDependencies, &dep.m_interfaceDependencies})
                    for (const auto& next : *scope)
                        dependency(next);
            }
            else
                for (const auto* scope : {&dep.m_publicDependencies, &dep.m_interfaceDependencies})
                    for (const auto& next : *scope)
                        dependency(next);
        };
        for (const auto* scope : {&t.m_publicDependencies, &t.m_privateDependencies})
            for (const auto& dep : *scope)
                dependency(dep);
        for (auto& lib : libraries)
            if (lib.find_first_of("/\\") != std::string::npos)
                lib = NativePaths::resolveBuildInput(ws, build, lib).generic_string();
        for (auto& directory : directories)
            directory = NativePaths::resolveBuildInput(ws, build, directory).generic_string();
        xml += "<Lib><OutputFile>" + escapeXml(artifact.string()) + "</OutputFile></Lib><Link><OutputFile>" + escapeXml(artifact.string()) + "</OutputFile>";
        if (t.m_kind == TargetKind::SharedLibrary && t.m_link.m_importLibrary)
            xml += "<ImportLibrary>" + escapeXml(sdkPath(graph.m_targetOutputs.at(t.m_name).at(1)).string()) + "</ImportLibrary>";
        auto pdb = artifact;
        pdb.replace_extension(".pdb");
        xml += "<ProgramDatabaseFile>" + escapeXml(pdb.string()) + "</ProgramDatabaseFile><GenerateDebugInformation>" +
               (NativeOptions::symbolsEnabled(t.m_compile, config) ? "true" : "false") + "</GenerateDebugInformation>";
        auto linkFlags = NativeOptions::nativeLinkFlags(t, tc, config);
        if (!t.m_link.m_importLibrary)
            linkFlags.push_back("/NOIMPLIB");
        for (const auto& symbol : t.m_link.m_exports)
            linkFlags.push_back("/EXPORT:" + symbol);
        linkFlags.insert(linkFlags.end(), t.m_link.m_linkFlags.begin(), t.m_link.m_linkFlags.end());
        std::string options;
        for (const auto& option : linkFlags)
            options += Process::quoteRspArg(option, false) + " ";
        xml += "<AdditionalOptions>" + escapeXml(options) + "%(AdditionalOptions)</AdditionalOptions><AdditionalDependencies>" + escapeXml(join(libraries)) +
               ";%(AdditionalDependencies)</AdditionalDependencies><AdditionalLibraryDirectories>" + escapeXml(join(directories)) +
               ";%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories></Link></ItemDefinitionGroup></Project>\n";
        const auto expected = t.m_kind == TargetKind::StaticLibrary   ? "StaticLibrary"
                              : t.m_kind == TargetKind::SharedLibrary ? "DynamicLibrary"
                                                                      : "Application";
        const auto close = xml.rfind("</Project>");
        xml.insert(
            close,
            "<Target Name=\"AxlumNativeContract\" BeforeTargets=\"PrepareForBuild\" Condition=\"" + rootCondition +
                "\"><Error Condition=\"'$(ConfigurationType)' != '" + std::string(expected) + "'\" Text=\"External native project kind mismatch; expected " +
                expected + ".\"/></Target>\n"
        );
        const auto bridge = folder / "axlum-output.props";
        FileSystem::writeIfChanged(bridge, xml);
        nlohmann::json plan = {
            {"msbuild", tc.m_msbuild},
            {"project", project.string()},
            {"configuration", selectedConfig},
            {"platform", selectedArch},
            {"bridge", bridge.string()},
            {"properties", t.m_nativeProject.m_properties}
        };
        const auto file = folder / "build.json";
        FileSystem::writeIfChanged(file, plan.dump(2) + "\n");
        graph.m_nativeProjectPlans[t.m_name] = file.string();
        Action action;
        action.m_name = "MSBuild " + t.m_name;
        action.m_owner = t.m_name;
        action.m_workingDirectory = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
        action.m_inputs = {project, bridge, file, tc.m_msbuild};
        action.m_outputs = graph.m_targetOutputs.at(t.m_name);
        action.m_always = true;
        action.m_restat = true;
        for (const auto& input : t.m_inputs)
            action.m_inputs.push_back(NativePaths::resolveBuildInput(ws, build, input));
        for (const auto* scope : {&t.m_publicDependencies, &t.m_privateDependencies})
            for (const auto& dep : *scope)
                for (const auto& output : graph.m_targetOutputs.at(dep))
                    action.m_orderOnly.push_back(output);
        action.m_argv = {self.string(), "__native-project", file.string()};
        graph.m_actions.push_back(std::move(action));
    }

    std::vector<std::string> NativeProjectBuilder::nativeProjectCommand(const fs::path& file, bool clean)
    {
        const auto plan = nlohmann::json::parse(FileSystem::readAll(file));
        const bool rebuild = std::getenv("AXLUM_REBUILD") && std::string(std::getenv("AXLUM_REBUILD")) == "1";
        std::vector<std::string> command = {
            plan.at("msbuild"),
            plan.at("project"),
            "/nologo",
            "/nodeReuse:false",
            "/verbosity:minimal",
            clean     ? "/t:Clean"
            : rebuild ? "/t:Rebuild"
                      : "/t:Build",
            "/p:Configuration=" + propertyValue(plan.at("configuration").get<std::string>()),
            "/p:Platform=" + propertyValue(plan.at("platform").get<std::string>()),
            "/p:ForceImportBeforeCppTargets=" + propertyValue(plan.at("bridge").get<std::string>())
        };
        for (const auto& [key, value] : plan.at("properties").items())
            command.push_back("/p:" + key + "=" + propertyValue(value.get<std::string>()));
        if (const auto* jobs = std::getenv("AXLUM_BUILD_JOBS"); jobs && *jobs)
            command.push_back("/m:" + std::to_string(std::stoi(jobs)));
        return command;
    }

    int NativeProjectBuilder::buildNativeProject(const std::vector<std::string>& args)
    {
        if (args.size() != 1)
            throw std::runtime_error("__native-project requires its build plan");
        const auto command = NativeProjectBuilder::nativeProjectCommand(args.front());
        return Process::run(command, fs::path(command.at(1)).parent_path());
    }
} // namespace axlum
