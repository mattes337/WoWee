# Phase 18 — Bindless materials and indirect draws for M2

**One session, one commit.** Depends on: 01 (caps). Player sees: nothing different; a
lower CPU frame time in doodad-dense zones on T1 hardware, and the GPU cull stops stalling
on a readback. Terrain and WMO follow in later sessions on the same rails.

## Ships

- **G2 (M2 only)** — one descriptor array of every loaded M2 texture (`descriptorIndexing`,
  `runtimeDescriptorArray`, `nonuniformEXT`), per-batch material index in the instance SSBO,
  the existing `m2_cull_hiz.comp.glsl` writing compacted `VkDrawIndexedIndirectCommand`s
  plus a count (grass's compaction shader is the template, `plan-grass.md`), one
  `vkCmdDrawIndexedIndirectCount` per (opaque, blended) bucket. The CPU readback
  (`m2_renderer_render.cpp:877-885`) is gone on this path.
- Bind-per-draw path unchanged behind the T1 gate; `unavailable` on T0.

## Steps

1. `TextureRegistry` handing out indices; descriptor set with `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT`
   + `UPDATE_AFTER_BIND`, 16k entries, grows by re-creation on a fence.
2. `m2.frag.glsl` variant: `texture(uTextures[nonuniformEXT(matIndex)], uv)`; material UBO
   becomes an SSBO array indexed the same way.
3. Cull shader: append compaction (atomic index, write draw command with `firstInstance`
   = compacted slot), count buffer; barrier to `INDIRECT_COMMAND_READ`.
4. Sort key preserved: the (model, LOD) grouping the CPU path relies on
   (`m2_renderer_render.cpp:1301-1445`) is reproduced by writing one draw per (model, LOD)
   group with instance ranges — the compaction runs per group.
5. Settings; perf HUD shows draw count and cull time.

## Settings

| key | kind | default | L / M / H / U | requires |
|---|---|---|---|---|
| `gpudriven` | Bool | 1 | 1/1/1/1 | caps `descriptorIndexing` + `drawIndirectCount` (greyed on T0) |

## Reserved

```cpp
// RESERVED(phase-19, G5-vrs): the indirect path's pipeline layout carries the shading-rate
// attachment slot so VRS needs no second layout.
```

## Verify

- Compare mode all scenes: bit-identical to phase 17 with `gpudriven` on and off — this
  phase must not change a pixel.
- CPU frame time and draw-call count in Stormwind and `orgrimmar-drag` on T1: recorded
  before/after; the cull readback stall disappears from the Tracy trace
  (`docs/perf_baseline.md`).
- Validation clean with `descriptorIndexing` features enabled.

## Commit

```
Draw M2 models from the GPU's own cull list

A bindless texture array and a material index per instance let the
compute cull write compacted indirect draws instead of a visibility
mask the CPU read back. Same pixels; fewer binds, no stall. Bind-per-
draw remains for hardware without descriptor indexing.
```
