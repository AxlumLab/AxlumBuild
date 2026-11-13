-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("ParitySuite")
    configuration("Debug", "Release")
    architecture({"x64", "arm64"})
workspace_reference("common", {file="common/axlum.build.lua"})

target("NativeFacade")
    kind("SharedLibrary")
    linktype("Modular")
    files("native/Facade.cpp")
    module("Core")
        binarymodulename("NativeCore")
        apidefine("CORE_API")
        public_includedirs("native")
        files("native/Core.cpp")
        private_deps("common::Utility/Math")

target("PchConsumer")
    kind("StaticLibrary")
    pchheader("native/Pch.hpp")
    sharedpch(true)
    inlining("On")
    functionlevellinking(true)
    buffersecuritycheck(true)
    files("native/partA.cpp")
    module("PartB")
        files("native/partB.cpp")

target("ManagedApp")
    kind("CSharpConsole")
    framework("net10.0")
    files("managed/Program.cs")
    nullable("Enable")
    private_deps("NativeFacade")

target("AndroidApp")
    kind("SharedLibrary")
    files("native/Activity.cpp")
    android_applicationid("org.axlum.parity")
    android_min_sdk(23)
    android_target_sdk(35)
