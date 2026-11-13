-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("RhodiumFmt")
    configuration("Debug", "Release")
    architecture("x64")
    cppdialect("C++20")

rhodium.require("fmt", {
    source = "https://github.com/fmtlib/fmt.git",
    version = "10.2.1", -- AxlumBuild alias for Rhodium's Git ref
    build = "header_only",
    artifacts = { include_dirs = {"include"}, defines = {"FMT_HEADER_ONLY=1"} }
})

target("FmtHello")
    kind("ConsoleApp")
    files("src/Main.cpp")
    packages("fmt")
