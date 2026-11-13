#!/usr/bin/env python3
# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

import pathlib
import sys

s__, s_inputPath, s_implPath, s_headerPath = sys.argv
s_number = int(pathlib.Path(s_inputPath).read_text(encoding="utf-8").strip())
pathlib.Path(s_implPath).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(s_headerPath).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(s_headerPath).write_text(
    "#pragma once\nint generated_value();\n", encoding="utf-8"
)
pathlib.Path(s_implPath).write_text(
    f'#include "Value.hpp"\nint generated_value() {{ return {s_number}; }}\n',
    encoding="utf-8",
)
