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

    class NativeOptions
    {
      public:
        NativeOptions() = delete;

      public:
        static CompileOptions effectiveCompile(const CompileOptions& target, const CompileOptions& module);
        static bool symbolsEnabled(const CompileOptions& options, Configuration config);
        static std::string runtimePolicy(const CompileOptions& options);
        static void validateNativeOptions(const Target& target, const Toolchain& tc);
        static std::vector<std::string> nativeCompileFlags(const CompileOptions& options, const Toolchain& tc, Configuration config, bool cpp, bool shared);
        static std::vector<std::string> nativeLinkFlags(const Target& target, const Toolchain& tc, Configuration config);
    };
} // namespace axlum
