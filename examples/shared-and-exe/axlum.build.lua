-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("SharedAndExe")
    configuration("Debug", "Release")
    architecture("x64")

target("SharedMath")
    kind("SharedLibrary")
    language("C++")
    cppdialect("C++20")
    files({"src/Library.cpp"})
    public_includedirs({"include"})
    defines({"SHARED_MATH_EXPORTS=1"})

target("SharedApp")
    kind("ConsoleApp")
    language("C++")
    cppdialect("C++20")
    files({"src/Main.cpp"})
    private_deps({"SharedMath"})
