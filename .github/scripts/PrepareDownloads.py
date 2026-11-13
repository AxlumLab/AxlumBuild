# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

"""Prefetch Lua with the SHA-512 pinned by the checked-out vcpkg port."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vcpkg", type=Path, default=Path(".vcpkg"))
    parser.add_argument("--downloads", type=Path, default=Path(".vcpkg-downloads"))
    args = parser.parse_args()
    port = args.vcpkg / "ports/lua"
    metadata = json.loads((port / "vcpkg.json").read_text())
    version = metadata.get(
        "version", metadata.get("version-string", metadata.get("version-semver"))
    )
    if not version or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise SystemExit("Unsupported Lua port version")
    source = (port / "portfile.cmake").read_text()
    if "https://www.lua.org/ftp/lua-${VERSION}.tar.gz" not in source:
        raise SystemExit("Lua port source changed; review the download adapter")
    digest = re.search(r"\bSHA512\s+([a-fA-F0-9]{128})\b", source)
    if not digest:
        raise SystemExit("Lua port has no literal pinned SHA-512")
    expected = digest[1].lower()
    args.downloads.mkdir(parents=True, exist_ok=True)
    destination = args.downloads / f"lua-{version}.tar.gz"
    if (
        destination.is_file()
        and hashlib.sha512(destination.read_bytes()).hexdigest() == expected
    ):
        print(f"Verified cached {destination.name}")
        return
    for scheme in ("https", "http"):
        url = f"{scheme}://www.lua.org/ftp/{destination.name}"
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                content = response.read()
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            print(f"Download unavailable: {url}: {error}")
            continue
        # HTTP fallback is accepted only with the exact repository-pinned digest.
        # Do not disable TLS verification or accept an unchecked archive.
        if hashlib.sha512(content).hexdigest() != expected:
            raise SystemExit(f"SHA-512 mismatch for {url}")
        temporary = destination.with_suffix(".tmp")
        temporary.write_bytes(content)
        temporary.replace(destination)
        print(f"Downloaded and SHA-512 verified {destination.name}")
        return
    raise SystemExit("All Lua source downloads failed")


if __name__ == "__main__":
    main()
