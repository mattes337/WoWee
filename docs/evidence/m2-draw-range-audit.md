# Selected M2 draw-range audit

Read-only numeric audit, 2026-09-08. **Both selected model/skin pairs pass all
index-buffer and remapped-vertex bounds checks.** No original or extracted
assets were modified, and no model geometry bytes are committed.

| Pair | Vertices / lookup entries | Index entries | Submeshes / batches | Maximum resolved vertex | Maximum batch end |
|---|---:|---:|---:|---:|---:|
| HumanMale / HumanMale00 | 5,264 / 5,264 | 19,068 | 61 / 61 | 5,263 | 19,068 |
| UI_Human / UI_Human00 | 9,892 / 9,892 | 25,914 | 27 / 29 | 9,891 | 25,914 |

Every raw triangle index is below its lookup-array length; every referenced
lookup value is below the model's vertex count. There are zero indices that the
production loader would clamp to 0. Every batch's widened `indexStart + indexCount`
is at most the actual resolved index count. Every batch references an existing
submesh, all index counts are multiples of 3, and all selected submesh levels are 0.
No invalid-submesh fallback or final invalid-range emptying is needed.

`tools/audit_m2_draw_ranges.py` mirrors the inspected production parser in
`src/pipeline/m2_loader.cpp`: MD20 version 264, nVertices/ofsVertices at offsets 60/64,
48-byte disk vertices, 48-byte SKIN header and submesh, 24-byte disk batch.
It checks each declared array against file length, then applies the exact
triangle -> vertexLookup -> globalVertex indirection. The draw audit uses
baseVertex 0, matching the renderer path independently inspected by the renderer
agent; submesh vertexStart is not added again. The computed uint16 index-buffer
sizes are 38,136 and 51,828 bytes. These are source-data-derived sizes, not measured
GPU allocations or a proof of upload success.

[JSON evidence](m2-draw-range-audit.json) contains the exact model/skin SHA256
identities and numeric records for each draw batch. Their original MPQ matches
and manifest-resolved paths are separately recorded in
[source asset comparison](source-asset-comparison.md).

Source checkout at audit: `0ec8707585e6c349a8a8cf17756c32cabb2980d8`.
Inspected `m2_loader.cpp` SHA256: `ddf6eef59b5d3b3efb3786b4d2a0cc64fd38b8719e69ab9ff619d88084d67bfb`.
The script ran successfully on both actual pairs. Three additional synthetic
checks exercised a valid pair, an out-of-range batch end and an invalid triangle
lookup; the valid fixture passed and both invalid fixtures failed as expected.
No full client, Vulkan or server was started for this audit.

```powershell
python tools/audit_m2_draw_ranges.py --pair Data/extracted/character/human/male/humanmale.m2 Data/extracted/character/human/male/humanmale00.skin --pair Data/extracted/interface/GLUES/MODELS/UI_Human/UI_Human.m2 Data/extracted/interface/GLUES/MODELS/UI_Human/UI_Human00.skin --output logs/m2-draw-range-audit.json
```

This independent Python decoder is intentionally limited to the inspected
WotLK format; it is not execution of the production C++ loader. These results
exclude obvious source-array/remap/draw-range violations for the selected files.
They do not establish runtime buffer contents/size, upload success, vertex
attribute layout, descriptor bounds, bone matrices, shader correctness,
synchronization or GPU lifetime safety, and do not identify the TDR cause.
