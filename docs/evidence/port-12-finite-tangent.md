# PORT-12 finite character tangents

## Proven defect

`CharacterRenderer::setupModelBuffers` accepted any accumulated tangent whose
squared length was at least `1e-8`, then evaluated
`normalize(t - n * dot(n, t))`. It did not validate the length of the projected
vector. A finite triangle whose UV-derived tangent is parallel to a unit model
normal makes that projected vector exactly zero. GLM normalization then yields
nonfinite components that are copied into `CharVertexGPU::tangent` and uploaded.

The same expression assumes a unit normal. An ordinary nonunit normal can make
the result nonorthogonal, while a sufficiently large but finite normal can
overflow the intermediate multiplication and also produce nonfinite output. A
zero normal with a finite nonzero tangent does not by itself create nonfinite
tangent output, but it cannot define the basis and needs a deterministic
fallback.

This is a CPU-side model-buffer correctness defect. It is not evidence about
the separate device-loss investigation; procedural diagnostic shaders bypass
tangent reads.

## Repair

`rendering/tangent_basis.hpp` normalizes a valid normal, substitutes +Z for a
degenerate one, validates the Gram-Schmidt residual before normalization, and
constructs a perpendicular unit vector from a stable reference axis when the
residual is unusable. Handedness is computed from the resulting finite,
orthonormal basis; an unusable bitangent defaults to +1.

`test_tangent_basis.cpp` derives the parallel tangent from explicit finite
triangle edges and UV deltas. It also covers nonunit, zero, and overflow-length
normals and both regular handedness signs.

The independent fail-before probe is preserved under the ignored path
`G:/WoW Projects/logs/fork-baseline/port12-tangent-audit`. It uses the original
GLM expression and asserts that the parallel case and overflow case are
nonfinite. The same executable passes with the finite helper.
