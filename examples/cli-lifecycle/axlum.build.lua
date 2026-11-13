-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("CliLifecycle")
    configuration("Debug", "Release")
    architecture("x64")

target("Library")
    kind("SharedLibrary")
    files("src/Library.cpp")
    public_includedirs("include")
    defines("LIBRARY_BUILD")
    installheaders("include/**.hpp")
    installheaderbase("include")

target("App")
    kind("ConsoleApp")
    files("src/Main.cpp")
    private_deps("Library")
    runtimefiles("resources/Data.txt")
    installfiles("resources/Notice.txt")
    installdatadir("share/CliLifecycle")
