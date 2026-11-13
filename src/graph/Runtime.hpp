/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"

namespace axlum
{

    class RuntimePlanner
    {
      public:
        RuntimePlanner() = delete;

      public:
        static void planRuntimeCopies(BuildGraph& graph, const Workspace& ws, const fs::path& build, Configuration config, const fs::path& self);
    };
} // namespace axlum
