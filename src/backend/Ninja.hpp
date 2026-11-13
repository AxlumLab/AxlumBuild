/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"

namespace axlum
{
    class NinjaBackend
    {
      public:
        NinjaBackend() = delete;

      public:
        static void generateNinja(const BuildGraph& graph, const fs::path& buildDir, const fs::path& executable);
        static void generateCompileCommands(const BuildGraph& graph, const fs::path& buildDir);
    };

} // namespace axlum
