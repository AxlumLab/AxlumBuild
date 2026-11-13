-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("ModuleDemo")
    configuration("Debug", "Release")
    architecture("x64")
    cppdialect("C++20")

target("ModuleApp")
    kind("ConsoleApp")
    linktype("Modular") -- Change to Monolithic to merge all modules into ModuleApp.
    files("src/Main.cpp")

    module("Base")
        binarymodulename("Core")
        apidefine("BASE_API")
        public_includedirs("include")
        defines("BASE_PRIVATE=1")
        files("src/Base.cpp")

    module("Extra")
        binarymodulename("Core") -- Both modules share one binary.
        apidefine("EXTRA_API")
        public_deps("Base")
        files("src/Extra.cpp")

    module("Logic")
        binarymodulename("Logic")
        apidefine("LOGIC_API")
        public_includedirs("include")
        private_deps("Extra")
        files("src/Logic.cpp")
