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
