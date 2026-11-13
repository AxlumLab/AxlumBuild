-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("Common")
    configuration("Debug", "Development", "Release")
    architecture({"x64", "arm64"})

target("Utility")
    kind("StaticLibrary")
    files({"src/Utility.cpp", "include/Utility.hpp"})
    public_includedirs("include")
    includedirs("@build/generated")
    defines("UTILITY_PRIVATE=1")
    prebuild("GenerateConfig", {
        tool="python",
        args={"${workspace}/scripts/Actions.py", "pre", "${workspace}/value.txt", "${build}/generated/config.hpp"},
        inputs={"scripts/Actions.py", "value.txt"},
        outputs={"generated/config.hpp"}
    })
    postbuild("Publish", {
        tool="python",
        args={"${workspace}/scripts/Actions.py", "post", "${targetfile}", "${build}/published/utility.bin"},
        inputs={"scripts/Actions.py"},
        outputs={"published/utility.bin"}
    })
