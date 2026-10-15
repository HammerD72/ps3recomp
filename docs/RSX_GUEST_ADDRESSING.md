# Per-draw texture addressing

VP draws snapshot the guest texture address and border registers. The compiled
pixel shader identifies sampled units; unused inherited registers do not create
sampler identities. Draw merging must compare recorded addressing with current
guest state. The curated backend's merge and SRV-binding caches participate in
these checks; the generic backend has no corresponding merge optimization.

Wrap, mirror, clamp, border and mirror-once map to D3D12. Legacy clamp and
mirror-border modes requiring shader support fail explicitly. Border color is
decoded from ARGB when a border axis uses it.

This correction covers addressing only. Filtering, anisotropy, bias and LOD keep
the existing VP policy (unit0 point; other units linear). Guest convolution and
full filtering semantics remain unfinished. This is not the full-sampler trial.

Sampler tables are immutable until checked GPU retirement. Each shader-visible
heap has at most2048 descriptors. More than128 distinct sixteen-unit tables
allocate another legal-size heap; switching heaps invalidates cached SRV root
tables. Capacity submissions retain the same lifetime rule. No presentation or
draw filtering is part of this mechanism.

On Windows with tests enabled, build `test_rsx_address_gpu` and
`test_rsx_address_tables`, then run `ctest -R ^rsx_address$ --output-on-failure`.
The production descriptor decoder passes seven authored WARP cases; the old
fixed states fail six. `test_rsx_address_gpu --hardware` selects hardware
explicitly. The production table-helper test uses authored COM storage adapters
to check131 immutable tables, heap rollover/rebinding, record identity and failed
retirement. It is not a GPU fence or native-game acceptance test.
