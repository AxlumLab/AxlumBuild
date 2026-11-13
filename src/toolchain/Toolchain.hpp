/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "model/Model.hpp"
#include <map>
#include <string>

namespace rhodium
{
    struct ToolchainProfile;
}

namespace axlum
{
    struct Toolchain
    {
        CompilerKind m_kind = CompilerKind::GCC;
        std::string m_host;
        std::string m_target;
        std::string m_cCompiler;
        std::string m_cppCompiler;
        std::string m_linker;
        std::string m_archiver;
        std::string m_ninja;
        std::string m_dotnet;
        std::string m_extensionObject;
        std::string m_extensionExecutable;
        std::string m_extensionStatic;
        std::string m_extensionShared;
        std::string m_id, m_compilerVersion, m_sdkVersion, m_resourceCompiler, m_manifestTool, m_cmake, m_documentationTool, m_msbuild;
        std::string m_platform, m_architecture, m_androidAbi;
        std::vector<std::string> m_targetFlags;
        std::map<std::string, std::string> m_environment;
        std::vector<fs::path> m_systemIncludes, m_systemLibraries;
        std::shared_ptr<rhodium::ToolchainProfile> m_profile;
    };

    struct ToolchainInfo
    {
        std::string m_id, m_compiler, m_version, m_sdk, m_platform, m_architecture, m_missing;
        bool m_complete = false;
    };

    class ToolchainDiscovery
    {
      public:
        ToolchainDiscovery() = delete;

      public:
        static std::vector<ToolchainInfo> listToolchains(const fs::path& workspace = fs::current_path());
        static std::string sdkReport(const fs::path& workspace = fs::current_path());
        static Toolchain discoverToolchain(
            const std::string& requested = "auto",
            const fs::path& workspace = fs::current_path(),
            const std::string& profileId = {},
            const std::string& architecture = "x64",
            const std::string& platform = {}
        );
        static std::string toolchainFingerprint(const Toolchain& toolchain, Configuration config);
        static std::string hostTriple();
        static std::string latestCppDialect(const Toolchain& toolchain);
    };
} // namespace axlum
