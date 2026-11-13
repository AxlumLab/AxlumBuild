# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Optional real NDK matrix + generated Android Studio/CMake bridge verification."""

import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

s_cli, s_examples = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def run(*args, env=None):
    result = subprocess.run(
        list(map(str, args)), env=env, capture_output=True, text=True, errors="replace"
    )
    assert result.returncode == 0, (
        args,
        result.returncode,
        result.stdout,
        result.stderr,
    )
    return result.stdout + result.stderr


s_candidates = [
    os.environ.get("ANDROID_NDK_ROOT", ""),
    os.environ.get("ANDROID_NDK_HOME", ""),
    "C:/SDK/android/ndk/android-ndk-r27c",
]
s_ndk = next(
    (
        Path(p)
        for p in s_candidates
        if p and (Path(p) / "build/cmake/android.toolchain.cmake").exists()
    ),
    None,
)
if s_ndk is None:
    print("SKIP: real Android cross checks require an installed NDK")
    sys.exit(0)

s_env = os.environ.copy()
s_env["ANDROID_NDK_ROOT"] = str(s_ndk)
s_env["ANDROID_SDK_ROOT"] = os.environ.get("ANDROID_SDK_ROOT", str(s_ndk.parent.parent))
s_env["RHODIUM_ANDROID_API"] = "23"
with tempfile.TemporaryDirectory(prefix="axlum cross matrix ") as s_tmp:
    s_root = Path(s_tmp)
    s_ws = s_root / "sources"
    shutil.copytree(s_examples / "cross-library", s_ws)
    s_manifest = s_ws / "axlum.build.lua"
    s_matrix = s_root / "matrix"
    run(
        s_cli,
        "matrix",
        "-f",
        s_manifest,
        "-B",
        s_matrix,
        "--preset",
        "host",
        "--preset",
        "android-arm64",
        env=s_env,
    )
    s_arm = s_matrix / "android-arm64"
    s_library = s_arm / "lib" / "libCrossLibrary.so"
    s_data = s_library.read_bytes()
    assert s_data[:4] == b"\x7fELF" and struct.unpack_from("<H", s_data, 18)[0] == 183
    # Saved NDK hints must survive a subsequent build outside the discovery shell.
    assert "no work to do" in run(s_cli, "build", "-B", s_arm)
    s_x64 = s_root / "android x64"
    run(
        s_cli,
        "configure",
        "-f",
        s_manifest,
        "-B",
        s_x64,
        "--platform",
        "android",
        "--arch",
        "x64",
        "--config",
        "Release",
        env=s_env,
    )
    run(s_cli, "build", "-B", s_x64)
    assert (
        struct.unpack_from("<H", (s_x64 / "lib/libCrossLibrary.so").read_bytes(), 18)[0]
        == 62
    )
    print(
        "PASS: real host/Android preset matrix, ARM64/x64 ELF machines and saved NDK discovery"
    )

    s_ide = s_root / "ide"
    run(
        s_cli,
        "ide",
        "-f",
        s_manifest,
        "-B",
        s_root / "ide builds",
        "-o",
        s_ide,
        "--format",
        "android-studio",
        "--preset",
        "host",
        env=s_env,
    )
    s_studio = s_ide / "android-studio"
    s_cmake = shutil.which("cmake")
    s_bridge = s_root / "native bridge"
    run(
        s_cmake,
        "-Wno-dev",
        "-Wno-deprecated",
        "-S",
        s_studio / "native",
        "-B",
        s_bridge,
        "-G",
        "Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=" + str(s_ndk / "build/cmake/android.toolchain.cmake"),
        "-DANDROID_ABI=arm64-v8a",
        "-DANDROID_PLATFORM=android-23",
        "-DCMAKE_BUILD_TYPE=Release",
        env=s_env,
    )
    run(s_cmake, "--build", s_bridge, "--target", "axlum_android", env=s_env)
    s_staged = s_studio / "native/.cxx/axlum-jni/Release/arm64-v8a/libCrossLibrary.so"
    assert (
        s_staged.exists()
        and struct.unpack_from("<H", s_staged.read_bytes(), 18)[0] == 183
    )
    print(
        "PASS: generated Android Studio CMake target delegates builds and stages ARM64 JNI libraries"
    )

    s_gradle = s_examples.parent / ".cache/gradle-8.7/bin/gradle.bat"
    s_java = Path("C:/Apps/AndroidStudio/jbr")
    if os.name == "nt" and s_gradle.exists() and s_java.exists():
        s_genv = s_env.copy()
        s_genv["JAVA_HOME"] = str(s_java)
        run(
            "cmd",
            "/d",
            "/c",
            s_gradle,
            "-p",
            s_studio,
            "axlum_host_CrossLibrary",
            "--no-daemon",
            "--console=plain",
            env=s_genv,
        )
        print("PASS: generated Android Studio Gradle Exec task performs a real build")
