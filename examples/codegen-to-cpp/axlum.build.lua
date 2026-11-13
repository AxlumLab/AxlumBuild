-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("CodegenHello")
    configuration("Debug", "Release")
    architecture("x64")

target("GenerateValue")
    kind("Codegen")
    tool("python3")
    args({"${workspace}/scripts/Generate.py", "${workspace}/source/value.txt", "${build}/generated/value.cpp", "${build}/generated/Value.hpp"})
    inputs({"scripts/Generate.py", "source/value.txt"})
    outputs({"generated/value.cpp", "generated/Value.hpp"})

target("CodegenHello")
    kind("ConsoleApp")
    language("C++")
    cppdialect("C++20")
    files({"src/Main.cpp", "@build/generated/value.cpp"})
    includedirs({"@build/generated"})
    private_deps({"GenerateValue"})
