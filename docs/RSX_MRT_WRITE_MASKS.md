# Independent MRT write masks

NV4097 SET_COLOR_MASK_MRT (byte method0x0370) carries ARGB nibbles at bits4..7,
8..11 and12..15 for secondary outputs1..3. Each nibble orders A,R,G,B from
least significant to most significant bit. The shared packer converts these
to D3D RGBA write-enable nibbles without treating zero as an unset value.
Reference: pinned RPCS3
[decoder](https://github.com/RPCS3/rpcs3/blob/f94e9f8647aeae07ea064dbafe407a41a922f485/rpcs3/Emu/RSX/rsx_decode.h)
and [reset](https://github.com/RPCS3/rpcs3/blob/f94e9f8647aeae07ea064dbafe407a41a922f485/rpcs3/Emu/RSX/rsx_methods.cpp).
The secondary mask resets to zero. Primary reset behavior is unchanged.

The command processor retains the raw word. All three VP record paths copy
packed masks; both contiguous-merge paths require equality. Existing positive
and negative PSO identities compare the complete packed value. D3D12 enables
independent target state and sets each write mask separately. Blend factors
and enables retain their prior caller behavior; secondary blend decoding is
not part of this correction. Other backends are unchanged.

The actual-command CPU fixture checks reset, method acceptance, dirty state,
zero/full masks and all ARGB roles on outputs1..3. Unexpected guest-memory or
draw milestone calls abort its metadata-only fixture. Seven authored D3D12
pixel cases cover disabled, full, partial and distinct four-target masks. The
extracted old descriptor behavior fails five cases and passes two controls;
the corrected production helper passes all seven on WARP and hardware. Fence
waits and subprocess execution are bounded. Test assets/shaders are authored.
Native graphics or race acceptance requires separate observation.

`rsx_mrt_observer.h` is an optional read-only ledger for a serialized routed
NV4097 FIFO callback. It records seen bits, last raw value, caller result and
GET address separately per subchannel, even while a caller's output gate is
closed. Unseen zero storage is not an observed zero. It does not infer register
semantics or change rendering. When analyzing a trace, distinguish control
writes from active draw commands; retained program/target bindings on a control
write do not prove that a draw used the new value. Captured logs remain private.
