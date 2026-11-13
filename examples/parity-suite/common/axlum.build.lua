-- @Author: khfahqp khfahqp@gmail.com
-- @LastEditors: khfahqp khfahqp@gmail.com
-- Copyright (c) 2026 by AxlumLab, All Rights Reserved.

workspace("Common")
target("Utility")
    kind("StaticLibrary")
    module("Math")
        files("Math.cpp")
        public_includedirs(".")
