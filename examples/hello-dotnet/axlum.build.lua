-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("ManagedHello")
    configuration("Debug", "Release")
    architecture("x64")

target("ManagedHello")
    kind("DotNetProject")
    project("Managed/ManagedHello.csproj")
