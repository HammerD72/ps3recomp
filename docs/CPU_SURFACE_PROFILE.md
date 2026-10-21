# CPU surface request attribution

Set `RSX_CPU_SURFACE_PROFILE=1` to record individual CPU read requests. It is
off by default and makes no profiling-clock calls when off. Existing copy,
generation, request servicing, validation, error and 30-second deadline behavior
is preserved. This is a diagnostic, not a throughput benchmark.

`CPUSURFACE_PROFILE` reports QPC ticks and `frequency` ticks per second.
`total = queue + service + return`. Queue starts before event creation and
ends when the renderer starts the request; return includes completion delivery
and client wakeup. Within service, allocation, prior-fence, commands, copy-fence,
packing and other are disjoint and sum to service. Commands includes recording
and submission; packing includes Map, CPU row conversion and Unmap. Fence stages
include signal, wait and device-status checks, not only GPU execution.

Matched counts overlapping live surfaces; hits are matched surfaces requiring
no copy, including generations with no device writes. Host/guest byte counts
describe complete copied footprints, not requested scalar bytes. Direct renderer
requests have zero queue/return stages. Shutdown completions have zero service.

Run `python tests/check_cpu_surface_profile.py`. It compiles actual request and
service bodies with an authored clock/queue model, validates emitted sums,
default-off clocks, direct servicing, clock failure and an over-accounting
mutant. It models the copy stages; it does not execute D3D12. Real GPU pixel
packing remains covered by `rsx_surface_readback_gpu`, and actual latency needs
a native application capture with matched identities and correct output.
