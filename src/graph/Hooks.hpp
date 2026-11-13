/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"

namespace axlum
{

    class HookPlanner
    {
      public:
        HookPlanner() = delete;

      public:
        static void planHooks(BuildGraph& graph, const Workspace& ws, const Toolchain& tc, const fs::path& build, Configuration config);
    };
} // namespace axlum
