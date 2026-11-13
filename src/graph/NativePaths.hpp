/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "model/Model.hpp"
#include "toolchain/Toolchain.hpp"

namespace axlum
{

    class NativePaths
    {
      public:
        NativePaths() = delete;

      public:
        static std::string
        expandBuildValue(std::string value, const Workspace& ws, const fs::path& build, Configuration config, const std::string& target = {});
        static fs::path resolveBuildInput(const Workspace& ws, const fs::path& build, const std::string& value);
        static fs::path targetArtifact(const Target& target, const Toolchain& tc, const Workspace& ws, const fs::path& build, Configuration config);
        static fs::path targetObjects(const Target& target, const Workspace& ws, const fs::path& build, Configuration config);
        static fs::path targetImportLibrary(const Target& target, const Workspace& ws, const fs::path& build, Configuration config);
    };
} // namespace axlum
