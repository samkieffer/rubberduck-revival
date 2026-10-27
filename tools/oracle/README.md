# Oracle

Runs the original `rdh3o.exe` routines side by side with `core/` under Wine.

Requirements: Wine (32-bit), `i686-w64-mingw32-g++`, and `reference/rdh3o.exe`
(extracted from `Setup_RD_2.12.exe`).

```sh
./tools/oracle/build.sh
wine build-oracle/oracle.exe reference/rdh3o.exe              # all unit + engine tests
wine build-oracle/oracle.exe reference/rdh3o.exe allfiles reference/content
wine build-oracle/oracle.exe reference/rdh3o.exe fuzz
wine build-oracle/oracle.exe reference/rdh3o.exe dump in.h30 out.raw 3000 [mode]
```

`rdimage.c` maps the executable at its image base inside a reserved section of
the harness (Wine keeps its heap at 0x400000), resolves imports and patches
`rand`, `malloc` and `free`. Tests call the original functions through
`thiscall` pointers at their original addresses.
