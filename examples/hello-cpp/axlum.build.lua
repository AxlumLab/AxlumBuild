-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("HelloCpp")
    configuration("Debug", "Release")
    architecture("x64")

target("HelloCpp")
    kind("ConsoleApp")
    language("C++")
    cppdialect("C++20")
    files({"src/Main.cpp"})
