# Packed XY16F render-target sampling

An RGBA8-rendered target can later be requested as guest Y16_X16_FLOAT (0x9F,
0xBF with the linear flag). Binding it with its RGBA8 SRV returns packed byte
channels rather than the two represented half floats. D3D typed resource view
compatibility does not permit simply changing RGBA8 to RG16F.

`rsx_texture_alias.h` decodes guest ARGB byte order to two half floats in the
sampling shader, before filtering, then applies TEXTURE_CONTROL1 remapping.
The adapter preserves this backend's existing one-mip static samplers: unit 0
point/clamp, other units linear/wrap. It does not implement additional guest
sampler policies or other incompatible format aliases. The PSO key includes
alias/remap state. Unaliased shaders remain unchanged.

Windows regression: `python tests/check_rsx_texture_alias.py` (WARP), or
`--device hardware` with no fallback. An authored RGBA8 texture contains
positive/negative half pairs. Unpatched sampling fails; adapted point/clamp,
linear/wrap, named textures, remapping and LOD/bias calls pass float readback.
There are no game bytes. `temp/checks/rsx-alias-*` retains source/executable
hashes, compile logs and results. A full Windows runtime library build passed.

Native external integration in MotorStorm US debug removed visible hill
ghosting in one ordinary menu-route capture. Fine terrain stippling and missing
bike remain. This is partial visual evidence, not game or performance acceptance.
Atomic branch is fork-only; no upstream PR has been authorized.
