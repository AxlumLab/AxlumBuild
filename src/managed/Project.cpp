/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#include "managed/Project.hpp"
#include "core/Process.hpp"
#include "graph/NativePaths.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>

namespace axlum
{
    namespace
    {
        std::string escapeXml(const std::string& s)
        {
            std::string out;
            for (const auto c : s)
            {
                if (c == '&')
                    out += "&amp;";
                else if (c == '<')
                    out += "&lt;";
                else if (c == '>')
                    out += "&gt;";
                else if (c == '"')
                    out += "&quot;";
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
    } // namespace

    void ManagedProjectGenerator::validateManagedOptions(const Target& t)
    {
        const auto& m = t.m_managed;
        if (!t.m_project.empty() && !t.m_generatedProject &&
            (!m.m_properties.empty() || !m.m_defines.empty() || !m.m_noWarn.empty() || !m.m_warningsAsErrors.empty() || !m.m_warningsNotAsErrors.empty() ||
             !m.m_analyzers.empty() || !m.m_analyzerProjects.empty() || !m.m_additionalFiles.empty() || !m.m_editorConfigs.empty() ||
             !m.m_nativeLibraries.empty() || !m.m_directPInvoke.empty() || !m.m_attributes.empty() || !m.m_packages.empty()))
            throw std::runtime_error("existing project controls its own managed compilation settings: " + t.m_name);
        for (const auto& define : m.m_defines)
            if (!std::regex_match(define, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                throw std::runtime_error("C# DefineConstants requires identifiers: " + define);
        std::set<std::string> names;
        for (const auto& package : m.m_packages)
            if (!names.insert(package.m_name).second)
                throw std::runtime_error("duplicate NuGet package: " + package.m_name);
        if (m.m_properties.contains("AssemblyName"))
        {
            const auto& name = m.m_properties.at("AssemblyName");
            if (name.empty() || name.find_first_of("/\\:\r\n") != std::string::npos || name == "." || name == "..")
                throw std::runtime_error("invalid assembly name: " + name);
        }
        if (!t.m_publish.m_nativeLibrary.empty() && t.m_managedExecutable)
            throw std::runtime_error("NativeLib=Shared requires a C# library target: " + t.m_name);
    }

    void ManagedProjectGenerator::prepareManagedProjects(Workspace& ws, const fs::path& build, const fs::path& executable)
    {
        for (auto& t : ws.m_targets)
            if (t.m_kind == TargetKind::DotNetProject && !t.m_framework.empty())
            {
                if (!t.m_project.empty() && !t.m_generatedProject)
                    throw std::runtime_error("use either project(...) or framework(...), not both: " + t.m_name);
                t.m_project = fs::absolute(build / "dotnet" / t.m_name / (t.m_name + ".csproj")).lexically_normal().string();
                t.m_generatedProject = true;
            }
        for (const auto& t : ws.m_targets)
        {
            if (t.m_kind != TargetKind::DotNetProject || t.m_framework.empty())
                continue;
            auto properties = t.m_managed.m_properties;
            properties["TargetFramework"] = t.m_framework;
            properties["OutputType"] = t.m_managedExecutable ? "Exe" : "Library";
            properties["EnableDefaultCompileItems"] = "false";
            if (!properties.contains("ImplicitUsings"))
                properties["ImplicitUsings"] = "enable";
            const auto root = t.m_sourceRoot.empty() ? ws.m_root : t.m_sourceRoot;
            for (const auto* name : {"NuGet.Config", "nuget.config"})
                if (fs::is_regular_file(root / name))
                {
                    properties["RestoreConfigFile"] = fs::absolute(root / name).generic_string();
                    break;
                }
            // Keep generated projects independent of properties imported by an enclosing bootstrap build.
            if (!t.m_managed.m_defines.empty())
                properties["DefineConstants"] = "$(DefineConstants);" + join(t.m_managed.m_defines);
            if (!t.m_managed.m_noWarn.empty())
                properties["NoWarn"] = "$(NoWarn);" + join(t.m_managed.m_noWarn);
            if (!t.m_managed.m_warningsAsErrors.empty())
                properties["WarningsAsErrors"] = join(t.m_managed.m_warningsAsErrors);
            if (!t.m_managed.m_warningsNotAsErrors.empty())
                properties["WarningsNotAsErrors"] = join(t.m_managed.m_warningsNotAsErrors);
            if (t.m_compile.m_warningsAsErrors.has_value())
                properties["TreatWarningsAsErrors"] = *t.m_compile.m_warningsAsErrors ? "true" : "false";
            if (!t.m_compile.m_optimization.empty())
                properties["Optimize"] = t.m_compile.m_optimization == "off" ? "false" : "true";
            if (!t.m_compile.m_symbols.empty())
            {
                properties["DebugType"] = t.m_compile.m_symbols == "off" ? "none" : "portable";
                properties["DebugSymbols"] = t.m_compile.m_symbols == "off" ? "false" : "true";
            }
            if (t.m_publish.m_aot.value_or(false) || !t.m_publish.m_nativeLibrary.empty())
                properties["IsAotCompatible"] = "true";
            if (properties.contains("EmitCompilerGeneratedFiles") && properties.at("EmitCompilerGeneratedFiles") == "true")
                properties["CompilerGeneratedFilesOutputPath"] = fs::absolute(build / "dotnet" / t.m_name / "generated").generic_string();
            std::string xml = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n";
            for (const auto& [key, value] : properties)
                xml += "    <" + key + ">" + escapeXml(value) + "</" + key + ">\n";
            xml += "  </PropertyGroup>\n  <ItemGroup>\n";
            auto path = [&](const std::string& value)
            {
                return escapeXml(NativePaths::resolveBuildInput(ws, build, value).generic_string());
            };
            for (const auto& source : t.m_sources)
                xml += "    <Compile Include=\"" + path(source) + "\" />\n";
            auto reference = [&](const std::string& value, bool analyzer)
            {
                const auto target = std::find_if(
                    ws.m_targets.begin(),
                    ws.m_targets.end(),
                    [&](const auto& other)
                    {
                        return other.m_name == value;
                    }
                );
                if (target != ws.m_targets.end() || fs::path(value).extension() == ".csproj")
                {
                    const auto file = target != ws.m_targets.end() ? target->m_project : value;
                    xml += "    <ProjectReference Include=\"" + path(file) + "\"" +
                           (analyzer ? " OutputItemType=\"Analyzer\" ReferenceOutputAssembly=\"false\"" : "") + " />\n";
                }
                else if (analyzer)
                    throw std::runtime_error("analyzer_projects requires a managed target or .csproj: " + value);
                else if (fs::path(value).extension() == ".dll")
                    xml +=
                        "    <Reference Include=\"" + escapeXml(fs::path(value).stem().string()) + "\"><HintPath>" + path(value) + "</HintPath></Reference>\n";
                else
                    xml += "    <Reference Include=\"" + escapeXml(value) + "\" />\n";
            };
            for (const auto& ref : t.m_references)
                reference(ref, false);
            for (const auto& ref : t.m_managed.m_analyzerProjects)
                reference(ref, true);
            for (const auto& module : t.m_managed.m_directPInvoke)
                xml += "    <DirectPInvoke Include=\"" + escapeXml(module) + "\" />\n";
            for (const auto& [kind, files] : std::vector<std::pair<std::string, const std::vector<std::string>*>>{
                     {"Analyzer", &t.m_managed.m_analyzers},
                     {"AdditionalFiles", &t.m_managed.m_additionalFiles},
                     {"EditorConfigFiles", &t.m_managed.m_editorConfigs},
                     {"NativeLibrary", &t.m_managed.m_nativeLibraries}
                 })
                for (const auto& file : *files)
                    xml += "    <" + kind + " Include=\"" + path(file) + "\" />\n";
            for (const auto& attribute : t.m_managed.m_attributes)
            {
                xml += "    <AssemblyAttribute Include=\"" + escapeXml(attribute.m_type) + "\">";
                for (std::size_t i = 0; i < attribute.m_arguments.size(); ++i)
                    xml += "<_Parameter" + std::to_string(i + 1) + ">" + escapeXml(attribute.m_arguments[i]) + "</_Parameter" + std::to_string(i + 1) + ">";
                xml += "</AssemblyAttribute>\n";
            }
            for (const auto& package : t.m_managed.m_packages)
            {
                xml += "    <PackageReference Include=\"" + escapeXml(package.m_name) + "\" Version=\"" + escapeXml(package.m_version) + "\"";
                if (!package.m_privateAssets.empty())
                    xml += " PrivateAssets=\"" + escapeXml(package.m_privateAssets) + "\"";
                if (!package.m_includeAssets.empty())
                    xml += " IncludeAssets=\"" + escapeXml(package.m_includeAssets) + "\"";
                if (!package.m_excludeAssets.empty())
                    xml += " ExcludeAssets=\"" + escapeXml(package.m_excludeAssets) + "\"";
                xml += " />\n";
            }
            xml += "  </ItemGroup>\n"
                   "  <Target Name=\"AxlumNormalizeEditorConfigs\" BeforeTargets=\"CoreCompile\">\n"
                   "    <ItemGroup>\n"
                   "      <_AxlumNormalizedConfig Include=\"@(EditorConfigFiles-&gt;'%(FullPath)'-&gt;Distinct())\" />\n"
                   "      <EditorConfigFiles Remove=\"@(EditorConfigFiles)\" />\n"
                   "      <EditorConfigFiles Include=\"@(_AxlumNormalizedConfig)\" />\n"
                   "    </ItemGroup>\n"
                   "  </Target>\n";
            if (!executable.empty())
            {
                const auto command = Process::quoteShellArg(executable.string()) + " build -B " + Process::quoteShellArg(fs::absolute(build).string()) +
                                     " -t " + Process::quoteShellArg(t.m_name);
                xml += "  <Target Name=\"AxlumPrepareGraph\" BeforeTargets=\"PrepareForBuild\" "
                       "Condition=\"'$(AxlumInvocation)' != 'true' and '$(DesignTimeBuild)' != 'true'\"><Exec Command=\"" +
                       escapeXml(command) + "\" /></Target>\n";
            }
            xml += "</Project>\n";
            FileSystem::writeIfChanged(
                fs::path(t.m_project).parent_path() / "Directory.Build.props",
                "<Project><PropertyGroup><UseArtifactsOutput>true</UseArtifactsOutput>"
                "<ArtifactsPath Condition=\"'$(ArtifactsPath)' == ''\">" +
                    escapeXml(fs::absolute(build / "dotnet" / t.m_name / "artifacts").generic_string()) + "</ArtifactsPath></PropertyGroup></Project>\n"
            );
            FileSystem::writeIfChanged(t.m_project, xml);
        }
    }
} // namespace axlum
