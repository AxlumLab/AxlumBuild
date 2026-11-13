-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("WorkspaceSuite")
    configuration("Debug", "Development", "Release")
    architecture({"x64", "arm64"})
    preset("dev", {config="Development", architecture="x64"})
    preset("release", {config="Release", architecture="x64"})
    workspace_reference("Common", {file="common/axlum.build.lua"})

target("SuiteApp")
    kind("ConsoleApp")
    files({"src/Main.cpp"})
    private_deps("Common::Utility")
    filter("configurations:Development")
        optimize("Speed")
        symbols("On")
        defines("SUITE_DEVELOPMENT=1")
    filter({})
