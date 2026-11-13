/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace axlum
{
    namespace fs = std::filesystem;
    struct PackageManifest;

    enum class TargetKind
    {
        Executable,
        StaticLibrary,
        SharedLibrary,
        HeaderOnly,
        DotNetProject,
        Codegen
    };

    struct Configuration
    {
        std::string m_name;
        static const Configuration s_debug, s_release;
        bool operator==(const Configuration&) const = default;
    };

    struct BuildPreset
    {
        std::string m_name, m_configuration = "Debug", m_architecture = "x64", m_platform, m_toolchain, m_compiler = "auto";
    };

    struct WorkspaceReference
    {
        std::string m_alias;
        fs::path m_manifest;
    };

    struct BuildHook
    {
        std::string m_name, m_tool, m_depfile;
        std::vector<std::string> m_args, m_inputs, m_outputs;
    };
    enum class CompilerKind
    {
        MSVC,
        GCC,
        Clang
    };

    struct CompileOptions
    {
        std::vector<std::string> m_publicIncludes;
        std::vector<std::string> m_privateIncludes;
        std::vector<std::string> m_publicDefines;
        std::vector<std::string> m_privateDefines;
        std::vector<std::string> m_cFlags;
        std::vector<std::string> m_cxxFlags;
        std::string m_cppStandard = "c++20";
        std::string m_cStandard = "c11";
        std::string m_optimization, m_symbols, m_warnings, m_runtime, m_inlining;
        std::vector<std::string> m_sanitizers;
        std::optional<bool> m_functionLevelLinking, m_runtimeChecks, m_stringPooling, m_intrinsicFunctions, m_bufferSecurityCheck, m_debugCRT, m_sharedPch,
            m_documentation;
        std::optional<bool> m_warningsAsErrors, m_exceptions, m_rtti, m_lto, m_pic, m_pchEnabled;
        std::string m_pchHeader, m_pchSource, m_pchFile, m_pchObject;
    };

    struct LinkOptions
    {
        std::vector<std::string> m_libraries;
        std::vector<std::string> m_libraryDirectories;
        std::vector<std::string> m_frameworks, m_frameworkDirectories;
        std::vector<std::string> m_linkFlags;
        std::optional<bool> m_incremental;
        std::string m_subsystem, m_entryPoint, m_moduleDefinition, m_manifest, m_debugLink;
        std::vector<std::string> m_delayLoadLibraries;
        std::vector<std::string> m_exports;
        bool m_importLibrary = true;
    };

    struct Module
    {
        std::string m_name;
        std::string m_targetName;
        std::vector<std::string> m_sources;
        std::vector<std::string> m_excludedSources;
        std::vector<std::string> m_ideFiles;
        CompileOptions m_compile;
        std::vector<std::string> m_publicDependencies, m_privateDependencies;
        std::string m_binaryModuleName, m_apiDefine;
        int m_declarationLine = 0;
    };

    struct AssemblyAttribute
    {
        std::string m_type;
        std::vector<std::string> m_arguments;
    };

    struct NuGetReference
    {
        std::string m_name, m_version, m_privateAssets, m_includeAssets, m_excludeAssets;
    };

    struct ManagedOptions
    {
        std::map<std::string, std::string> m_properties;
        std::vector<std::string> m_defines, m_noWarn, m_warningsAsErrors, m_warningsNotAsErrors;
        std::vector<std::string> m_analyzers, m_analyzerProjects, m_additionalFiles, m_editorConfigs, m_nativeLibraries;
        std::vector<std::string> m_directPInvoke;
        std::vector<AssemblyAttribute> m_attributes;
        std::vector<NuGetReference> m_packages;
    };

    struct PublishOptions
    {
        std::string m_rid, m_directory, m_nativeLibrary;
        std::optional<bool> m_aot, m_selfContained, m_trimmed, m_singleFile, m_stripSymbols;
        std::vector<std::string> m_exports, m_dependencies, m_systemDependencies;
    };

    struct AndroidOptions
    {
        std::string m_applicationId, m_activity = "android.app.NativeActivity", m_manifest;
        int m_minSdk = 23, m_targetSdk = 35;
    };

    struct InstallOptions
    {
        std::vector<std::string> m_headers, m_files;
        std::string m_headerBase = "include", m_bindir = "bin", m_libdir = "lib", m_includedir = "include", m_datadir = "share";
        bool m_enabled = true;
    };

    struct IDECustomization
    {
        std::map<std::string, std::string> m_properties, m_debugger;
        std::string m_projectBegin, m_projectEnd, m_filtersBegin, m_filtersEnd, m_userBegin, m_userEnd;
        std::string m_jsonPatch;
    };

    struct NativeProjectOptions
    {
        std::map<std::string, std::string> m_configurations, m_architectures, m_properties;
    };

    struct Target
    {
        std::string m_name;
        fs::path m_sourceRoot;
        std::vector<std::string> m_platforms, m_architectures, m_configurations;
        NativeProjectOptions m_nativeProject;
        TargetKind m_kind = TargetKind::Executable;
        std::vector<std::string> m_sources;
        std::vector<std::string> m_excludedSources;
        std::vector<std::string> m_packages;
        std::vector<std::string> m_publicPackages;
        std::string m_language;
        std::vector<std::string> m_publicDependencies;
        std::vector<std::string> m_privateDependencies;
        std::vector<std::string> m_interfaceDependencies;
        std::vector<std::string> m_publicModules, m_privateModules;
        CompileOptions m_compile;
        LinkOptions m_link;
        std::string m_linkType = "monolithic";
        std::string m_outputName, m_outputDirectory, m_objectDirectory, m_importLibraryDirectory;
        std::string m_icon, m_documentationDirectory;
        std::map<std::string, IDECustomization> m_ide;
        std::vector<std::string> m_runtimeFiles, m_optionalRuntimeFiles, m_resourceInputs;
        std::string m_project;   // dotnet_project
        std::string m_framework; // SDK project generated for CSharpLibrary/CSharpConsole
        bool m_managedExecutable = false;
        bool m_generatedProject = false;
        std::vector<std::string> m_references;
        ManagedOptions m_managed;
        PublishOptions m_publish;
        InstallOptions m_install;
        AndroidOptions m_android;
        std::string m_tool; // codegen executable or @target/TargetName
        std::vector<std::string> m_args;
        std::vector<std::string> m_inputs;
        std::vector<std::string> m_outputs;
        std::string m_depfile;
        std::vector<BuildHook> m_preBuild, m_postBuild;
        std::vector<std::string> m_ideFiles;
        int m_declarationLine = 0;
    };

    struct Workspace
    {
        std::string m_name;
        std::map<std::string, IDECustomization> m_ide;
        fs::path m_manifest;
        fs::path m_root;
        std::vector<Configuration> m_configurations;
        std::vector<std::string> m_architectures;
        std::vector<BuildPreset> m_presets;
        std::vector<WorkspaceReference> m_references;
        std::unordered_map<std::string, std::string> m_targetAliases;
        std::map<std::string, std::string> m_disabledTargets;
        std::vector<Target> m_targets;
        std::vector<Module> m_modules;
        std::shared_ptr<PackageManifest> m_packageManifest;
    };

    class BuildModel
    {
      public:
        BuildModel() = delete;

      public:
        static void mergeIDECustomization(IDECustomization& destination, const IDECustomization& source);
        static std::string toString(TargetKind kind);
        static TargetKind parseTargetKind(std::string_view value);
        static Configuration parseConfiguration(std::string_view value);
        static std::string toString(Configuration config);
        static bool isDebug(Configuration config);
        static std::string normalizeArchitecture(std::string_view value);
        static bool isNativeProject(const Target& target);
        static std::string targetUnsupportedReason(const Target& target, Configuration config, const std::string& architecture, const std::string& platform);
        static void selectTargets(Workspace& workspace, Configuration config, const std::string& architecture, const std::string& platform);
        static void validateWorkspace(const Workspace& ws);
        static std::vector<std::size_t> topologicalTargets(const Workspace& ws);
    };
} // namespace axlum
