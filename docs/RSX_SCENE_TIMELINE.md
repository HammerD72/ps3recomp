# Opt-in scene readbacks

`RSX_TIMELINE_ARM_FILE` names a once-only arm file containing a hexadecimal
offscreen target offset followed by one to eight strictly increasing decimal
operation indices. Alternatively use one to eight `fp:HEX` program identities
in the requested submission order, selecting the first match after each prior
match. Do not mix modes. FP requests wait (with an explicit pending message)
for one original submission containing every identity; partial frames are never
combined. This tolerates unrelated draw-count shifts. Captured program bytes
and state, not an address alone, establish what actually executed. `RSX_CAP_DIR` selects a private output directory. Nothing
is enabled without the environment variable and arm file.

Copies execute before those operations in the original command list. They
restore the prior tracked resource state and require checked GPU retirement
before mapping. No draws are skipped or replaced. The allocation budget is
128 MiB; invalid input or failed retirement is an explicit diagnostic failure.

Each BMP now has `.raw` and `.layout` sidecars. Raw bytes retain the mapped
resource's alpha, packed bits and floating-point values; BMP visualization does
not. Layout records target, width, height, aligned row pitch, numeric DXGI
format and byte count. Interpret only pixel bytes: row padding is unspecified.
This is the host resource representation, not a guest-memory dump. Output
paths must not already exist; open/write/close failures remain fatal.

Operation metadata includes recorded surface format, MRT targets, viewport and
constant-buffer slot. A backend may define `RSX_TIMELINE_DRAW_DATA(op, file,
path)` before including the helper to retain its own verified program/constant
snapshots; otherwise metadata explicitly reports `draw_data=unavailable`.
Such a provider must identify the source and bounds rather than substitute a
later live-memory read for recorded shader bytes.

Captures and any original payloads must remain private. Readback copies and
disk writes perturb execution, so diagnostic runs are not performance proof.


## Physical binding evidence

Opt-in timeline runs also retain each successfully compiled guest FP's final
adapted HLSL, PS DXBC, VS DXBC and native x64 PSO descriptor. Per-selected-draw
binding records identify the selected physical SRV window, observed actual
CreateShaderResourceView contents (resource, format, component mapping, cube),
resource dimensions, sampler-table descriptors and program identity. The
observer retains served b0/b1 bytes, expanded VP vertices and the current VS
binary immediately before the original DrawInstanced. Unavailable program
identity is explicit. This observes CPU command/descriptor provenance; it is
not an independent GPU API-state debugger.

RSX_BOUND_INPUT_MASK optionally selects hexadecimal units1..FFFF for raw
input copies on each selected operation. The curated launcher exposes this as
--capture-bound-inputs MASK and requires scene timeline capture. Resources must
be known tracked OffRTs or uploaded VP textures, single-mip/single-layer color
2D, single-sample, at most4096 per dimension. Unknown state provenance and
unsupported resource shapes fail rather than guess. Copies precede the
original draw and restore its resource states. Its actual primary output is
copied immediately after that original draw, before later GUI passes. Checked
retirement precedes Map, raw writes and releases. No replacement draw occurs.
The128MiB binding payload/readback budget and128-resource count are separate
from the original scene timeline budget. Layout records preserve exact host
DXGI format, row pitch, row count/bytes and required total footprint bytes;
row padding is unspecified. Output data may use a partial final padded row.

PSO descriptor bytes contain process pointers and require matching host ABI/
SDK interpretation. They are private evidence, not a portable file format;
replay must replace pointer-bearing fields from captured shader/input data,
validate unsupported states and reconstruct compatible root bindings. Source,
DXBC, constants, vertices and texture bytes may contain original game payloads.
They must never enter version control, issues, releases or CI artifacts.
Matching the captured bad output establishes a reproducer, not correct game
rendering. Runtime acceptance remains a separate native observation.
