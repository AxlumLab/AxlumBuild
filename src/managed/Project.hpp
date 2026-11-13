/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "model/Model.hpp"

namespace axlum
{

    class ManagedProjectGenerator
    {
      public:
        ManagedProjectGenerator() = delete;

      public:
        static void validateManagedOptions(const Target& target);
        static void prepareManagedProjects(Workspace& workspace, const fs::path& directory, const fs::path& executable = {});
    };
} // namespace axlum
