-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("EmbeddedRhodium")
    configuration("Debug", "Release")
    architecture("x64")
    cppdialect("C++20")

rhodium.require_local("sample_headers", {
    path = "packages/headers",
    build = "header_only",
    artifacts = { include_dirs = {"include"}, defines = {"SAMPLE_PACKAGE=7"} }
})

rhodium.require_local("sample_math", {
    path = "packages/math",
    build = "cmake",
    linkage = "static",
    dependencies = {"sample_headers"},
    artifacts = { include_dirs = {"include"}, lib_dirs = {"lib"}, libs = {"sample_math"} }
})

target("MathWrapper")
    kind("StaticLibrary")
    files("src/Wrapper.cpp")
    public_includedirs("src")
    public_packages("sample_math")

target("PackageHello")
    kind("ConsoleApp")
    files("src/Main.cpp")
    dependson("MathWrapper")
    filter("configurations:Debug")
        defines("PACKAGE_DEBUG=1")
    filter({})
