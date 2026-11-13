/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "managed/Project.hpp"
#include "model/Model.hpp"

namespace axlum
{
    // Lua is the primary format; the legacy TOML reader remains available.

    class ManifestLoader
    {
      public:
        ManifestLoader() = delete;

      public:
        static Workspace loadManifest(
            const fs::path& filename,
            Configuration config = Configuration::s_debug,
            const fs::path& buildDirectory = {},
            const std::string& architecture = "x64",
            const std::string& platform = {}
        );
        static Workspace loadLuaManifest(
            const fs::path& filename,
            Configuration config = Configuration::s_debug,
            const fs::path& buildDirectory = {},
            const std::string& architecture = "x64",
            const std::string& platform = {}
        );
        static Workspace loadSingleManifest(
            const fs::path& filename,
            Configuration config,
            const fs::path& buildDirectory,
            const std::string& architecture,
            const std::string& platform
        );
    };
} // namespace axlum
