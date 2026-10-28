# Oracle

Runs the original `rdh3o.exe` (v2.12) and `RUBDUCK.EXE` (v1.02) routines side by
side with `core/` under Wine.

Requirements: Wine (32-bit), `i686-w64-mingw32-g++`, `reference/rdh3o.exe`
(extracted from `Setup_RD_2.12.exe`) and `reference/RUBDUCK.EXE`.

```sh
./tools/oracle/build.sh
wine build-oracle/oracle.exe reference/rdh3o.exe              # all unit + engine tests
wine build-oracle/oracle.exe reference/rdh3o.exe allfiles reference/content
wine build-oracle/oracle.exe reference/rdh3o.exe fuzz
wine build-oracle/oracle.exe reference/rdh3o.exe dump in.h30 out.raw 3000 [mode]

wine build-oracle/oracle_v1.exe reference/RUBDUCK.EXE              # engine, output stage, distortion, fuzz
wine build-oracle/oracle_v1.exe reference/RUBDUCK.EXE allfiles reference/content
wine build-oracle/oracle_v1.exe reference/RUBDUCK.EXE hashes tests/fixtures   # "|v1" hash lines
```

`RUBDUCK.EXE` links MFC42 and MSVCRT dynamically: MFC imports are left
unresolved (the voice never calls them) and `rand`, `_ftol`, `malloc` and `free`
are redirected through its import table.

`rdimage.c` maps the executable at its image base inside a reserved section of
the harness (Wine keeps its heap at 0x400000), resolves imports and patches
`rand`, `malloc` and `free`. Tests call the original functions through
`thiscall` pointers at their original addresses.
