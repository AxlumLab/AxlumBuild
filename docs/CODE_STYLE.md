# Code style

AxlumBuild follows the Iridium naming and layout conventions for handwritten code.

Project-owned source/header filenames use PascalCase, such as `NativeImage.cpp`,
`Process.hpp` and `CommandLifecycle.py`. Conventional build/configuration filenames
and `axlum.build.lua` retain their required names. Public C++ APIs live directly in
`namespace axlum`; headers expose class methods rather than namespace free functions.
Implementation helpers may use anonymous namespaces. CLI handling belongs to
`Application`; `Main.cpp` stays a thin entrypoint.

- Types use `PascalCase`; functions, methods, parameters and locals use `lowerCamelCase`.
- Instance data members, including public struct fields, use `m_lowerCamelCase`.
- Static data members, static locals and namespace/global variables use `s_lowerCamelCase`.
- Preprocessor definitions keep `UPPER_SNAKE_CASE`. Enum values retain their existing spelling.
- Use Allman braces, four spaces, UTF-8, LF and a final newline.
- Include the matching header first. Keep declarations in `.hpp` and implementations in `.cpp`.
- Source files use the following header; scripts use equivalent native comment syntax.

```cpp
/*
 * @Author: khfahqp khfahqp@gmail.com
 * @LastEditors: khfahqp khfahqp@gmail.com
 * Copyright (c) 2026 by AxlumLab, All Rights Reserved.
 */
```

Keep a blank line after `#pragma once`. Long parameter lists break after the
opening parenthesis, put one parameter on each line and place the closing
parenthesis on its own line. Short declarations stay on one line. Constructor
declarations and static utility methods have separate public sections. Use blank
lines between definitions and before the final namespace closing brace.

`.clang-format` defines the layout; `.editorconfig` defines text conventions.
Apply them to project-owned code under
`src`, `tests` and `examples`. Do not reformat dependencies under `third_party`,
build outputs or generated external sources.

Names required by external interfaces remain exact: C/C++ `main`, Android's
`ANativeActivity_onCreate`, C# `Main` and SDK interface overrides. Lua DSL commands,
CLI flags, JSON keys, environment variables and external library APIs keep their
published names. C++ model fields and functions adopt the conventions above;
embedders must update their source references when upgrading.

For C++/C#/C source formatting, run `clang-format -i <files>` from the repository.
Do not run automatic fixes against third-party headers.

Python helpers follow the same function/local/global naming rules and are formatted
with Black. Lua manifests retain the published DSL command names and four-space
indentation. JSON stays valid JSON without comment headers. `.gitattributes` keeps
source and configuration files on LF across platforms.

The style migration rebuilt `axlum-build`, `axlum-unit-tests` and
`axlum-native-plan-tests` in `build/`. The unit, native-plan and integration tests
passed (3/3; 94.97 seconds total). Header verification covered 147 files; formatting
verification covered 107 C/C++/C# files. Python syntax/Black checks, clang-tidy
configuration validation and `git diff --check` passed. No full regression,
NativeAOT publication or dependency tests were run.
