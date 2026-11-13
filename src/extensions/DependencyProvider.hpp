/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include <string>
#include <vector>

namespace axlum
{
    // Legacy generic provider contract. The embedded Rhodium bridge lives in packages/.
    struct DependencyRequest
    {
        std::string m_name, m_versionConstraint, m_triple, m_configuration, m_toolchainFingerprint;
    };

    struct ResolvedArtifact
    {
        std::string m_name, m_version, m_triple, m_configuration, m_abiFingerprint;
        std::vector<std::string> m_includes, m_defines, m_orderedLibraries, m_runtimeArtifacts;
    };

    class DependencyProvider
    {
      public:
        virtual ~DependencyProvider() = default;
        virtual ResolvedArtifact resolve(const DependencyRequest&) = 0;
    };
} // namespace axlum
