# Freezing donor evidence before a port

BASE-01 is an ongoing per-port requirement. Use
[`freeze_donor_evidence.py`](../tools/freeze_donor_evidence.py) to freeze only
the source, fixtures and notices selected for a concrete port. The tool reads
the donor checkout without resetting, staging, committing or changing its
index. Dirty and untracked selected files are preserved as exact bytes.

Create a selection manifest outside the donor:

```json
{
  "task": "PORT-example",
  "sources": [
    {
      "path": "crates/example/src/feature.rs",
      "symbols": ["Feature::update"],
      "notes": "Describe the donor behavior and destination boundary"
    }
  ],
  "test_inputs": [
    {"path": "tests/fixtures/feature.json"}
  ],
  "notices": [
    {"path": "LICENSE", "notes": "Identify applicable license scope"}
  ]
}
```

These are illustrative paths; select files that actually exist. If the donor
declares its license in a package manifest instead, select that manifest as a
notice and document the declaration. An empty notice selection is explicitly
reported as incomplete license evidence. Notices are copied verbatim, and
the tool does not decide their legal applicability.

```powershell
python tools/freeze_donor_evidence.py --donor ../wow-client `
  --manifest docs/port-example-selection.json `
  --output docs/evidence/port-example
python tools/test_freeze_donor_evidence.py
```

The new output directory contains:

- `files/`: only selected regular files, retaining donor-relative paths and bytes.
- `selection.json`: the exact input manifest, including symbol and scenario notes.
- `provenance.json`: donor commit, selected Git status, per-file SHA-256 and
  byte sizes, selected source metadata, patch hashes and explicit limitations.
- `staged.patch` and `unstaged.patch`: separate binary-capable Git diffs for
  the selected paths. Untracked files appear in the snapshots and status,
  since Git does not include them in these patches.

Existing output directories, output inside the donor, path traversal,
symlink selections, directories and missing files are rejected. Capture
rechecks the selected commit/diffs/status and file bytes before writing;
if selected work changes concurrently, retry after that work settles.
Do not mistake this check for a filesystem transaction or a full checkout
snapshot. No timestamps or machine-specific absolute paths are embedded, so
unchanged inputs produce identical bundles.

After freezing, review the symbols against the snapshots, identify relevant
notices, and record the actual test command/result and destination regression
alongside the bundle. The tool does not execute tests, verify symbol meaning
or close BASE-01 globally. The existing
[PORT-08 evidence](evidence/port-08-ready-check/README.md) illustrates the
behavioral review and regression evidence that must accompany source
provenance; its earlier excerpt-based format remains valid historical evidence.
