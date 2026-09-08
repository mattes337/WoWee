# Static capability evidence

This is the initial source-derived portion of BASE-02, EVAL-05 and API-01.
The target is WotLK 3.3.5a/build 12340. No server revision, stock asset
provenance, clean fallback-off session or functional API certification is
claimed. The machine-readable [ledger](capability-ledger.json) identifies its
scanned inputs with a SHA-256 digest; registrations remain candidates even
when a source detector calls them provided.

Regenerate from the repository root using the locally extracted interface:

```powershell
python tools/capability_ledger.py --framexml Data/extracted/interface/FrameXML --ctest-json docs/evidence/headless-configured-tests-20260908.json
python tools/capability_ledger.py --framexml Data/extracted/interface/FrameXML --ctest-json docs/evidence/headless-configured-tests-20260908.json --check
python tools/test_capability_scanners.py
python tools/test_capability_inventory.py
python tools/validate_opcode_maps.py --expansion wotlk --strict-required --required-opcodes docs/wotlk-required-opcodes.json
```

Optionally include a specific build's configured CTest manifest:

The committed September 8 snapshot uses the saved 28-test headless/SDL manifest
from source `915c8752f`, with machine-specific source/build roots replaced by
placeholders. It records that selected build's configuration, not every test in the
full client build. Actual executed suites and focused additions are documented
separately in [headless test evidence](headless-tests.md).

```powershell
ctest --test-dir build-headless-20260908 -C Debug --show-only=json-v1 > logs/ctest-manifest.json
python tools/capability_ledger.py --ctest-json logs/ctest-manifest.json
python tools/capability_ledger.py --ctest-json logs/ctest-manifest.json --check
```

Use PowerShell 7 or another UTF-8-preserving redirect. The manifest bytes enter
the input digest, and `--check` requires the same manifest selection. The
generator does not run its commands. Every configured test remains `not-run`
in this inventory, even if an extraneous input field says `passed`; CTest's
show-only JSON is registration evidence, not a result report. Preserve actual
CTest output separately before recording pass/fail evidence. A missing
manifest is explicitly `not supplied`, rather than an empty successful suite.

Schema version 2 additionally records:

- Literal `kClientCVars` bindings and scale expressions, named default
  candidates, prefix/blanket default dispatch candidates and
  `storedCVarValue` fallback reads with source locations. Dynamic expressions
  remain unresolved; dispatch precedence and settings side effects are not
  inferred from registration.
- Widget method registration routes, implementation symbols where detected,
  and separate no-op/provided candidate dispositions using the existing shared
  provider. A name with no literal route can be dynamically generated; an
  allowlist occurrence does not override a detected real implementation.
- Optional configured test names, commands, labels and disabled properties,
  all explicitly separated from execution evidence.

The extraction is deliberately not committed. A machine without that input
can run the synthetic scanner regressions and opcode checks; it cannot
regenerate or certify the extracted-interface report. The input digest is a
content identity, not proof of stock provenance. Any scanned source change
requires regeneration. Source locations in API rows are textual references
and can also include declarations or comments; the candidate call counts
come from the existing comment/string-filtering detector.

## Reviewed warnings

All 30 WotLK missing-reference warnings have individual dispositions and
source locations in the ledger. The [review data](capability-dispositions.json)
is maintained separately from generated observations. Most warnings arise
from Classic/TBC social, flight, combat, aura or old LFG registrations.
WotLK replacements are not aliased blindly: several have different payloads.
The existing obsolete aura spellings are separately registered. The level-up
alternate shares the real mapped handler, but its unused provenance still
needs review.

The mapped `SMSG_CHAT_SERVER_MESSAGE` path and legacy logical registration now
share a typed decoder. Pinned AzerothCore source and synthetic packet fixtures
cover shutdown/restart timers and four-byte cancellation packets; commit
`cd4d477a7` fixes the previously discarded type. [Evidence and limits](evidence/eval-05-server-message-20260908.md).
Live visible-message acceptance remains open. The unmapped legacy name is an
alternate registration, not a second missing WotLK wire opcode.

The scoped strict check enforces a reviewed 15-name WotLK replacement
contract. Removing any required map entry fails it, aliases resolve, and
Classic is not required to supply WotLK features. Unscoped `--strict-required`
retains its old all-reference behavior. The 30 warnings remain visible;
the selected contract is not the complete gameplay protocol inventory.

Every one of appendix A's 49 names has a disposition and task owner.
`LOCAL_Function_Environment_Manager` is a demonstrated scanner false
positive: `RestrictedExecution.lua` declares it as the second local in a
multiple assignment and later invokes that local. The movie-recording names
are platform-dependent calls in `MacOptionsFrame.lua`. All other rows remain
explicit unresolved static candidates under their existing implementation
tasks. Their appearance in this extracted interface does not establish that
the input is stock or that a service exists.

## Remaining acceptance gates

BASE-02 remains open: CVar runtime behavior, dispatch-versus-skip dispositions,
test execution results, runtime missing-global reports and per-system live
scenarios still need to be merged into this ledger. EVAL-05 remains open for
other profiles and unresolved behavior evidence. API-01 remains open for
pinned stock/LoD input, dynamic-registration review, fallback-off runtime
reports and return/state/event contracts. No generated row is a completed
feature merely because its registration exists.
