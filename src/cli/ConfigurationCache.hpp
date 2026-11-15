/*
 * @Author: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once
#include "backend/Ide.hpp"
#include "core/Process.hpp"
#include <stdexcept>

namespace nlohmann
{
    template <class T> struct adl_serializer<std::optional<T>>
    {
        static void to_json(json& j, const std::optional<T>& v)
        {
            if (v)
                j = *v;
            else
                j = nullptr;
        }

        static void from_json(const json& j, std::optional<T>& v)
        {
            if (j.is_null())
                v.reset();
            else
                v = j.get<T>();
        }
    };
} // namespace nlohmann

namespace axlum
{
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Configuration, m_name)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BuildPreset, m_name, m_configuration, m_architecture, m_platform, m_toolchain, m_compiler, m_options)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WorkspaceReference, m_alias, m_manifest)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BuildHook, m_name, m_tool, m_depfile, m_args, m_inputs, m_outputs)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        CompileOptions,
        m_publicIncludes,
        m_privateIncludes,
        m_publicDefines,
        m_privateDefines,
        m_forceIncludes,
        m_charset,
        m_cFlags,
        m_cxxFlags,
        m_cppStandard,
        m_cStandard,
        m_optimization,
        m_symbols,
        m_warnings,
        m_runtime,
        m_inlining,
        m_sanitizers,
        m_functionLevelLinking,
        m_runtimeChecks,
        m_stringPooling,
        m_intrinsicFunctions,
        m_bufferSecurityCheck,
        m_debugCRT,
        m_sharedPch,
        m_documentation,
        m_warningsAsErrors,
        m_exceptions,
        m_rtti,
        m_lto,
        m_pic,
        m_pchEnabled,
        m_pchHeader,
        m_pchSource,
        m_pchFile,
        m_pchObject
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        LinkOptions,
        m_libraries,
        m_libraryDirectories,
        m_frameworks,
        m_frameworkDirectories,
        m_linkFlags,
        m_incremental,
        m_subsystem,
        m_entryPoint,
        m_moduleDefinition,
        m_manifest,
        m_debugLink,
        m_delayLoadLibraries,
        m_exports,
        m_importLibrary
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        Module,
        m_name,
        m_targetName,
        m_sources,
        m_excludedSources,
        m_ideFiles,
        m_compile,
        m_publicDependencies,
        m_privateDependencies,
        m_binaryModuleName,
        m_apiDefine,
        m_declarationLine
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AssemblyAttribute, m_type, m_arguments)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NuGetReference, m_name, m_version, m_privateAssets, m_includeAssets, m_excludeAssets)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        ManagedOptions,
        m_properties,
        m_defines,
        m_noWarn,
        m_warningsAsErrors,
        m_warningsNotAsErrors,
        m_analyzers,
        m_analyzerProjects,
        m_additionalFiles,
        m_editorConfigs,
        m_nativeLibraries,
        m_directPInvoke,
        m_attributes,
        m_packages
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        PublishOptions,
        m_rid,
        m_directory,
        m_nativeLibrary,
        m_aot,
        m_selfContained,
        m_trimmed,
        m_singleFile,
        m_stripSymbols,
        m_exports,
        m_dependencies,
        m_systemDependencies
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AndroidOptions, m_applicationId, m_activity, m_manifest, m_minSdk, m_targetSdk)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InstallOptions, m_headers, m_files, m_headerBase, m_bindir, m_libdir, m_includedir, m_datadir, m_enabled)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        IDECustomization,
        m_properties,
        m_debugger,
        m_projectBegin,
        m_projectEnd,
        m_filtersBegin,
        m_filtersEnd,
        m_userBegin,
        m_userEnd,
        m_jsonPatch
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NativeProjectOptions, m_configurations, m_architectures, m_properties)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        Target,
        m_features,
        m_hostTool,
        m_hostArtifact,
        m_name,
        m_sourceRoot,
        m_platforms,
        m_architectures,
        m_configurations,
        m_nativeProject,
        m_kind,
        m_sources,
        m_excludedSources,
        m_packages,
        m_publicPackages,
        m_language,
        m_publicDependencies,
        m_privateDependencies,
        m_interfaceDependencies,
        m_publicModules,
        m_privateModules,
        m_compile,
        m_link,
        m_linkType,
        m_outputName,
        m_outputDirectory,
        m_objectDirectory,
        m_importLibraryDirectory,
        m_icon,
        m_documentationDirectory,
        m_ide,
        m_runtimeFiles,
        m_optionalRuntimeFiles,
        m_resourceInputs,
        m_project,
        m_framework,
        m_managedExecutable,
        m_generatedProject,
        m_references,
        m_managed,
        m_publish,
        m_install,
        m_android,
        m_tool,
        m_args,
        m_inputs,
        m_outputs,
        m_depfile,
        m_preBuild,
        m_postBuild,
        m_ideFiles,
        m_declarationLine
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        Workspace,
        m_configurationRoots,
        m_localPackageRoots,
        m_features,
        m_name,
        m_ide,
        m_manifest,
        m_root,
        m_configurations,
        m_architectures,
        m_presets,
        m_references,
        m_targetAliases,
        m_disabledTargets,
        m_targets,
        m_modules
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        Toolchain,
        m_kind,
        m_host,
        m_target,
        m_cCompiler,
        m_cppCompiler,
        m_linker,
        m_archiver,
        m_ninja,
        m_dotnet,
        m_extensionObject,
        m_extensionExecutable,
        m_extensionStatic,
        m_extensionShared,
        m_id,
        m_compilerVersion,
        m_sdkVersion,
        m_resourceCompiler,
        m_manifestTool,
        m_cmake,
        m_documentationTool,
        m_msbuild,
        m_platform,
        m_architecture,
        m_androidAbi,
        m_targetFlags,
        m_environment,
        m_systemIncludes,
        m_systemLibraries
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        Action,
        m_environment,
        m_name,
        m_owner,
        m_argv,
        m_workingDirectory,
        m_inputs,
        m_outputs,
        m_dynamicOutputs,
        m_orderOnly,
        m_depfile,
        m_depsStyle,
        m_environmentIsolated,
        m_always,
        m_restat,
        m_rsp,
        m_gnuRsp,
        m_compile,
        m_source
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        BuildGraph,
        m_environment,
        m_disabledTargets,
        m_actions,
        m_targetOutputs,
        m_targetKinds,
        m_targetAliases,
        m_nativeProjectPlans,
        m_targetDependencies
    )
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        IDEContext,
        m_workspace,
        m_toolchain,
        m_graph,
        m_buildDirectory,
        m_configuration,
        m_label,
        m_regenerateArguments,
        m_regenerateDirectory
    )

    // Cached command context contains resolved package interfaces, not resolver objects.
    class ConfigurationCache
    {
      public:
        static void save(const IDEContext& context)
        {
            FileSystem::writeIfChanged(context.m_buildDirectory / "axlum.context.json", nlohmann::json{{"version", 1}, {"context", context}}.dump());
        }

        static IDEContext load(const fs::path& directory)
        {
            const auto file = directory / "axlum.context.json";
            if (!fs::is_regular_file(file))
                throw std::runtime_error("configuration cache missing; run configure -B " + directory.string());
            const auto data = nlohmann::json::parse(FileSystem::readAll(file));
            if (data.at("version") != 1)
                throw std::runtime_error("configuration cache is incompatible; run configure -B " + directory.string());
            auto context = data.at("context").get<IDEContext>();
            if (fs::absolute(context.m_buildDirectory).lexically_normal() != fs::absolute(directory).lexically_normal())
                throw std::runtime_error("configuration cache was moved; run configure -B " + directory.string());
            return context;
        }
    };
} // namespace axlum
