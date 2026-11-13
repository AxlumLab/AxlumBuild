# CI source archives

`lua-5.5.0.tar.gz` is the unmodified upstream Lua source archive from
<https://www.lua.org/ftp/lua-5.5.0.tar.gz>. It is included because the upstream
server resets connections from GitHub-hosted runners, including HTTP fallback.

SHA-512:
`3253d2cdc929da6438095a30d66ef16a1abdbb0ada8fee238705b3b38492f14be9553640fdca6b25661e01155ba5582032e0a2ef064e4c283e85efc0a128cabe`

`PrepareDownloads.py` checks this archive against the SHA-512 in the pinned
vcpkg Lua port before placing it in the download cache. When upgrading Lua,
replace this archive with the corresponding original upstream release; never
repack it or change the checksum to bypass verification.

Lua is distributed under the MIT license; see `lua-5.5.0/doc/readme.html`
inside the archive. Release packages also include vcpkg's dependency copyright
notices.
