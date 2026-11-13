/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */

#pragma once

#include "model/Model.hpp"

namespace axlum
{
    struct NativeImage
    {
        std::string m_format, m_architecture;
        bool m_managed = false;
        std::vector<std::string> m_exports, m_imports;
    };

    class NativeImageInspector
    {
      public:
        NativeImageInspector() = delete;

      public:
        static NativeImage inspectNativeImage(const fs::path& file);
        static std::string fileSha256(const fs::path& file);
        static bool systemNativeDependency(const std::string& name);
    };
} // namespace axlum
