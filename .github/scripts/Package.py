# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Validate release versions and archive the installed CLI, docs and build identity."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check-version", nargs="?", const="")
    parser.add_argument("--platform")
    parser.add_argument("--tag", default="")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    version = re.search(
        r"project\(AxlumBuild VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
        (root / "CMakeLists.txt").read_text(),
    )[1]
    assert json.loads((root / "vcpkg.json").read_text())["version-string"] == version
    tag = args.check_version if args.check_version is not None else args.tag
    if tag and tag != "v" + version:
        raise SystemExit(f"Tag {tag} does not match project version v{version}")
    if args.check_version is not None:
        print(f"Version verified: {version}")
        return
    if not args.platform:
        parser.error("--platform is required for packaging")
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()
    rhodium = subprocess.check_output(
        ["git", "-C", "third_party/rhodium", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()
    label = tag or f"v{version}-dev-{commit[:8]}"
    name = f"axlum-build-{label}-{args.platform}"
    package = root / "build" / "packages" / name
    if package.exists():
        raise SystemExit(f"Package staging directory already exists: {package}")
    shutil.copytree(root / "stage", package)
    shutil.copy2(root / "README.md", package / "README.md")
    shutil.copy2(root / "LICENSE", package / "LICENSE")
    for notice in (root / "third_party/rhodium").glob("LICENSE*"):
        if notice.is_file():
            destination = package / "licenses" / "rhodium" / notice.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, destination)
    for notice in (root / "build/vcpkg_installed").glob("*/share/*/copyright"):
        destination = package / "licenses" / notice.parent.name / "copyright"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, destination)
    shutil.copytree(root / "docs", package / "docs")
    (package / "build-info.json").write_text(
        json.dumps(
            {
                "version": version,
                "tag": tag,
                "commit": commit,
                "rhodiumCommit": rhodium,
                "platform": args.platform,
            },
            indent=2,
        )
        + "\n"
    )
    dist = root / "dist"
    dist.mkdir(exist_ok=True)
    archive = shutil.make_archive(
        str(dist / name),
        "zip" if args.platform.startswith("windows") else "gztar",
        root_dir=package.parent,
        base_dir=name,
    )
    print(archive)


if __name__ == "__main__":
    main()
