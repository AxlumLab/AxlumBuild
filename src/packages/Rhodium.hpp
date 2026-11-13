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
    // Configure restores packages; graph only reads the existing lockfile/artifacts.

    class PackageManager
    {
      public:
        PackageManager() = delete;

      public:
        static void resolvePackages(Workspace& workspace, const Toolchain& toolchain, Configuration config, bool locked = false, bool restore = true);
        static std::string
        packageInputsFingerprint(const Workspace& workspace, const Toolchain& toolchain, Configuration config, const fs::path& buildDirectory);
    };
} // namespace axlum
