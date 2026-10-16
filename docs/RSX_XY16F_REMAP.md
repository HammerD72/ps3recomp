# XY16F component crossbar

The Y16_X16_FLOAT format (base 0x9F, including linear 0xBF) presents the
two uploaded host channels as **R,G,R,G in ARGB crossbar order**. It must not
use the ordinary four-channel A,R,G,B mapping. This is independently specified
by RPCS3 commit `f94e9f8647aeae07ea064dbafe407a41a922f485`, function
`get_component_mapping` in
[`VKFormats.cpp`](https://github.com/RPCS3/rpcs3/blob/f94e9f8647aeae07ea064dbafe407a41a922f485/rpcs3/Emu/RSX/VK/VKFormats.cpp).

For CONTROL1 0x29E4, output RGBA is host G,R,0,1. For identity 0xAAE4,
output RGBA is G,R,G,R. This changes both ordinary uploaded XY16F sampling
and the existing packed-render-target alias shader adaptation through their
shared production remap helper. It does not change alias byte reconstruction,
half/full register semantics, guest filtering or surface layout.

The six-case `rsx_texture_remap` CTest fails four XY16F cases before the fix;
ordinary ARGB and G8B8 controls pass. All six pass afterward. Seven authored
alias GPU cases pass WARP and hardware after correcting expectations to the
independent format contract. The prior GPU expectations repeated the erroneous
four-channel assumption; they were not an independent format oracle. The
raw-RGBA negative path fails all seven revised cases.

These are generic contract tests, not proof that any game's final postprocess,
motion-buffer encoding, original materials or race display is correct.

The alias fixture additionally has two asymmetric-mantissa cases. The pinned
reference's `VKGSRender.cpp/get_compatible_surface_format` maps surface8
(A8R8G8B8) to BGRA8. `VKFormats.cpp/get_format_convert_flags` marks BGRA8 as
32-bit-swapped and RG16F as16-bit-swapped. `VKTexture.cpp/copy_image_typeless`
therefore chooses `cs_shuffle_32_16`; `VKCompute.h` selects `bswap_u16_u32`
from `Program/GLSLSnippets/ShuffleBytes.glsl`, which swaps the halfwords.
For shader-output RGBA bytes80,38,40,3C, BGRA storage40,38,80,3C becomes
80,3C,40,38: host halves3C80 (1.125),3840 (0.53125). These independent
steps agree with the current alias pairs(A,R)/(G,B); no byte-order runtime
change follows. This establishes only the sampled surface8-to-XY16F contract,
not every surface format or the guest motion producer's correctness.
All nine cases pass WARP/hardware. A deliberately swapped-low-bytes adapter
passes the original seven but fails exactly the two asymmetric cases; the
runner checks these counts, demonstrating why the earlier vectors alone could
not distinguish this incorrect ordering. The unadapted negative fails all nine.


## Recorded alias sampling policy

The old backend passed the constant point/clamp mask `1`, assigning every
other alias unit bilinear/wrap regardless of recorded guest state. An observed
source-copy draw uses nearest/clamp on unit1 (`address=60030303`,
`filter=01012000`). An authored two-texel test samples at U=.49: the old
policy returns RG=(1.22,.04), whereas nearest requires (.5,1).

The backend now records filter state with address/border, excludes records with
changed filters from merging, derives supported alias policy from those
immutable registers, and includes the derived mask in PSO identity. Existing
one-mip nearest/clamp and linear/wrap implementations are retained. Other
min/mag/address combinations fail explicitly; this is not full guest filtering,
LOD, anisotropy, convolution or all addressing-mode support. Normal texture
sampling remains outside this change.

The fixture's `--guest-policy` run exercises all ten GPU cases on WARP or the
requested hardware adapter. `--guest-policy --unit-policy` restores the actual
old constant-mask call: exactly the new case fails, nine controls pass.
The separate asymmetric-byte mutant still fails exactly two of nine cases.
Policy checks also reject mixed filtering, linear/clamp and an invalid min
filter without modifying the result. The production record adapter test checks
immutable filter snapshots and merge identity. None of this establishes native
graphics or race acceptance.
