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
    // Resolve module interfaces and lower binary groups into ordinary native targets.

    class ModulePlanner
    {
      public:
        ModulePlanner() = delete;

      public:
        static Workspace planModules(const Workspace& workspace, const Toolchain& toolchain);
    };
} // namespace axlum
