-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("CrossLibrary")
    configuration("Debug", "Development", "Release")
    architecture({"x64", "arm64"})
    preset("host", {config="Debug", architecture="x64"})
    preset("android-arm64", {config="Release", architecture="arm64", platform="android"})

target("CrossLibrary")
    kind("SharedLibrary")
    when("windows", function() defines("CROSS_WINDOWS=1") end)
    when("android", function() defines("CROSS_ANDROID=1") end)
    module("API")
        apidefine("CROSS_API")
        public_includedirs("include")
        files({"src/Library.cpp", "include/Library.hpp"})
