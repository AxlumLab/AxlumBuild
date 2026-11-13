# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

from pathlib import Path
import sys

s_mode, s_source, s_output = sys.argv[1:]
s_source, s_output = Path(s_source), Path(s_output)
s_data = (
    (
        "#pragma once\n#define GENERATED_VALUE " + str(int(s_source.read_text())) + "\n"
    ).encode()
    if s_mode == "pre"
    else s_source.read_bytes()
)
s_output.parent.mkdir(parents=True, exist_ok=True)
if not s_output.exists() or s_output.read_bytes() != s_data:
    s_output.write_bytes(s_data)
