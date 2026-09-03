# Vendored Lua

This directory is an unmodified copy of the Lua source (the C core and standard
libraries), embedded so the firmware builds without an external dependency.

| | |
| --- | --- |
| Version | 5.4.7 |
| Source | <https://www.lua.org/ftp/lua-5.4.7.tar.gz> |
| SHA-256 | 9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30 |
| License | MIT (see LICENSE) |

## What was changed

- `lua.c` and `luac.c` (the standalone interpreter and compiler `main()`s) were
  removed; only the embeddable core and libraries remain.
- Nothing else is modified. Do not hand-edit these files — re-vendor from upstream
  to upgrade.
