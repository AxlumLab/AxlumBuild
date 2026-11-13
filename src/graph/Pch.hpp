/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "graph/Graph.hpp"

namespace axlum
{
    struct PchPlan
    {
        std::vector<std::string> m_flags;
        std::vector<fs::path> m_inputs, m_objects, m_documents;
        fs::path m_compiledSource;
    };

    class PchPlanner
    {
      public:
        PchPlanner() = delete;

      public:
        static PchPlan planPch(
            BuildGraph& graph,
            const Toolchain& tc,
            const std::vector<std::string>& commonArguments,
            const fs::path& header,
            const fs::path& source,
            const fs::path& objectDirectory,
            const fs::path& workspace,
            const std::vector<fs::path>& orderOnly,
            const std::string& target,
            const std::string& group,
            bool documentation = false
        );
        static PchPlan useExternalPch(const Toolchain& tc, const fs::path& header, const fs::path& binary, const fs::path& object);
    };
} // namespace axlum
