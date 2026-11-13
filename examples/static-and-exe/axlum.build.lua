-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("StaticAndExe")
    configuration("Debug", "Release")
    architecture("x64")

target("Math")
    kind("StaticLibrary")
    language("C++")
    cppdialect("C++20")

    module("Arithmetic")
        files({"src/Math.cpp"})
        public_includedirs({"include"})
        public_defines({"MATH_PUBLIC=7"})
        defines({"MATH_PRIVATE=1"})

target("Hello")
    kind("ConsoleApp")
    language("C++")
    cppdialect("C++20")
    files({"src/Main.cpp"})
    private_deps({"Math"})
