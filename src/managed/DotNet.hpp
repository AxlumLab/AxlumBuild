/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "core/Diagnostics.hpp"
#include "model/Model.hpp"
#include "toolchain/Toolchain.hpp"

namespace axlum
{

    class DotNetBuilder
    {
      public:
        DotNetBuilder() = delete;

      public:
        static int buildManaged(const std::vector<std::string>& arguments);
        static int publishManaged(
            const Workspace& workspace,
            const Target& target,
            const Toolchain& toolchain,
            const fs::path& directory,
            Configuration configuration,
            const fs::path& executable,
            const PublishOptions& overrides,
            const ExecutionOptions& execution,
            bool buildFirst = true,
            fs::path* publishedDirectory = nullptr
        );
    };
} // namespace axlum
