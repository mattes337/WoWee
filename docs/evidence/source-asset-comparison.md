# Selected original MPQ / extracted asset comparison

Read-only local audit, 2026-09-08. **All seven selected extracted files exactly
match readable entries in the original MPQs.** These are byte-equality findings,
not a certification of the complete data tree or a GPU fault diagnosis.

The comparison used Python 3.13 with mpyq 0.2.5 against all 18 `.MPQ` files below
`G:/WoW AzerothCore/Data`, opening each archive independently with listfile loading
disabled. No patch precedence was applied by the audit. All selected entries
were readable with no archive/decompression errors, no delta-patch flags and
neutral locale/platform 0. Decompressed lengths matched block-table declarations.
Original and extracted assets were not modified; only hashes/metadata are saved.

## Exact matches

Paths below are the **actual extracted manifest mappings**, including their
stored capitalization. Each mapping resolves to the same selected bytes on this
Windows filesystem. The raw manifest record and manifest SHA256 are retained
in the JSON evidence, without treating its short `h` field as SHA256.

| Extracted manifest path | Bytes | Matching MPQ | Entry/extracted SHA256 |
|---|---:|---|---|
| `interface/FrameXML/FrameXML.toc` | 2820 | `enUS\patch-enUS-3.MPQ` | `3158bea13225ae51137a389f0f3ab8566e94b6be84196dd2c1fda27024677754` |
| `interface/FrameXML/WorldFrame.lua` | 3327 | `enUS\patch-enUS-3.MPQ` | `295c9066359278a08804290346f4da72634c4d247338bb5836e8317c747d0cc4` |
| `misc/Fonts/FRIZQT__.TTF` | 62316 | `enUS\locale-enUS.MPQ` | `8f798feb09a0e9dc97daf0a54b52a9a1a7b4cf7103fcb2aa1566ab36ac4dc41c` |
| `character/human/male/humanmale.m2` | 1585376 | `patch-3.MPQ` | `b22478c1ab8d1e9bb7542f686e60e4e665a510d11b5360109a3e9e46d5bf91d2` |
| `character/human/male/humanmale00.skin` | 74176 | `patch-2.MPQ` | `cfc65fba893f05335bbd054b083b910455ef852c57ca8094ff4549741596ed0e` |
| `interface/GLUES/MODELS/UI_Human/UI_Human.m2` | 481552 | `enUS\locale-enUS.MPQ` | `1aa7299a6c99fdc733c2e62cd6d74af8357045f29ff96abf3c46767bbc2ddde7` |
| `interface/GLUES/MODELS/UI_Human/UI_Human00.skin` | 113248 | `enUS\locale-enUS.MPQ` | `f93ecae326e01dbb382ce604e7a528148e751c2e45a13a0cd6e59bcde3cf0bd6` |

For the two FrameXML files, `locale-enUS.MPQ`, `patch-enUS.MPQ` and
`patch-enUS-2.MPQ` contain different older candidates. The matching UI candidate
is specifically `patch-enUS-3.MPQ`. HumanMale.m2 matches `patch-3.MPQ`, while its
00.skin matches `patch-2.MPQ`; this is a per-entry match across archive tiers,
not evidence that every asset should be taken from one archive.

Evidence: [UI/font JSON](source-asset-comparison.json) and
[model/skin JSON](source-asset-model-comparison.json), including every discovered
candidate hash, block flags, archive size/mtime, exact manifest records, and
selected loose-file comparisons. Only the two matching UI/font archives were
also hashed in full:

- `enUS/locale-enUS.MPQ`: `45f02a3bf3964b169f397cea58113cdce5fa7bdf58d4b63f68e46255bb3bd3ea`
- `enUS/patch-enUS-3.MPQ`: `d61a60297af9044d926754d997cd5aa630500cd003d41983a9ccb5b324d61299`

## Custom loose files and provenance limits

`G:/WoW AzerothCore/Wow.exe` reports file version `3, 3, 5, 12340`, product version
`Version 3.3`; its SHA256 is
`0323f758842f048415b93163750a2be8ca6dd618ebe3df06aa9d5fa07f22c6de`.
This is a local version/hash identity, not authenticated publisher provenance.

The original Data directory also contains `patch-4.loose.json`, a custom
`.patch/dev/client` deployment manifest dated 2026-03-25. Two selected loose
model variants exist under that Data directory and differ from the extracted
files:

- `Character/Human/Male/HumanMale.m2`: loose SHA256
  `4a0f596c0d80bf78122cea419ee06bcc573545c5ee1a8371970056da5ce0c844`.
- `Interface/Glues/Models/UI_Human/UI_Human.m2`: loose SHA256
  `6ea89baa5dbc60e689f030f88d77067251292047b19a8de29fb98155579810b9`.

Neither selected loose skin exists. Neither selected FrameXML file nor the font
exists as a loose file at its logical path under the original install root or
Data root. The extracted models match the MPQ variants, **not** those custom
loose model variants. This rules out byte equality with those two particular
custom copies; it does not prove the cause of the preview GPU failure or exclude
other custom assets/configuration/runtime defects.

WoWee's current C++ extractor (`tools/asset_extract/extractor.cpp`, patch sequence
near lines 532-546) orders base then locale patches by tier and uses highest
priority first when resolving entries. The observed matches are consistent with
that local algorithm. This audit did not run the original client's loader or
prove its exact precedence, nor recover a trustworthy historical extraction
manifest linking this tree to one extraction command. The preexisting untracked
`tools/extract_assets_mpyq.py` was read but left unchanged; its priority heuristic
is different and cannot establish the extraction provenance used here.

No release-authenticated archive checksums, whole-tree comparison, later locale
variant selection, original-loader tracing or complete loose-file override audit
was performed. ENV-03's full stock/modified-data determination remains open.

## Reproduction

`tools/compare_mpq_entries.py` only reads asset trees and writes a metadata report
outside both trees. An exit 0 means a report was generated, not that stock
provenance or even every match is established; inspect candidates/errors. It
rejects independently encountered patch-delta entries rather than pretending to
reconstruct them. Full matching-archive hashes are opt-in to avoid scanning many
GB unnecessarily. For example:

```powershell
python tools/compare_mpq_entries.py --source 'G:/WoW AzerothCore/Data' --extracted Data/extracted --manifest Data/extracted/manifest.json --entry 'Interface\FrameXML\WorldFrame.lua=interface/FrameXML/WorldFrame.lua' --output logs/worldframe-comparison.json
```

Supply additional `--entry 'MPQPath=extracted/path'` values for each selected file
and `--hash-matching-archives` when full hashes of matching archives are wanted.
The committed JSON contains exactly the seven requested entries across two runs.
No game bytes or account configuration are included.
