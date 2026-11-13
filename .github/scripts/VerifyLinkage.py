# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Fail packaging if the platform's static-linking contract is violated."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True)
    parser.add_argument("--stage", type=Path, default=Path("stage"))
    args = parser.parse_args()
    windows = args.platform.startswith("windows")
    binary = args.stage / "bin" / ("axlum-build.exe" if windows else "axlum-build")

    def output(*command):
        return subprocess.check_output(command, text=True, errors="replace")

    if windows:
        imports = re.findall(
            r"^\s*([\w.-]+\.dll)\s*$",
            output("dumpbin", "/dependents", str(binary)),
            re.I | re.M,
        )
        if not imports:
            raise SystemExit("Cannot read PE dependencies")
        forbidden = r"^(vcruntime|msvcp[0-9]|msvcr[0-9]|ucrtbase|libcrypto|libssl|libcurl|lua|rhodium)"
        if any(re.search(forbidden, name, re.I) for name in imports):
            raise SystemExit(f"Unexpected dynamic runtime/dependency: {imports}")
        system = Path(os.environ["SystemRoot"]) / "System32"
        if any(
            not name.lower().startswith(("api-ms-", "ext-ms-"))
            and not (system / name).is_file()
            for name in imports
        ):
            raise SystemExit(f"Non-system DLL dependency: {imports}")
        mode = "static CRT and dependencies; Windows system DLLs remain"
    elif args.platform.startswith("linux"):
        headers = output("readelf", "-lW", str(binary))
        dynamic = output("readelf", "-dW", str(binary))
        if re.search(r"\bINTERP\b", headers) or "(NEEDED)" in dynamic:
            raise SystemExit("Linux release is not a static ELF executable")
        imports = []
        mode = "static ELF; no interpreter or DT_NEEDED entries"
    else:
        imports = [
            line.strip().split(" ", 1)[0]
            for line in output("otool", "-L", str(binary)).splitlines()[1:]
            if line.strip()
        ]
        if any(
            not name.startswith(("/usr/lib/", "/System/Library/")) for name in imports
        ):
            raise SystemExit(f"Unexpected non-system dynamic dependency: {imports}")
        mode = "static dependencies; macOS system libraries remain"
    report = {"platform": args.platform, "mode": mode, "systemImports": imports}
    (args.stage / "linkage.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
