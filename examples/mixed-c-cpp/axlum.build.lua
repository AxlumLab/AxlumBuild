-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("MixedNative")
    configuration("Debug", "Release")
    architecture("x64")

target("BaseC")
    kind("StaticLibrary")
    language("C++")
    cppdialect("C++20")
    files({"src/Base.c"})
    public_includedirs({"include/base"})
    public_defines({"BASE_PUBLIC=1"})

target("FacadeCpp")
    kind("StaticLibrary")
    language("C++")
    cppdialect("C++20")
    files({"src/Facade.cpp"})
    public_includedirs({"include/facade"})
    private_deps({"BaseC"})

target("MixedApp")
    kind("ConsoleApp")
    language("C++")
    cppdialect("C++20")
    files({"src/Main.cpp"})
    private_deps({"FacadeCpp"})
