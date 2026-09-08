# Windows argv UTF-8 probe — 2026-09-08

## Scope

A standalone native probe measured the bytes presented to narrow `main` on the
Windows test host, whose active ANSI code page is 1252. It then exercised the
proposed `wmain` plus `WideCharToMultiByte(CP_UTF8,
WC_ERR_INVALID_CHARS, ...)` conversion. This did not build or modify wowee or
`framexml_run`.

The sources and results are under `D:/wowee-argv-probe-20260908`. The narrow
probe source has SHA-256
`0d3581e7b97ae57279c5dea25c516f929062af7ca24375692f50281a154af2ec`;
its executable has SHA-256
`79c7273caddb9f16b71dbac364a4430ffc0ab0f187804951f3db84fd89e1edd1`.
Python `subprocess.run` supplied the Unicode arguments. Narrow `main` received:

- `é` as `E9`, which is not the UTF-8 sequence `C3 A9`;
- `emoji-😀` as `65 6D 6F 6A 69 2D 3F 3F`;
- `mix-é-😀` as `6D 69 78 2D E9 2D 3F 3F`.

Thus treating Windows narrow argv as UTF-8 both misdecodes ACP-representable
characters and irreversibly replaces supplementary characters.

## Wide conversion result

The wide probe was compiled once with:

```text
call "I:\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /EHsc /O1 /Fe:D:\wowee-argv-probe-20260908\argv_probe_wide.exe D:\wowee-argv-probe-20260908\argv_probe_wide.cpp /link /incremental:no
```

Its source SHA-256 is
`6c9f2db77d2a6d06899742c9013cf067403239956353c56abdf1cf6937779a3b`;
the executable SHA-256 is
`742293780645f7a4583a01e5fe3749a91249d4b1b2569409a25acf5d0c03e7c6`;
and `wide-results.json` has SHA-256
`9fe823c24a349cb272f1c4cc5fe13cc6272c24d4405a4a65685b317c78ffdac1`.
All three invocations exited zero and produced:

- `é` as UTF-8 `C3 A9`;
- `emoji-😀` as `65 6D 6F 6A 69 2D F0 9F 98 80`;
- `mix-é-😀` as `6D 69 78 2D C3 A9 2D F0 9F 98 80`.

The same conversion helper rejected a synthetic unpaired UTF-16 high surrogate
with Windows error 1113 (`ERROR_NO_UNICODE_TRANSLATION`). This supports a
Windows `wmain` entry that converts every argument into owned UTF-8 strings,
then passes stable `char*` pointers to the runner's shared platform-independent
body. Conversion failure must stop argument processing rather than substitute
question marks. Non-Windows builds can retain narrow `main`.
