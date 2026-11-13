/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "model/Model.hpp"

namespace axlum
{

    class ResourceScanner
    {
      public:
        ResourceScanner() = delete;

      public:
        static std::vector<fs::path> resourceDependencies(const fs::path& source, const std::vector<fs::path>& includeDirs);
    };
} // namespace axlum
