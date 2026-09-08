# ENV-03 Windows manifest base-path evidence

Date: 2026-09-08

Source baseline: `94780b8d7`

`AssetManifest::load` previously classified a base path as absolute only when
its first character was `/`. A Windows drive-letter path such as
`G:/extracted` consequently became `<manifest-directory>/G:/extracted`. The
FrameXML matrix and client smoke tools worked around this by rewriting an
absolute source base into a relative path.

The production loader now constructs `std::filesystem::path` values, uses
`is_absolute()`, resolves only relative paths against the manifest directory, and
joins entry paths with the filesystem path operator. Both base and result are
lexically normalized without requiring the target to exist during parsing.

The headless regression creates a tiny real manifest and asset file under the
system temporary directory. On Windows, its adapter for the previous raw-string
join proves the malformed path differs and does not exist, while the production
resolver returns the real drive-letter path. A second case preserves relative
`../assets` behavior.

Validation:

```text
cmake --build build-input-ci-validation --target test_asset_manifest_paths --config Release -j 2
ctest --test-dir build-input-ci-validation -C Release -R ^asset_manifest_paths$ --output-on-failure
```

Result: 1/1 test passed in 0.63 seconds; direct execution passed 7 assertions in
2 cases. `git diff --check` passed. No GPU process, client, or server was run.
